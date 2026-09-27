#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/videos/video_player_instance.hpp
 * @brief Threaded libVLC Video Player Engine — Frame Buffer Bridge to Blend2D Canvas
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & WORKING PROCESS:
 * ----------------------------------------
 * VideoPlayerInstance wraps a libVLC media player and decodes video frames on a
 * DEDICATED BACKGROUND THREAD (the "3rd layer" in FolioNote's rendering pipeline).
 *
 * The 3-Layer Rendering Pipeline:
 *   Layer 1 — staticCanvasLayer  (BLImage): Grid + committed objects. Rebuilt on needsFullRebake.
 *   Layer 2 — liveInkingLayer    (BLImage): In-flight pen stroke. Wiped each gesture.
 *   Layer 3 — VIDEO DECODE THREAD          : libVLC decodes into pixelBuffer[] at native FPS.
 *               ↓ (atomic swap on main thread during Render)
 *             compositeSurface   (BLImage): Compositor reads pixelBuffer → blit_image()
 *
 * Zero-Copy Frame Hand-off:
 *   1. libVLC decode thread calls lock_cb → receives pointer to pixelBuffer (BGRA32 / PRGB32).
 *   2. libVLC decodes frame directly into our buffer (hardware-accelerated: D3D11VA on Windows).
 *   3. unlock_cb sets hasNewFrame = true (atomic, memory_order_release).
 *   4. Main render thread checks hasNewFrame → wraps pixelBuffer in BLImage via
 *      BLImage::createFromData() (zero-copy external data reference) → blit_image() into scene.
 *
 * Thread Safety Model:
 *   - pixelBuffer is protected by frameMutex (std::mutex).
 *   - hasNewFrame is std::atomic<bool> (lock-free fast-path check).
 *   - All libVLC instance/player pointers are owned exclusively by the VideoPlayerInstance.
 *   - Public API (Play/Pause/Seek/SetVolume) is safe to call from main thread.
 *
 * Graceful Degradation (no VLC installed or SDK missing):
 *   - FOLIO_HAS_LIBVLC compile-time guard disables all VLC code paths.
 *   - VideoPlayerInstance still compiles as a stub that reports ErrorState.
 *   - Render() falls back to the placeholder player card.
 *
 * Input/Output:
 *   @input  sourceUrl     — Filesystem path or HTTP/HLS URL string
 *   @output pixelBuffer   — BGRA32 raw pixel data at (videoW * videoH * 4) bytes
 *   @output hasNewFrame   — Atomic flag; main thread polls each frame
 *   @output playbackState — Current playback state enum
 *   @output timeSec       — Current playback position in seconds (from libVLC)
 *   @output durationSec   — Total media duration in seconds
 */

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstring>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// FOLIO_HAS_LIBVLC guard — set by CMake when libvlc.h is found
// ─────────────────────────────────────────────────────────────────────────────
#if defined(FOLIO_HAS_LIBVLC)
// MSVC does not define ssize_t natively, which libvlc_media.h requires
#  if defined(_MSC_VER)
#    include <BaseTsd.h>
     typedef SSIZE_T ssize_t;
#  endif

// VLC 3.x SDK headers are located in sdk/include/vlc/ and included individually.
// The CMake include path is set to sdk/include/, so we use <vlc/libvlc.h> etc.
#  include <vlc/libvlc.h>
#  include <vlc/libvlc_media.h>
#  include <vlc/libvlc_renderer_discoverer.h>
#  include <vlc/libvlc_media_player.h>
#  include <vlc/libvlc_events.h>
#endif

namespace Folio {

/**
 * @enum VideoPlayerState
 * @brief Describes the current lifecycle phase of the VideoPlayerInstance.
 */
enum class VideoPlayerState : uint8_t {
    Idle,        ///< No media loaded yet
    Loading,     ///< Media opened; waiting for first frame
    Playing,     ///< Actively decoding and presenting frames
    Paused,      ///< Decode paused; last frame is frozen in pixelBuffer
    Ended,       ///< Playback reached end of media
    Error        ///< libVLC reported a media error (check errorMessage)
};

/**
 * @class VideoPlayerInstance
 * @brief Owns the libVLC instance, media player, and pixel ring buffer for one VideoObject.
 *
 * Lifetime Model:
 *   1. Construct with a source URL.
 *   2. Call Open() to begin asynchronous media loading.
 *   3. Main render loop calls HasNewFrame() → if true, acquires LockFrame() and
 *      blits the raw BGRA32 pixels using BLImage::createFromData().
 *   4. Call Stop() before destruction (destructor also handles teardown).
 */
class VideoPlayerInstance {
public:
    // =========================================================================
    // DECODED VIDEO GEOMETRY (set once first frame is decoded)
    // =========================================================================

    int videoW = 0;     ///< Decoded frame width in pixels
    int videoH = 0;     ///< Decoded frame height in pixels

    // =========================================================================
    // PLAYBACK STATE
    // =========================================================================

    /// Current lifecycle state of the player (thread-safe: read from main thread)
    std::atomic<VideoPlayerState> state{ VideoPlayerState::Idle };

    /// Atomic flag: set to true by libVLC decode thread when a new frame is ready.
    /// Main render thread resets it to false after consuming the frame.
    std::atomic<bool> hasNewFrame{ false };

    /// Atomic flag: set to true only after at least ONE frame has actually been decoded into pixelBuffer.
    /// Prevents blitting a 100% transparent/empty buffer before the first frame arrives.
    std::atomic<bool> hasValidDecodedFrame{ false };

    /// Human-readable error description when state == Error
    std::string errorMessage;

    // =========================================================================
    // PIXEL BUFFER (thread-safe via frameMutex)
    // =========================================================================

    /**
     * @brief Raw BGRA32 pixel buffer for the latest decoded video frame.
     *
     * Memory layout:
     *   pixel[y][x] at byte offset: (y * videoW + x) * 4
     *   Channels in memory order: [B, G, R, A] — matches Blend2D BL_FORMAT_PRGB32 (BGRA)
     *
     * Size: videoW * videoH * 4 bytes
     * This buffer is pre-multiplied alpha (PRGB32) as required by Blend2D.
     */
    std::vector<uint8_t> pixelBuffer;

    /// Mutex protecting pixelBuffer during concurrent decode writes and render reads
    std::mutex frameMutex;

    // =========================================================================
    // PLAYBACK METADATA (updated each frame by VLC event thread)
    // =========================================================================

    std::atomic<float> timeSec{ 0.0f };       ///< Current playback position [seconds]
    std::atomic<float> durationSec{ 0.0f };   ///< Total media duration [seconds]
    std::atomic<int>   volumePercent{ 100 };  ///< Current audio volume [0..200]
    std::atomic<bool>  isMuted{ false };       ///< Audio muted flag
    std::atomic<float> playbackRate{ 1.0f };   ///< Playback speed multiplier

    // =========================================================================
    // CONSTRUCTOR / DESTRUCTOR
    // =========================================================================

    VideoPlayerInstance() = default;

    /**
     * @brief Destructor guarantees libVLC teardown before memory release.
     */
    ~VideoPlayerInstance() {
        Stop();
        Release();
    }

    // Non-copyable, non-movable (owns VLC pointers)
    VideoPlayerInstance(const VideoPlayerInstance&) = delete;
    VideoPlayerInstance& operator=(const VideoPlayerInstance&) = delete;

    // =========================================================================
    // PUBLIC API — LIFECYCLE
    // =========================================================================

    /**
     * @brief Opens a video file or network stream URL and begins playback.
     *
     * @param[in] url          Filesystem path or network URL (HTTP, HLS .m3u8, RTSP).
     * @param[in] startPaused  If true, open media but do not auto-play.
     * @return true on success; false if libVLC unavailable or URL invalid.
     *
     * Working Process:
     *   1. Stop() any existing playback.
     *   2. Create libvlc_instance_t with silent plugin output.
     *   3. Create libvlc_media_t from the path/URL.
     *   4. Wire memory callbacks: lock_cb / unlock_cb / display_cb.
     *   5. Start asynchronous decode via libvlc_media_player_play().
     */
    bool Open(const std::string& url, bool startPaused = false);

    /** @brief Resumes paused playback. */
    void Play();

    /** @brief Pauses playback; last frame stays frozen in pixelBuffer. */
    void Pause();

    /** @brief Stops and unloads current media. Does not destroy the VLC instance. */
    void Stop();

    /**
     * @brief Seeks to an absolute position in seconds.
     *
     * @param[in] positionSec Target time [seconds].
     *
     * Internal math: normalized_pos = positionSec / durationSec ∈ [0.0, 1.0]
     */
    void SeekTo(float positionSec);

    /**
     * @brief Sets audio volume [0..200]. 100 = 100% (original level).
     */
    void SetVolume(int percent);

    /** @brief Toggles audio mute without changing stored volume level. */
    void ToggleMute();

    /**
     * @brief Sets playback speed multiplier.
     * @param[in] rate  1.0 = normal, 0.5 = half-speed, 2.0 = double-speed.
     */
    void SetPlaybackRate(float rate);

    // =========================================================================
    // PUBLIC API — FRAME ACCESS (called from main render thread each frame)
    // =========================================================================

    /**
     * @brief Returns true when a new video frame is decoded and ready to blit.
     *
     * Usage in VideoObject::Render():
     * @code
     *   if (player && player->HasNewFrame()) {
     *       int fw, fh;
     *       const uint8_t* px = player->LockFrame(fw, fh);
     *       if (px) {
     *           BLImage frame;
     *           frame.createFromData(fw, fh, BL_FORMAT_PRGB32,
     *                                const_cast<uint8_t*>(px), fw * 4);
     *           ctx.blit_image(BLPointI(screenX, screenY), frame);
     *           player->UnlockFrame();
     *       }
     *   }
     * @endcode
     */
    [[nodiscard]] bool HasNewFrame() const noexcept {
        auto s = state.load(std::memory_order_relaxed);
        return hasNewFrame.load(std::memory_order_acquire) &&
               (s == VideoPlayerState::Playing ||
                s == VideoPlayerState::Paused  ||
                s == VideoPlayerState::Loading);
    }

    /**
     * @brief Gets the current lifecycle state of the player engine.
     */
    [[nodiscard]] VideoPlayerState GetState() const noexcept {
        return state.load(std::memory_order_relaxed);
    }

    /**
     * @brief Checks if a valid, decoded video frame is present in the pixel buffer.
     *
     * Unlike HasNewFrame() (which is reset to false once consumed), HasValidFrame() remains
     * true as long as at least one decoded frame has been received. This allows the canvas
     * compositor to keep displaying the frozen frame while the video is paused or seeking.
     *
     * @return true only after at least one real frame has been decoded and pixelBuffer is populated.
     */
    [[nodiscard]] bool HasValidFrame() const noexcept {
        return hasValidDecodedFrame.load(std::memory_order_acquire) &&
               videoW > 0 && videoH > 0 && !pixelBuffer.empty();
    }

    /**
     * @brief Locks the frame mutex and returns a raw pointer to the BGRA32 pixel data.
     *
     * @param[out] outWidth   Frame width in pixels.
     * @param[out] outHeight  Frame height in pixels.
     * @return Pointer to BGRA32 pixel data, or nullptr if no valid frame.
     *
     * IMPORTANT: Must be followed by UnlockFrame() to release the mutex.
     *            Never hold this lock across a full render frame.
     */
    const uint8_t* LockFrame(int& outWidth, int& outHeight) {
        frameMutex.lock();
        if (pixelBuffer.empty() || videoW <= 0 || videoH <= 0) {
            frameMutex.unlock();
            outWidth = outHeight = 0;
            return nullptr;
        }
        hasNewFrame.store(false, std::memory_order_release);
        outWidth  = videoW;
        outHeight = videoH;
        return pixelBuffer.data();
    }

    /**
     * @brief Releases the frame mutex acquired by LockFrame().
     */
    void UnlockFrame() {
        frameMutex.unlock();
    }

    /**
     * @brief Registers a callback invoked each time display_cb is called.
     *        Typically sets canvas.isDirty = true to trigger a Blend2D repaint.
     *
     * @param[in] cb  Callable with signature `void()`.
     */
    void SetFrameReadyCallback(std::function<void()> cb) {
        onFrameReady = std::move(cb);
    }

private:
#if defined(FOLIO_HAS_LIBVLC)
    // =========================================================================
    // LIBVLC INTERNALS (private implementation detail)
    // =========================================================================

    libvlc_instance_t*     vlcInstance = nullptr;
    libvlc_media_player_t* vlcPlayer   = nullptr;
    libvlc_media_t*        vlcMedia    = nullptr;

    /**
     * @brief Releases all libVLC handles in safe teardown order:
     *        Stop → Release player → Release media → Release instance
     */
    void Release();

    // ─────────────────────────────────────────────────────────────────────────
    // LIBVLC MEMORY VIDEO CALLBACKS
    // These three static C-linkage functions form the decode "video sink" that
    // redirects libVLC's decoded frames into our own BGRA32 pixel buffer instead
    // of rendering to a native window/surface.
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Called by libVLC decode thread BEFORE decoding a new frame.
     *
     * @param[in]  data    Pointer to VideoPlayerInstance (set via libvlc_video_set_callbacks).
     * @param[out] planes  We set planes[0] = pixelBuffer.data() so VLC writes into our buffer.
     * @return nullptr (used as the 'picture' identifier passed to unlock_cb and display_cb).
     *
     * Memory safety: pixelBuffer is pre-allocated to videoW * videoH * 4 bytes and only
     * resized when the video format is negotiated (video format callback, called once).
     */
    static void* lock_cb(void* data, void** planes);

    /**
     * @brief Called by libVLC decode thread AFTER a frame has been fully decoded.
     *
     * @param[in] data    Pointer to VideoPlayerInstance.
     * @param[in] picture Value returned by lock_cb (nullptr).
     * @param[in] planes  Plane pointer array (same as passed to lock_cb).
     *
     * Action: sets hasNewFrame = true (atomic release barrier) so the main render
     *         thread discovers the new frame on its next Render() call.
     */
    static void unlock_cb(void* data, void* picture, void* const* planes);

    /**
     * @brief Called by libVLC decode thread to "display" the decoded frame.
     *
     * @param[in] data    Pointer to VideoPlayerInstance.
     * @param[in] picture Value returned by lock_cb (nullptr).
     *
     * Action: invokes onFrameReady() callback which signals the canvas to repaint.
     */
    static void display_cb(void* data, void* picture);

    /**
     * @brief libVLC event callback for playback lifecycle state transitions.
     *
     * Handles the following event types:
     *   libvlc_MediaPlayerPlaying        → state = Playing
     *   libvlc_MediaPlayerPaused         → state = Paused
     *   libvlc_MediaPlayerStopped        → state = Ended (or Idle)
     *   libvlc_MediaPlayerEndReached     → state = Ended
     *   libvlc_MediaPlayerEncounteredError → state = Error
     *   libvlc_MediaPlayerTimeChanged    → updates timeSec atom
     *   libvlc_MediaPlayerLengthChanged  → updates durationSec atom
     */
    static void event_cb(const libvlc_event_t* event, void* data);

    /**
     * @brief libVLC log callback to capture internal decoding and media diagnostics.
     */
    static void log_cb(void* data, int level, const libvlc_log_t* ctx, const char* fmt, va_list args);

    /**
     * @brief libVLC video format negotiation callback.
     *        Called once when VLC has determined the video dimensions and format.
     *
     * @param[in,out] data    Pointer to VideoPlayerInstance pointer (double-indirect).
     * @param[in,out] chroma  4-byte chroma string. We force "BGRA" to match BL_FORMAT_PRGB32.
     * @param[in,out] width   Frame width in pixels.
     * @param[in,out] height  Frame height in pixels.
     * @param[out]    pitches Row stride for each plane [bytes].
     * @param[out]    lines   Number of rows in each plane.
     * @return Number of image planes (always 1 for packed BGRA).
     *
     * Side effect: Allocates pixelBuffer = width * height * 4 bytes.
     *              Sets videoW, videoH.
     */
    static unsigned int video_format_cb(void** data, char* chroma,
                                        unsigned int* width, unsigned int* height,
                                        unsigned int* pitches, unsigned int* lines);

    /**
     * @brief libVLC video format cleanup callback. Called once on format change/teardown.
     *
     * @param[in] data  Pointer to VideoPlayerInstance pointer.
     *
     * Side effect: clears pixelBuffer if reallocating.
     */
    static void video_cleanup_cb(void* data);

#else  // !FOLIO_HAS_LIBVLC — graceful stub
    void Release() {} ///< No-op stub when libVLC is unavailable
#endif

    /// Dirty-flag callback: called from display_cb each decoded frame
    std::function<void()> onFrameReady;
};

} // namespace Folio
