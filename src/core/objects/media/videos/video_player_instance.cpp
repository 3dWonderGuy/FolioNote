/**
 * =========================================================================================
 * @file core/objects/media/videos/video_player_instance.cpp
 * @brief libVLC Memory Callback Implementation — Decode Thread to Blend2D Frame Bridge
 * =========================================================================================
 *
 * GENERAL WORKING PROCESS:
 * -------------------------
 * This file implements the VideoPlayerInstance class defined in video_player_instance.hpp.
 * It contains all libVLC C API calls and the three core memory callbacks that form the
 * "video sink": lock_cb, unlock_cb, display_cb.
 *
 * The decode pipeline for each frame (all on VLC's internal decode thread):
 *   1. lock_cb()   → give VLC our pixelBuffer pointer as decode target
 *   2. libVLC decodes H.264/HEVC/VP9 frame into pixelBuffer (D3D11VA hw accel on Win)
 *   3. unlock_cb() → set hasNewFrame = true (atomic release)
 *   4. display_cb() → call onFrameReady() so the canvas marks isDirty = true
 *   5. Main thread render loop sees isDirty, calls Render(), checks HasNewFrame()
 *   6. LockFrame() → BLImage::createFromData(pixelBuffer) → ctx.blit_image()
 *   7. UnlockFrame() releases mutex
 */

#include "core/objects/media/videos/video_player_instance.hpp"
#include "utils/logger.hpp"

#include <cstring>
#include <cassert>

// Only compile real implementation when libVLC SDK is available
#if defined(FOLIO_HAS_LIBVLC)

namespace Folio {

// =============================================================================
// OPEN — Initialize libVLC instance, load media, wire callbacks, start decode
// =============================================================================

bool VideoPlayerInstance::Open(const std::string& url, bool startPaused) {
    if (url.empty()) {
        errorMessage = "Empty URL";
        state.store(VideoPlayerState::Error);
        return false;
    }

    LOG_INFO(CanvasObject, "VideoPlayerInstance::Open: target='" + url + "' (startPaused=" + (startPaused ? "true" : "false") + ")");

    // 1. Stop and release any previously loaded media
    Stop();
    Release();
    hasValidDecodedFrame.store(false);

    // -------------------------------------------------------------------------
    // 2. Create libVLC instance with minimal plugin output
    //    "--no-video-title"  : don't show title overlay on decode start
    //    "--avcodec-hw=any"  : enable hardware decoding (D3D11VA on Windows)
    // -------------------------------------------------------------------------
    const char* vlcArgs[] = {
        "--no-video-title-show",
        "--avcodec-hw=any",
        "--aout=directsound",   // Use DirectSound audio on Windows
    };
    constexpr int vlcArgCount = static_cast<int>(sizeof(vlcArgs) / sizeof(vlcArgs[0]));

    vlcInstance = libvlc_new(vlcArgCount, vlcArgs);
    if (!vlcInstance) {
        errorMessage = "libvlc_new() failed: VLC plugins directory not found";
        state.store(VideoPlayerState::Error);
        LOG_ERROR(CanvasObject, "Failed to create libVLC instance: " + errorMessage);
        return false;
    }

    // Attach logger to pipe libVLC internal logs into FolioNote logger
    libvlc_log_set(vlcInstance, log_cb, this);
    LOG_INFO(CanvasObject, "libVLC instance created successfully (version: " + std::string(libvlc_get_version()) + ")");

    // -------------------------------------------------------------------------
    // 3. Open media from URL or file path
    //    libvlc_media_new_path: file:// URL from local filesystem path
    //    libvlc_media_new_location: http/https/rtsp/hls streams
    // -------------------------------------------------------------------------
    bool isNetworkUrl = (url.find("http://") == 0 || url.find("https://") == 0 ||
                         url.find("rtsp://")  == 0 || url.find("rtmp://")  == 0 ||
                         url.find("file://")  == 0);

    if (isNetworkUrl) {
        LOG_INFO(CanvasObject, "Opening media location MRL: " + url);
        vlcMedia = libvlc_media_new_location(vlcInstance, url.c_str());
    } else {
        std::filesystem::path p(url);
        std::string nativePath = p.make_preferred().string();
        LOG_INFO(CanvasObject, "Opening media filesystem path: " + nativePath);
        vlcMedia = libvlc_media_new_path(vlcInstance, nativePath.c_str());
    }

    if (!vlcMedia) {
        errorMessage = "libvlc_media_new_*() failed: bad URL or path";
        state.store(VideoPlayerState::Error);
        LOG_ERROR(CanvasObject, "Failed to open media: " + url);
        return false;
    }

    if (isNetworkUrl) {
        // Smooth out network jitter, buffer ahead 2 seconds, and prevent TLS disconnects
        libvlc_media_add_option(vlcMedia, ":network-caching=2000");
        libvlc_media_add_option(vlcMedia, ":live-caching=2000");
        libvlc_media_add_option(vlcMedia, ":clock-jitter=1000");
        libvlc_media_add_option(vlcMedia, ":clock-synchro=0");
    }

    // -------------------------------------------------------------------------
    // 4. Create the media player
    // -------------------------------------------------------------------------
    vlcPlayer = libvlc_media_player_new_from_media(vlcMedia);
    if (!vlcPlayer) {
        errorMessage = "libvlc_media_player_new_from_media() failed";
        state.store(VideoPlayerState::Error);
        return false;
    }

    // -------------------------------------------------------------------------
    // 5. Wire memory video callbacks (our "video sink")
    //    This tells VLC to decode frames into our pixelBuffer instead of a window.
    //
    //    libvlc_video_set_format_callbacks: called once when VLC knows frame dimensions
    //    libvlc_video_set_callbacks: called every frame (lock/unlock/display)
    // -------------------------------------------------------------------------
    libvlc_video_set_format_callbacks(vlcPlayer, video_format_cb, video_cleanup_cb);
    libvlc_video_set_callbacks(vlcPlayer, lock_cb, unlock_cb, display_cb, this);

    // -------------------------------------------------------------------------
    // 6. Subscribe to playback lifecycle events
    // -------------------------------------------------------------------------
    libvlc_event_manager_t* evtMgr = libvlc_media_player_event_manager(vlcPlayer);
    if (evtMgr) {
        libvlc_event_attach(evtMgr, libvlc_MediaPlayerPlaying,          event_cb, this);
        libvlc_event_attach(evtMgr, libvlc_MediaPlayerPaused,           event_cb, this);
        libvlc_event_attach(evtMgr, libvlc_MediaPlayerStopped,          event_cb, this);
        libvlc_event_attach(evtMgr, libvlc_MediaPlayerEndReached,       event_cb, this);
        libvlc_event_attach(evtMgr, libvlc_MediaPlayerEncounteredError, event_cb, this);
        libvlc_event_attach(evtMgr, libvlc_MediaPlayerTimeChanged,      event_cb, this);
        libvlc_event_attach(evtMgr, libvlc_MediaPlayerLengthChanged,    event_cb, this);
    }

    // -------------------------------------------------------------------------
    // 7. Start playback (or stay paused if startPaused)
    // -------------------------------------------------------------------------
    state.store(VideoPlayerState::Loading);

    if (libvlc_media_player_play(vlcPlayer) != 0) {
        errorMessage = "libvlc_media_player_play() failed to start";
        state.store(VideoPlayerState::Error);
        return false;
    }

    if (startPaused) {
        // Give VLC a moment to decode the first frame, then pause
        libvlc_media_player_set_pause(vlcPlayer, 1);
    }

    LOG_INFO(CanvasObject, "Opened media: " + url);
    return true;
}

// =============================================================================
// PLAY / PAUSE / STOP
// =============================================================================

void VideoPlayerInstance::Play() {
    if (!vlcPlayer) return;
    libvlc_media_player_play(vlcPlayer);
}

void VideoPlayerInstance::Pause() {
    if (!vlcPlayer) return;
    if (libvlc_media_player_can_pause(vlcPlayer)) {
        libvlc_media_player_set_pause(vlcPlayer, 1);
    }
}

void VideoPlayerInstance::Stop() {
    if (!vlcPlayer) return;
    libvlc_media_player_stop(vlcPlayer);
    state.store(VideoPlayerState::Idle);
    hasNewFrame.store(false);
    hasValidDecodedFrame.store(false);
}

// =============================================================================
// SEEK / VOLUME / RATE
// =============================================================================

void VideoPlayerInstance::SeekTo(float positionSec) {
    if (!vlcPlayer) return;
    float dur = durationSec.load(std::memory_order_relaxed);
    if (dur <= 0.0f) return;

    // Normalized position: p ∈ [0.0, 1.0]
    float normalizedPos = positionSec / dur;
    normalizedPos = std::max(0.0f, std::min(1.0f, normalizedPos));

    libvlc_media_player_set_position(vlcPlayer, normalizedPos);
}

void VideoPlayerInstance::SetVolume(int percent) {
    if (!vlcPlayer) return;
    percent = std::max(0, std::min(200, percent));
    libvlc_audio_set_volume(vlcPlayer, percent);
    volumePercent.store(percent, std::memory_order_relaxed);
}

void VideoPlayerInstance::ToggleMute() {
    if (!vlcPlayer) return;
    bool currentlyMuted = isMuted.load(std::memory_order_relaxed);
    libvlc_audio_set_mute(vlcPlayer, currentlyMuted ? 0 : 1);
    isMuted.store(!currentlyMuted, std::memory_order_relaxed);
}

void VideoPlayerInstance::SetPlaybackRate(float rate) {
    if (!vlcPlayer) return;
    rate = std::max(0.1f, std::min(8.0f, rate));  // Clamp to sane range
    libvlc_media_player_set_rate(vlcPlayer, rate);
    playbackRate.store(rate, std::memory_order_relaxed);
}

// =============================================================================
// RELEASE — Safe RAII teardown of all libVLC resources
// =============================================================================

void VideoPlayerInstance::Release() {
    // Teardown order is critical: player must be stopped before releasing handles
    if (vlcPlayer) {
        libvlc_media_player_release(vlcPlayer);
        vlcPlayer = nullptr;
    }
    if (vlcMedia) {
        libvlc_media_release(vlcMedia);
        vlcMedia = nullptr;
    }
    if (vlcInstance) {
        libvlc_release(vlcInstance);
        vlcInstance = nullptr;
    }
}

// =============================================================================
// VIDEO FORMAT CALLBACK — Called once when VLC resolves frame dimensions
// =============================================================================

unsigned int VideoPlayerInstance::video_format_cb(void** data, char* chroma,
                                                    unsigned int* width,
                                                    unsigned int* height,
                                                    unsigned int* pitches,
                                                    unsigned int* lines)
{
    auto* self = static_cast<VideoPlayerInstance*>(*data);

    // Force BGRA output format — matches Blend2D BL_FORMAT_PRGB32 native layout
    // chroma is a 4-character string (not null-terminated): "BGRA"
    std::memcpy(chroma, "BGRA", 4);

    // Store decoded frame dimensions
    self->videoW = static_cast<int>(*width);
    self->videoH = static_cast<int>(*height);

    // Tightly-packed rows: stride = width * 4 bytes (no padding)
    pitches[0] = (*width) * 4;
    lines[0]   = *height;

    // Allocate pixel buffer: width * height * 4 bytes (BGRA, 4 bytes/pixel)
    {
        std::lock_guard<std::mutex> lock(self->frameMutex);
        self->pixelBuffer.assign(static_cast<size_t>(*width) * (*height) * 4, 0);
    }

    LOG_INFO(CanvasObject,
        "Video format set: " + std::to_string(*width) + "x" + std::to_string(*height) + " BGRA");

    // Return 1 plane (BGRA is a packed single-plane format)
    return 1u;
}

void VideoPlayerInstance::video_cleanup_cb(void* /*data*/) {
    // Called when format changes or on teardown; nothing to free (vector manages itself)
}

// =============================================================================
// LOCK CALLBACK — Called before each frame decode; hands VLC our pixel buffer
// =============================================================================

void* VideoPlayerInstance::lock_cb(void* data, void** planes) {
    auto* self = static_cast<VideoPlayerInstance*>(data);

    // Lock the frame mutex so the render thread cannot read while VLC is writing
    self->frameMutex.lock();

    // Give VLC our pixel buffer as the decode destination
    planes[0] = self->pixelBuffer.empty() ? nullptr : self->pixelBuffer.data();

    // Return nullptr as the 'picture' token (passed to unlock_cb and display_cb)
    return nullptr;
}

// =============================================================================
// UNLOCK CALLBACK — Called after a frame has been fully decoded into pixelBuffer
// =============================================================================

void VideoPlayerInstance::unlock_cb(void* data, void* /*picture*/, void* const* /*planes*/) {
    auto* self = static_cast<VideoPlayerInstance*>(data);

    // Mark that at least ONE valid decoded frame is present in pixelBuffer
    self->hasValidDecodedFrame.store(true, std::memory_order_release);

    // Signal the main render thread that a new frame is ready (atomic release barrier)
    self->hasNewFrame.store(true, std::memory_order_release);

    // Release the frame mutex so the main render thread can blit the frame
    self->frameMutex.unlock();
}

// =============================================================================
// DISPLAY CALLBACK — VLC "presents" the frame; we trigger canvas repaint
// =============================================================================

void VideoPlayerInstance::display_cb(void* data, void* /*picture*/) {
    auto* self = static_cast<VideoPlayerInstance*>(data);

    // Invoke the canvas dirty callback to schedule a Blend2D repaint.
    // This is safe to call from VLC's decode thread as long as onFrameReady
    // only does an atomic store (e.g. isDirty = true).
    if (self->onFrameReady) {
        self->onFrameReady();
    }
}

void VideoPlayerInstance::log_cb(void* /*data*/, int level, const libvlc_log_t* /*ctx*/, const char* fmt, va_list args) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    if (level >= 3) {
        LOG_ERROR(CanvasObject, std::string("[libVLC] ") + buf);
    } else {
        LOG_INFO(CanvasObject, std::string("[libVLC] ") + buf);
    }
}

// =============================================================================
// EVENT CALLBACK — Handles VLC playback state transitions and time updates
// =============================================================================

void VideoPlayerInstance::event_cb(const libvlc_event_t* event, void* data) {
    auto* self = static_cast<VideoPlayerInstance*>(data);

    switch (event->type) {
        case libvlc_MediaPlayerPlaying:
            self->state.store(VideoPlayerState::Playing, std::memory_order_relaxed);
            LOG_INFO(CanvasObject, "libVLC state: PLAYING");
            break;

        case libvlc_MediaPlayerPaused:
            self->state.store(VideoPlayerState::Paused, std::memory_order_relaxed);
            LOG_INFO(CanvasObject, "libVLC state: PAUSED");
            break;

        case libvlc_MediaPlayerStopped:
            self->state.store(VideoPlayerState::Ended, std::memory_order_relaxed);
            self->hasNewFrame.store(false, std::memory_order_relaxed);
            LOG_INFO(CanvasObject, "libVLC state: STOPPED");
            break;

        case libvlc_MediaPlayerEndReached:
            self->state.store(VideoPlayerState::Ended, std::memory_order_relaxed);
            self->hasNewFrame.store(false, std::memory_order_relaxed);
            LOG_INFO(CanvasObject, "libVLC state: END REACHED");
            break;

        case libvlc_MediaPlayerEncounteredError:
            self->state.store(VideoPlayerState::Error, std::memory_order_relaxed);
            self->errorMessage = "libVLC encountered a playback error";
            LOG_ERROR(CanvasObject, self->errorMessage);
            break;

        case libvlc_MediaPlayerTimeChanged: {
            // event->u.media_player_time_changed.new_time is in milliseconds
            float newTimeSec = static_cast<float>(event->u.media_player_time_changed.new_time) / 1000.0f;
            self->timeSec.store(newTimeSec, std::memory_order_relaxed);
            break;
        }

        case libvlc_MediaPlayerLengthChanged: {
            // event->u.media_player_length_changed.new_length is in milliseconds
            float newDurSec = static_cast<float>(event->u.media_player_length_changed.new_length) / 1000.0f;
            self->durationSec.store(newDurSec, std::memory_order_relaxed);
            LOG_INFO(CanvasObject, "libVLC media duration: " + std::to_string(newDurSec) + "s");
            break;
        }

        default:
            break;
    }
}

} // namespace Folio

#else // !FOLIO_HAS_LIBVLC — stub implementations for all platforms without SDK

namespace Folio {

bool VideoPlayerInstance::Open(const std::string& url, bool /*startPaused*/) {
    (void)url;
    errorMessage = "libVLC not available: rebuild with FOLIO_HAS_LIBVLC=1";
    state.store(VideoPlayerState::Error);
    return false;
}

void VideoPlayerInstance::Play()  { /* no-op stub */ }
void VideoPlayerInstance::Pause() { /* no-op stub */ }
void VideoPlayerInstance::Stop()  { state.store(VideoPlayerState::Idle); }
void VideoPlayerInstance::SeekTo(float /*positionSec*/) { /* no-op stub */ }
void VideoPlayerInstance::SetVolume(int /*percent*/)    { /* no-op stub */ }
void VideoPlayerInstance::ToggleMute()                  { /* no-op stub */ }
void VideoPlayerInstance::SetPlaybackRate(float /*rate*/) { /* no-op stub */ }

} // namespace Folio

#endif // FOLIO_HAS_LIBVLC
