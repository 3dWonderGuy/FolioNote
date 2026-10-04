/**
 * =========================================================================================
 * @file core/objects/media/videos/video_container.cpp
 * @brief VideoObject Implementation — libVLC Frame Blitting, Transport UI, YouTube Support
 * =========================================================================================
 *
 * GENERAL WORKING PROCESS:
 * -------------------------
 * This file implements VideoObject which bridges:
 *   - VideoPlayerInstance (libVLC decode thread) → decoded BGRA32 pixel frames
 *   - Blend2D canvas (main render thread) → BLImage wrapping the pixel buffer → blit_image()
 *   - ImGui/canvas interaction → Play/Pause/Seek events via context menu + on-canvas scrubber
 *
 * Render() frame blitting pipeline (per frame):
 *   1. Convert world-space (mm) bounds to screen-space (px) via viewport.WorldToScreen().
 *   2. Check player->HasNewFrame() — if true, LockFrame() → BLImage::createFromData() → blit_image()
 *   3. UnlockFrame() releases frameMutex.
 *   4. Overlay transport controls and border.
 */

#include "core/objects/media/videos/video_container.hpp"
#include "core/objects/media/images/image_decoder.hpp"
#include "core/spatial/aabb_utils.hpp"
#include "core/canvas_engine/canvas_engine.hpp"
#include "core/objects/object_registry.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <urlmon.h>
#endif

#include <SDL3/SDL.h>  // for SDL_GetTicks() in transport fade timer

namespace Folio {

// =============================================================================
// OBJECT REGISTRY SELF-REGISTRATION
// =============================================================================

namespace {
    /**
     * @brief Self-registers VideoObject with the global ObjectRegistry.
     *
     * General Working Process:
     * When the translation unit is initialized at runtime, this static constant
     * invokes ObjectRegistry::Register<VideoObject>() with factory constructor,
     * human-readable type name, and emoji icon.
     */
    [[maybe_unused]] static const bool s_registeredVideo = 
        ObjectRegistry::Register<VideoObject>(
            ObjectType::Video,
            "VideoObject",
            "🎬",
            true
        );
}

/**
 * @brief Executes a system command silently in a hidden background process and captures its stdout.
 *
 * GENERAL WORKING PROCESS & SECURITY:
 * -----------------------------------
 * On Windows, this creates an anonymous pipe with security attributes set to allow handle
 * inheritance, configures STARTUPINFOA with STARTF_USESTDHANDLES and STARTF_USESHOWWINDOW (SW_HIDE),
 * and creates the child process with CREATE_NO_WINDOW so no black console window flashes
 * in front of the user. Output is streamed into a buffer and returned as a std::string.
 *
 * @param[in] cmd Shell command line string to execute.
 * @return Standard output text emitted by the child process.
 */
static std::string RunCommandAndCaptureStdout(const std::string& cmd) {
#if defined(_WIN32)
    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return "";
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string fullCmd = "cmd.exe /c " + cmd;
    std::vector<char> cmdBuf(fullCmd.begin(), fullCmd.end());
    cmdBuf.push_back('\0');

    if (!CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return "";
    }

    // Close write handle in parent so ReadFile terminates when child exits
    CloseHandle(hWritePipe);

    std::string result;
    char buffer[1024];
    DWORD bytesRead = 0;

    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result.append(buffer, bytesRead);
    }

    // Wait up to 15 seconds for process termination
    WaitForSingleObject(pi.hProcess, 15000);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
#else
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
#endif
}

// =============================================================================
// CONSTRUCTORS
// =============================================================================

VideoObject::VideoObject() {
    type        = ObjectType::Video;
    worldWidth  = 80.0;
    worldHeight = 45.0;
    aspectRatio = 16.0 / 9.0;
    UpdateBounds();
}

VideoObject::VideoObject(const std::string& url, const std::string& name,
                         double w, double h)
    : sourceUrl(url), displayName(name)
{
    type        = ObjectType::Video;
    worldWidth  = w;
    worldHeight = h;
    aspectRatio = (h > 0.0) ? (w / h) : (16.0 / 9.0);
    UpdateBounds();
}

// =============================================================================
// SOURCE TYPE HELPERS
// =============================================================================

bool VideoObject::IsYouTube() const noexcept {
    return sourceUrl.find("youtube.com") != std::string::npos ||
           sourceUrl.find("youtu.be")    != std::string::npos;
}

bool VideoObject::IsNetworkStream() const noexcept {
    return sourceUrl.find("http://")  == 0 ||
           sourceUrl.find("https://") == 0 ||
           sourceUrl.find("rtsp://")  == 0 ||
           sourceUrl.find("rtmp://")  == 0;
}

std::string VideoObject::GetYouTubeId() const {
    // 1. Short URL: youtu.be/<ID>
    auto pos = sourceUrl.find("youtu.be/");
    if (pos != std::string::npos) {
        auto id  = sourceUrl.substr(pos + 9);
        auto end = id.find_first_of("?&#");
        return (end != std::string::npos) ? id.substr(0, end) : id;
    }

    // 2. Embed URL: youtube.com/embed/<ID>
    pos = sourceUrl.find("/embed/");
    if (pos != std::string::npos) {
        auto id  = sourceUrl.substr(pos + 7);
        auto end = id.find_first_of("?&#/");
        return (end != std::string::npos) ? id.substr(0, end) : id;
    }

    // 3. Watch URL: youtube.com/watch?v=<ID>
    pos = sourceUrl.find("v=");
    if (pos != std::string::npos) {
        auto id  = sourceUrl.substr(pos + 2);
        auto end = id.find_first_of("&");
        return (end != std::string::npos) ? id.substr(0, end) : id;
    }

    return "";
}

std::string VideoObject::GetYouTubeThumbnailUrl(const std::string& quality) const {
    std::string id = GetYouTubeId();
    if (id.empty()) return "";
    return "https://img.youtube.com/vi/" + id + "/" + quality + ".jpg";
}

// =============================================================================
// YOUTUBE ASYNCHRONOUS POSTER THUMBNAIL & STREAM RESOLUTION
// =============================================================================

/**
 * @brief Asynchronously fetches and decodes the high-resolution YouTube poster thumbnail.
 *
 * GENERAL WORKING PROCESS:
 * -------------------------
 * 1. Checks if thumbnail is already being fetched or already loaded; returns immediately if so.
 * 2. Queries YouTube CDN for the standard hqdefault (or maxresdefault) image.
 * 3. Caches the downloaded JPEG into the OS temporary directory (%TEMP%/FolioNote/yt_thumbs/<id>.jpg).
 * 4. Decodes the cached file using ImageDecoder::DecodeFromFile into a Blend2D BLImage.
 * 5. Safely updates thumbnailImg and triggers onDirty() to repaint the canvas.
 *
 * @param[in] onDirty Callback to signal canvas repaint.
 */
void VideoObject::FetchYouTubeThumbnailAsync(std::function<void()> onDirty) {
    if (!IsYouTube()) return;
    std::string videoId = GetYouTubeId();
    if (videoId.empty()) return;

    if (isFetchingThumbnail.exchange(true)) {
        return; // Already in flight
    }

    LOG_INFO(CanvasObject, "Fetching YouTube thumbnail poster for video ID: " + videoId);

    std::thread([this, videoId, onDirty]() {
        std::string thumbUrl = "https://img.youtube.com/vi/" + videoId + "/hqdefault.jpg";

        std::filesystem::path cacheDir = std::filesystem::temp_directory_path() / "FolioNote" / "yt_thumbs";
        std::error_code ec;
        std::filesystem::create_directories(cacheDir, ec);
        std::filesystem::path cacheFile = cacheDir / (videoId + ".jpg");

        bool downloaded = false;
        if (std::filesystem::exists(cacheFile, ec)) {
            downloaded = true;
        } else {
#if defined(_WIN32)
            HRESULT hr = URLDownloadToFileA(nullptr, thumbUrl.c_str(), cacheFile.string().c_str(), 0, nullptr);
            downloaded = (hr == S_OK && std::filesystem::exists(cacheFile, ec));
            if (!downloaded) {
                // Secondary fallback via Windows curl
                std::string curlCmd = "curl.exe -s -L -o \"" + cacheFile.string() + "\" \"" + thumbUrl + "\"";
                int ret = std::system(curlCmd.c_str());
                downloaded = (ret == 0 && std::filesystem::exists(cacheFile, ec));
            }
#else
            std::string curlCmd = "curl -s -L -o \"" + cacheFile.string() + "\" \"" + thumbUrl + "\"";
            int ret = std::system(curlCmd.c_str());
            downloaded = (ret == 0 && std::filesystem::exists(cacheFile, ec));
#endif
        }

        if (downloaded) {
            auto decoded = ImageDecoder::DecodeFromFile(cacheFile.string());
            if (!decoded.image.is_empty()) {
                thumbnailImg = std::move(decoded.image);
                thumbnailLoaded = true;
                LOG_INFO(CanvasObject, "Successfully loaded YouTube thumbnail poster for: " + videoId +
                         " (" + std::to_string(thumbnailImg.width()) + "x" + std::to_string(thumbnailImg.height()) + ")");
                if (onDirty) onDirty();
            }
        } else {
            LOG_WARN(CanvasObject, "Failed to download YouTube thumbnail for ID: " + videoId);
        }

        isFetchingThumbnail.store(false);
    }).detach();
}

/**
 * @brief Resolves YouTube stream URL into a direct playable stream via yt-dlp.
 *
 * GENERAL WORKING PROCESS & FALLBACK:
 * ------------------------------------
 * 1. Spawns an asynchronous worker thread so the main render/UI loop never blocks.
 * 2. Sets isResolvingStream = true so Render() displays a visual loading badge.
 * 3. Probes python -m yt_dlp or standalone yt-dlp to extract the direct .googlevideo.com media stream URL.
 * 4. If direct stream is extracted:
 *    - Caches URL in resolvedStreamUrl.
 *    - Opens media in libVLC player instance.
 *    - Sets isPlaying = true and marks canvas dirty.
 * 5. If yt-dlp is not available or resolution fails:
 *    - Seamlessly falls back to opening the video in the default system web browser (FileManager::OpenWithDefaultApp).
 *
 * @param[in] onDirty Callback to signal canvas repaint.
 */
void VideoObject::ResolveYouTubeStreamAsync(std::function<void()> onDirty) {
    if (!IsYouTube()) return;
    if (isResolvingStream.exchange(true)) {
        LOG_INFO(CanvasObject, "YouTube stream resolution already in progress for: " + sourceUrl);
        return;
    }

    LOG_INFO(CanvasObject, "Initiating YouTube stream resolution via yt-dlp for: " + sourceUrl);
    if (onDirty) onDirty();

    std::thread([this, onDirty]() {
        std::string streamUrl;

        // Sequence of commands to resolve direct media stream (multiplexed H.264 + AAC audio preferred)
        const std::vector<std::string> commands = {
            "python -m yt_dlp --no-warnings --remote-components ejs:github --js-runtimes node -g -f \"best[vcodec^=avc1][acodec!=none]/best[ext=mp4]/18/22/b\" \"" + sourceUrl + "\"",
            "yt-dlp --no-warnings --remote-components ejs:github --js-runtimes node -g -f \"best[vcodec^=avc1][acodec!=none]/best[ext=mp4]/18/22/b\" \"" + sourceUrl + "\"",
            "python -m yt_dlp --no-warnings -g -f \"best[vcodec^=avc1][acodec!=none]/best[ext=mp4]/18/22/b\" \"" + sourceUrl + "\"",
            "yt-dlp --no-warnings -g -f \"best[vcodec^=avc1][acodec!=none]/best[ext=mp4]/18/22/b\" \"" + sourceUrl + "\"",
            "python -m yt_dlp --no-warnings -g -f \"best[ext=mp4]/best\" \"" + sourceUrl + "\"",
            "yt-dlp --no-warnings -g -f \"best[ext=mp4]/best\" \"" + sourceUrl + "\""
        };

        for (const auto& cmd : commands) {
            std::string out = RunCommandAndCaptureStdout(cmd);
            size_t httpPos = out.find("http://");
            if (httpPos == std::string::npos) {
                httpPos = out.find("https://");
            }
            if (httpPos != std::string::npos) {
                size_t endPos = out.find_first_of("\r\n", httpPos);
                if (endPos != std::string::npos) {
                    streamUrl = out.substr(httpPos, endPos - httpPos);
                } else {
                    streamUrl = out.substr(httpPos);
                }
                LOG_INFO(CanvasObject, "Resolved direct YouTube stream URL via: " + cmd.substr(0, 45) + "...");
                break;
            }
        }

        if (!streamUrl.empty()) {
            resolvedStreamUrl = streamUrl;
            if (!player) {
                player = std::make_unique<VideoPlayerInstance>();
                if (onDirty) player->SetFrameReadyCallback(onDirty);
            }
            LOG_INFO(CanvasObject, "Opening resolved YouTube stream URL in libVLC");
            if (player->Open(resolvedStreamUrl, false)) {
                isPlaying = true;
            } else {
                LOG_ERROR(CanvasObject, "Failed to open resolved YouTube stream: " + player->errorMessage);
                FileManager::OpenWithDefaultApp(sourceUrl);
                isPlaying = false;
            }
        } else {
            LOG_WARN(CanvasObject, "yt-dlp unavailable or stream resolution failed for: " + sourceUrl +
                     " — Falling back to default system web browser");
            FileManager::OpenWithDefaultApp(sourceUrl);
            isPlaying = false;
        }

        isResolvingStream.store(false);
        if (onDirty) onDirty();
    }).detach();
}

// =============================================================================
// PLAYBACK CONTROLS
// =============================================================================

void VideoObject::Play(std::function<void()> onDirty) {
    // -------------------------------------------------------------------------
    // Lazy-initialize the VideoPlayerInstance on first Play() call.
    // This avoids allocating a VLC instance for every canvas VideoObject at load time.
    // -------------------------------------------------------------------------
    if (!player) {
        player = std::make_unique<VideoPlayerInstance>();
        if (onDirty) {
            player->SetFrameReadyCallback(onDirty);
        }
    } else if (onDirty) {
        player->SetFrameReadyCallback(onDirty);
    }

    // If the player is already opened and currently paused, resume playback directly!
    // This avoids tearing down VLC and re-opening the media file from scratch.
    if (player->GetState() == VideoPlayerState::Paused) {
        LOG_INFO(CanvasObject, "VideoObject::Play: Resuming paused playback for uid=" + std::to_string(uid) + " (" + displayName + ")");
        player->Play();
        isPlaying = true;
        return;
    }

    if (player->GetState() == VideoPlayerState::Playing) {
        LOG_INFO(CanvasObject, "VideoObject::Play: Already playing for uid=" + std::to_string(uid));
        isPlaying = true;
        return;
    }

    // -------------------------------------------------------------------------
    // YouTube stream resolution path
    // -------------------------------------------------------------------------
    if (IsYouTube()) {
        if (!resolvedStreamUrl.empty()) {
            LOG_INFO(CanvasObject, "VideoObject::Play: Playing previously resolved YouTube stream for uid=" + std::to_string(uid));
            if (!player->Open(resolvedStreamUrl, false)) {
                LOG_ERROR(CanvasObject, "Failed to open cached stream: " + player->errorMessage + " — re-resolving");
                resolvedStreamUrl.clear();
                ResolveYouTubeStreamAsync(onDirty);
            } else {
                isPlaying = true;
            }
            return;
        }

        ResolveYouTubeStreamAsync(onDirty);
        return;
    }

    // Standard media file / direct network stream
    LOG_INFO(CanvasObject, "VideoObject::Play: Opening media stream for uid=" + std::to_string(uid) + " (" + sourceUrl + ")");
    if (!player->Open(sourceUrl, false)) {
        LOG_ERROR(CanvasObject, "Failed to open video: " + sourceUrl + " — " + player->errorMessage);
    }
    isPlaying = true;
}

void VideoObject::Pause() {
    if (player) {
        player->Pause();
        LOG_INFO(CanvasObject, "VideoObject::Pause: Paused playback for uid=" + std::to_string(uid));
    }
    isPlaying = false;
}

void VideoObject::TogglePlay(std::function<void()> onDirty) {
    if (isPlaying) {
        Pause();
    } else {
        Play(std::move(onDirty));
    }
}

bool VideoObject::HandleCanvasClick(double wx, double wy, bool isDoubleClick,
                                     std::function<void()> onDirty)
{
    if (!isVisible) return false;

    // Check if the click lies inside the video container bounds
    if (wx < worldX || wx > worldX + worldWidth ||
        wy < worldY || wy > worldY + worldHeight) {
        return false;
    }

    LOG_INFO(CanvasObject, "VideoObject::HandleCanvasClick: uid=" + std::to_string(uid) +
             " name='" + displayName + "' isPlaying=" + (isPlaying ? "true" : "false") +
             " isDoubleClick=" + (isDoubleClick ? "true" : "false"));

    // Case 0: YouTube badge click in top-right corner opens external video in browser
    if (IsYouTube()) {
        const double bw = std::min(worldWidth * 0.18, 16.0);
        const double bh = std::min(worldHeight * 0.08, 6.0);
        const double bx = worldX + worldWidth - bw - 1.5;
        const double by = worldY + 1.5;
        if (wx >= bx && wx <= bx + bw && wy >= by && wy <= by + bh) {
            LOG_INFO(CanvasObject, "Clicked YouTube badge — opening in external browser: " + sourceUrl);
            FileManager::OpenWithDefaultApp(sourceUrl);
            return true;
        }
    }

    // Case 1: If the video is NOT currently playing, clicking anywhere on the container starts it!
    if (!isPlaying) {
        Play(onDirty);
        if (onDirty) onDirty();
        return true;
    }

    // Case 2: Double-click anywhere on an active video pauses it
    if (isDoubleClick) {
        Pause();
        if (onDirty) onDirty();
        return true;
    }

    // Case 3: Transport bar interaction (bottom strip)
    const double barH = std::max(3.0, std::min(worldHeight * 0.18, 8.0));
    const double barY = worldY + worldHeight - barH;

    if (wy >= barY && wy <= worldY + worldHeight) {
        const double pad  = std::max(0.8, worldWidth * 0.015);
        const double btnW = barH * 0.85;

        // Transport Play/Pause button (left side)
        if (wx >= worldX && wx <= worldX + pad + btnW + pad) {
            TogglePlay(onDirty);
            if (onDirty) onDirty();
            return true;
        }

        // Transport Mute button (right side)
        const double volX = worldX + worldWidth - pad - barH * 0.8;
        if (wx >= volX - pad) {
            ToggleMute();
            if (onDirty) onDirty();
            return true;
        }

        // Transport Scrubber seek bar
        const double scrubX = worldX + pad + btnW + pad;
        const double scrubW = worldWidth - (pad + btnW + pad) - (pad + barH * 2.0);
        if (scrubW > 5.0 && wx >= scrubX && wx <= scrubX + scrubW) {
            float dur = player ? player->durationSec.load(std::memory_order_relaxed) : 0.0f;
            if (dur > 0.0f) {
                double progress = std::clamp((wx - scrubX) / scrubW, 0.0, 1.0);
                float targetSec = static_cast<float>(progress * dur);
                LOG_INFO(CanvasObject, "Scrubber seek to: " + std::to_string(targetSec) + "s / " + std::to_string(dur) + "s");
                SeekTo(targetSec);
                if (onDirty) onDirty();
                return true;
            }
        }
    }

    // Case 4: Center play/pause area
    const double cx = worldX + worldWidth * 0.5;
    const double cy = worldY + worldHeight * 0.5;
    const double tSize = std::min(worldWidth, worldHeight) * 0.18;
    double distSq = (wx - cx) * (wx - cx) + (wy - cy) * (wy - cy);
    if (distSq <= (tSize * 1.6) * (tSize * 1.6)) {
        TogglePlay(onDirty);
        if (onDirty) onDirty();
        return true;
    }

    return false;
}

void VideoObject::Stop() {
    if (player) {
        player->Stop();
        LOG_INFO(CanvasObject, "VideoObject::Stop: Stopped playback for uid=" + std::to_string(uid));
    }
    isPlaying = false;
}

void VideoObject::SeekTo(float positionSec) {
    if (player) player->SeekTo(positionSec);
}

void VideoObject::SetVolume(float vol) {
    volume = std::max(0.0f, std::min(2.0f, vol));
    if (player) player->SetVolume(static_cast<int>(volume * 100.0f));
}

void VideoObject::ToggleMute() {
    isMuted = !isMuted;
    if (player) player->ToggleMute();
    LOG_INFO(CanvasObject, "VideoObject::ToggleMute: isMuted=" + std::string(isMuted ? "true" : "false"));
}

void VideoObject::ResetToNativeSize(double pixelsPerMm) {
    // Convert pixel dimensions to world millimeters.
    // Math: worldSize = nativePixels / pixelsPerMm
    if (nativeVideoW > 0 && nativeVideoH > 0 && pixelsPerMm > 0.0) {
        worldWidth  = static_cast<double>(nativeVideoW) / pixelsPerMm;
        worldHeight = static_cast<double>(nativeVideoH) / pixelsPerMm;
        aspectRatio = static_cast<double>(nativeVideoW) / static_cast<double>(nativeVideoH);
        transform   = BLMatrix2D::make_identity();
        UpdateBounds();
    }
}

// =============================================================================
// TRANSFORM & BAKING — Aspect Ratio Always Enforced
// =============================================================================

void VideoObject::BakeTransform() {
    if (Folio::AABBUtils::BakeTransformedRect(worldX, worldY, worldWidth, worldHeight, transform, 0.5)) {
        // VIDEO: Aspect ratio is ALWAYS locked — even after edge-handle resizing.
        // If the bake produced a non-matching ratio, we correct the height.
        //
        // Math: worldHeight = worldWidth / aspectRatio
        //       This preserves width (user's last explicit dimension) and
        //       re-derives height from the locked ratio.
        if (aspectRatio > 0.0 && worldWidth > 0.0) {
            worldHeight = worldWidth / aspectRatio;
        }
        UpdateBounds();
    }
}

// =============================================================================
// RENDER — Frame Blit + Transport Controls + Placeholder Card
// =============================================================================

void VideoObject::Render(BLContext& ctx, const Viewport& viewport) const {
    if (!isVisible) return;

    if (worldWidth <= 0.0 || worldHeight <= 0.0) return;

    ctx.save();
    // Apply object's affine transform (for rotation / free-form resize history)
    ctx.apply_transform(transform);

    // Canvas coordinate space:
    // ctx has already been transformed into screen space by staticCtx.set_transform(renderMatrix).
    // Therefore, all drawing operations inside Render() take place directly in canvas world coordinates (mm),
    // matching the behavior of ImageObject, TextBoxObject, and InkContainer.
    const double x = worldX;
    const double y = worldY;
    const double w = worldWidth;
    const double h = worldHeight;

    // Hairline stroke scaling: keeps borders at consistent physical/screen pixel weight under zooming
    // Math: strokeScale = 1.0 / zoom
    const double strokeScale = (viewport.zoom > 0.001) ? (1.0 / viewport.zoom) : 1.0;

    // ─────────────────────────────────────────────────────────────────────────
    // 2. BLIT DECODED VIDEO FRAME (if player has a new frame ready or valid frame cached)
    // ─────────────────────────────────────────────────────────────────────────
    bool frameBlitted = false;

    if (player && (player->HasNewFrame() || player->HasValidFrame())) {
        int fw = 0, fh = 0;
        const uint8_t* px = player->LockFrame(fw, fh);

        if (px && fw > 0 && fh > 0) {
            // Update native dimensions if this is the first frame
            if (nativeVideoW != fw || nativeVideoH != fh) {
                const_cast<VideoObject*>(this)->nativeVideoW = fw;
                const_cast<VideoObject*>(this)->nativeVideoH = fh;
                const_cast<VideoObject*>(this)->aspectRatio  = static_cast<double>(fw) / static_cast<double>(fh);
            }

            // Zero-copy: wrap the BGRA32 pixel buffer in a BLImage (external data reference)
            // BL_FORMAT_PRGB32 = pre-multiplied BGRA32, matching libVLC's "BGRA" sink output.
            BLImage frame;
            BLResult res = frame.create_from_data(fw, fh, BL_FORMAT_PRGB32,
                                                const_cast<uint8_t*>(px),
                                                static_cast<intptr_t>(fw) * 4);

            if (res == BL_SUCCESS) {
                BLRect destRect(x, y, w, h);
                ctx.blit_image(destRect, frame);
                frameBlitted = true;
            }

            player->UnlockFrame();
        } else if (px == nullptr) {
            player->UnlockFrame();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3. RENDER PLACEHOLDER / POSTER THUMBNAIL if no frame is decoded
    // ─────────────────────────────────────────────────────────────────────────
    if (!frameBlitted) {
        if (thumbnailLoaded && !thumbnailImg.is_empty()) {
            BLRect destRect(x, y, w, h);
            ctx.blit_image(destRect, thumbnailImg);

            // Overlay dark circular glow and center play triangle
            const double cx = x + w * 0.5;
            const double cy = y + h * 0.5;
            const double tSize = std::min(w, h) * 0.16;

            ctx.set_fill_style(BLRgba32(0x00, 0x00, 0x00, 120));
            ctx.fill_circle(cx, cy, tSize * 1.5);

            if (!isPlaying && !isResolvingStream.load()) {
                BLPath tri;
                tri.move_to(cx - tSize * 0.35, cy - tSize * 0.8);
                tri.line_to(cx + tSize * 0.9,  cy);
                tri.line_to(cx - tSize * 0.35, cy + tSize * 0.8);
                tri.close();
                ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 240));
                ctx.fill_path(tri);
            }
        } else {
            RenderPlaceholderCard(ctx, x, y, w, h);
        }

        // Background stream resolution progress indicator badge
        if (isResolvingStream.load()) {
            const double rw = std::min(w * 0.85, 45.0);
            const double rh = std::min(h * 0.22, 9.0);
            const double rx = x + (w - rw) * 0.5;
            const double ry = y + (h - rh) * 0.5;
            ctx.set_fill_style(BLRgba32(0x18, 0x1A, 0x22, 235));
            ctx.fill_round_rect(BLRoundRect(rx, ry, rw, rh, 3.0, 3.0));
            ctx.set_stroke_style(BLRgba32(0xFF, 0x00, 0x00, 220));
            ctx.set_stroke_width(0.6 * strokeScale);
            ctx.stroke_round_rect(BLRoundRect(rx, ry, rw, rh, 3.0, 3.0));
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4. TRANSPORT CONTROLS OVERLAY
    // ─────────────────────────────────────────────────────────────────────────
    uint64_t nowMs = SDL_GetTicks();
    bool showControls = (nowMs - lastHoverMs) < CONTROLS_FADE_DELAY_MS;

    if (showControls && w >= 15.0) {
        float timeSec = player ? player->timeSec.load(std::memory_order_relaxed) : 0.0f;
        float durSec  = player ? player->durationSec.load(std::memory_order_relaxed) : 0.0f;
        RenderTransportBar(ctx, x, y, w, h, timeSec, durSec);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5. BORDER OUTLINE
    // ─────────────────────────────────────────────────────────────────────────
    ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 180));
    ctx.set_stroke_width(0.8 * strokeScale);
    ctx.stroke_rect(BLRect(x, y, w, h));

    // ─────────────────────────────────────────────────────────────────────────
    // 6. YOUTUBE BADGE (red pill with white play triangle in top-right corner)
    // ─────────────────────────────────────────────────────────────────────────
    if (IsYouTube() && w > 10.0) {
        const double bw = std::min(w * 0.18, 14.0);
        const double bh = std::min(h * 0.08, 5.0);
        const double bx = x + w - bw - 1.2;
        const double by = y + 1.2;

        ctx.set_fill_style(BLRgba32(0xFF, 0x00, 0x00, 235));
        ctx.fill_round_rect(BLRoundRect(bx, by, bw, bh, bh * 0.35, bh * 0.35));

        // Draw YouTube white play triangle inside the badge
        const double triH = bh * 0.45;
        const double triW = triH * 0.85;
        const double tcx  = bx + (bw - triW) * 0.5;
        const double tcy  = by + (bh - triH) * 0.5;
        BLPath miniTri;
        miniTri.move_to(tcx, tcy);
        miniTri.line_to(tcx + triW, tcy + triH * 0.5);
        miniTri.line_to(tcx, tcy + triH);
        miniTri.close();
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 255));
        ctx.fill_path(miniTri);
    }

    ctx.restore();
}

// =============================================================================
// PRIVATE: Placeholder Player Card (no frame / not playing)
// =============================================================================

void VideoObject::RenderPlaceholderCard(BLContext& ctx, double sx, double sy,
                                         double sw, double sh) const
{
    // Dark semi-transparent background
    ctx.set_fill_style(BLRgba32(0x12, 0x14, 0x1A, static_cast<uint8_t>(opacity * 240)));
    ctx.fill_rect(BLRect(sx, sy, sw, sh));

    // Center play triangle
    const double cx     = sx + sw * 0.5;
    const double cy     = sy + sh * 0.5;
    const double tSize  = std::min(sw, sh) * 0.18;

    // Ambient glow behind play button
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 22));
    ctx.fill_circle(cx, cy, tSize * 1.6);

    if (!isPlaying) {
        // White play triangle
        BLPath tri;
        tri.move_to(cx - tSize * 0.4, cy - tSize);
        tri.line_to(cx + tSize,        cy);
        tri.line_to(cx - tSize * 0.4, cy + tSize);
        tri.close();
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, static_cast<uint8_t>(opacity * 220)));
        ctx.fill_path(tri);
    } else {
        // Pause icon: two vertical bars
        const double bw = tSize * 0.32;
        const double bh = tSize * 1.3;
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 210));
        ctx.fill_rect(cx - bw * 1.5, cy - bh * 0.5, bw, bh);
        ctx.fill_rect(cx + bw * 0.5, cy - bh * 0.5, bw, bh);
    }

    // Display name label at bottom
    if (!displayName.empty() && sw > 60.0) {
        // Gradient scrim at bottom
        BLGradient scrim(BLLinearGradientValues(sx, sy + sh * 0.7, sx, sy + sh));
        scrim.add_stop(0.0, BLRgba32(0x00, 0x00, 0x00, 0));
        scrim.add_stop(1.0, BLRgba32(0x00, 0x00, 0x00, 160));
        ctx.set_fill_style(scrim);
        ctx.fill_rect(BLRect(sx, sy + sh * 0.7, sw, sh * 0.3));
    }
}

// =============================================================================
// PRIVATE: Transport Control Bar (Play/Pause, Scrubber, Timecode, Volume)
// =============================================================================

void VideoObject::RenderTransportBar(BLContext& ctx, double sx, double sy,
                                      double sw, double sh,
                                      float timeSec, float durSec) const
{
    // Transport bar sits at the bottom, 18% of video height (max 8mm)
    const double barH   = std::max(3.0, std::min(sh * 0.18, 8.0));
    const double barY   = sy + sh - barH;
    const double pad    = std::max(0.8, sw * 0.015);

    // Semi-transparent background scrim
    ctx.set_fill_style(BLRgba32(0x00, 0x00, 0x00, 185));
    ctx.fill_rect(BLRect(sx, barY, sw, barH));

    // ── Play/Pause button ────────────────────────────────────────────────────
    const double btnW = barH * 0.85;
    const double btnCx = sx + pad + btnW * 0.5;
    const double btnCy = barY + barH * 0.5;

    if (!isPlaying) {
        // Play triangle
        double ts = barH * 0.22;
        BLPath tri;
        tri.move_to(btnCx - ts * 0.4, btnCy - ts);
        tri.line_to(btnCx + ts,        btnCy);
        tri.line_to(btnCx - ts * 0.4, btnCy + ts);
        tri.close();
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 240));
        ctx.fill_path(tri);
    } else {
        // Pause bars
        double bw2 = barH * 0.07;
        double bh2 = barH * 0.36;
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 240));
        ctx.fill_rect(btnCx - bw2 * 1.5, btnCy - bh2 * 0.5, bw2, bh2);
        ctx.fill_rect(btnCx + bw2 * 0.5, btnCy - bh2 * 0.5, bw2, bh2);
    }

    // ── Scrubber / Progress Bar ───────────────────────────────────────────────
    const double scrubX = sx + pad + btnW + pad;
    const double scrubW = sw - (pad + btnW + pad) - (pad + barH * 2.0);
    const double scrubY = barY + barH * 0.5 - barH * 0.06;
    const double scrubH = std::max(0.6, barH * 0.12);

    if (scrubW > 5.0) {
        // Track background
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 60));
        ctx.fill_round_rect(BLRoundRect(scrubX, scrubY, scrubW, scrubH, scrubH * 0.5, scrubH * 0.5));

        // Filled portion (progress)
        if (durSec > 0.0f) {
            // Math: fillWidth = scrubW * (timeSec / durSec) ∈ [0, scrubW]
            double progress = std::max(0.0, std::min(1.0,
                                                     static_cast<double>(timeSec) / static_cast<double>(durSec)));
            double fillW = scrubW * progress;
            if (fillW > 0.1) {
                ctx.set_fill_style(BLRgba32(0xFF, 0x40, 0x40, 255));
                ctx.fill_round_rect(BLRoundRect(scrubX, scrubY, fillW, scrubH, scrubH * 0.5, scrubH * 0.5));
            }

            // Playhead circle
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 255));
            ctx.fill_circle(scrubX + fillW, scrubY + scrubH * 0.5, scrubH * 1.2);
        }
    }

    // ── Volume Icon ──────────────────────────────────────────────────────────
    if (sw > 15.0) {
        double volX  = sx + sw - pad - barH * 0.8;
        double volCy = barY + barH * 0.5;
        double vs    = barH * 0.18;

        if (isMuted) {
            ctx.set_stroke_style(BLRgba32(0xFF, 0xFF, 0xFF, 200));
            ctx.set_stroke_width(0.3);
            ctx.stroke_line(volX, volCy - vs, volX + vs * 1.5, volCy + vs);
            ctx.stroke_line(volX + vs * 1.5, volCy - vs, volX, volCy + vs);
        } else {
            BLPath spk;
            spk.move_to(volX, volCy - vs * 0.5);
            spk.line_to(volX + vs, volCy - vs * 0.5);
            spk.line_to(volX + vs * 1.8, volCy - vs);
            spk.line_to(volX + vs * 1.8, volCy + vs);
            spk.line_to(volX + vs, volCy + vs * 0.5);
            spk.line_to(volX, volCy + vs * 0.5);
            spk.close();
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 200));
            ctx.fill_path(spk);
        }
    }
}

// =============================================================================
// PRIVATE: Timecode Formatter
// =============================================================================

std::string VideoObject::FormatTimecode(float seconds) {
    if (seconds < 0.0f) seconds = 0.0f;
    int totalSec = static_cast<int>(seconds);
    int h  = totalSec / 3600;
    int m  = (totalSec % 3600) / 60;
    int s  = totalSec % 60;

    char buf[16];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }
    return buf;
}

// =============================================================================
// CONTEXT MENU & OBJECT ACTIONS
// =============================================================================

void VideoObject::CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) {
    // ── Action: Play / Pause toggle ───────────────────────────────────────
    {
        ContextMenuItem act;
        act.label = isPlaying ? "Pause" : "Play";
        act.icon  = isPlaying ? "⏸" : "▶";
        act.iconKey = isPlaying ? "pause" : "play";
        act.order = 100;
        act.isSeparatorBefore = true;
        act.onTrigger = [this]() {
            if (isPlaying) {
                Pause();
            } else {
                Play();
            }
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Stop ─────────────────────────────────────────────────────
    {
        ContextMenuItem act;
        act.label   = "Stop";
        act.icon    = "⏹";
        act.iconKey = "stop";
        act.order   = 110;
        act.onTrigger = [this]() {
            Stop();
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Mute / Unmute ────────────────────────────────────────────
    {
        ContextMenuItem act;
        act.label   = isMuted ? "Unmute" : "Mute";
        act.icon    = isMuted ? "🔊" : "🔇";
        act.iconKey = isMuted ? "unmute" : "mute";
        act.order   = 120;
        act.onTrigger = [this]() {
            ToggleMute();
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Loop toggle ──────────────────────────────────────────────
    {
        ContextMenuItem act;
        act.label   = isLooping ? "Disable Loop" : "Enable Loop";
        act.icon    = "🔁";
        act.iconKey = "loop";
        act.order   = 130;
        act.onTrigger = [this]() {
            isLooping = !isLooping;
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Reset to Native Size ─────────────────────────────────────
    {
        ContextMenuItem act;
        act.label   = "Reset to Native Size";
        act.icon    = "⤢";
        act.iconKey = "reset_size";
        act.order   = 140;
        act.isSeparatorBefore = true;
        act.onTrigger = [this]() {
            // 96 DPI default canvas baseline: 96 / 25.4 ≈ 3.7795 px/mm
            ResetToNativeSize(96.0 / 25.4);
            UpdateBounds();
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Open in Native Player (system default video player) ──────
    {
        ContextMenuItem act;
        act.label   = "Open in Native Player";
        act.icon    = "🎬";
        act.iconKey = "open_external";
        act.order   = 150;
        act.onTrigger = [this]() {
            if (!sourceUrl.empty()) {
                // SDL_OpenURL handles both file:// paths and http:// URLs
                SDL_OpenURL(sourceUrl.c_str());
            }
        };
        actions.push_back(std::move(act));
    }
}

// =============================================================================
// CLONE
// =============================================================================

std::unique_ptr<CanvasObject> VideoObject::Clone() const {
    auto copy = std::make_unique<VideoObject>(sourceUrl, displayName, worldWidth, worldHeight);
    copy->worldX       = worldX;
    copy->worldY       = worldY;
    copy->aspectRatio  = aspectRatio;
    copy->nativeVideoW = nativeVideoW;
    copy->nativeVideoH = nativeVideoH;
    copy->isVisible    = isVisible;
    copy->opacity      = opacity;
    copy->zOrder       = zOrder;
    copy->isSelectable = isSelectable;
    copy->isLooping    = isLooping;
    copy->isMuted      = isMuted;
    copy->volume       = volume;
    copy->playbackRate = playbackRate;
    copy->transform    = transform;
    copy->thumbnailPath = thumbnailPath;
    // player is NOT copied — clone starts idle; caller must call Play() if needed
    copy->UpdateBounds();
    return copy;
}

} // namespace Folio
