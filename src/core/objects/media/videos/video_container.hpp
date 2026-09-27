#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/videos/video_container.hpp
 * @brief Canvas Object Representing a Video Player — Local Files and Network Streams
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTERACTION MODEL:
 * -----------------------------------------
 * VideoObject is a first-class canvas citizen that owns a VideoPlayerInstance (the libVLC
 * decode thread engine) and blits decoded BGRA32 video frames directly into the Blend2D
 * compositeSurface each frame.
 *
 * Key Design Decisions:
 *   1. Aspect Ratio ALWAYS Locked:
 *      Unlike ImageObject (which allows edge-handle squishing), VideoObject ALWAYS preserves
 *      its aspect ratio — both corner and edge handles resize proportionally. The ratio
 *      is derived from the actual decoded frame dimensions on first decode.
 *
 *   2. Dual Source Support:
 *      - Local physical files: MP4, MKV, WebM, MOV, AVI — copied into imports/videos/
 *      - Network streams: HTTP/HTTPS, HLS (.m3u8), RTSP — passed directly to libVLC
 *      - YouTube links: Detected by URL pattern; poster thumbnail shown, click-to-play
 *        via system browser or native player fallback
 *
 *   3. 3rd-Layer Decode Thread:
 *      VideoPlayerInstance decodes frames on its own background thread (libVLC's internal
 *      thread pool). The main render thread picks up new frames via HasNewFrame() + LockFrame().
 *
 *   4. Transport Controls Overlay:
 *      On-canvas scrubber bar (Play/Pause, timecode, seek slider, volume) rendered by
 *      Blend2D in screen-space on the compositeSurface during VideoObject::Render().
 *
 * Memory Model:
 *   pixelBuffer: videoW * videoH * 4 bytes (BGRA32), owned by VideoPlayerInstance
 *   player:      unique_ptr<VideoPlayerInstance> — created lazily on first Play()
 *   thumbnailImg: BLImage — optional poster frame (YouTube or first-frame snapshot)
 *
 * Input/Output of Render():
 *   @input  ctx        Blend2D rendering context (screen-space coordinates)
 *   @input  viewport   Current camera viewport (for world→screen projection)
 *   @output            Blits decoded video frame or player-card placeholder into ctx
 */

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <vector>
#include <atomic>
#include <cstdint>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/objects/media/videos/video_player_instance.hpp"

namespace Folio {

/**
 * @class VideoObject
 * @brief Resizable video canvas container with libVLC playback, transport controls,
 *        and YouTube poster thumbnail support.
 */
class VideoObject : public CanvasObject {
public:
    // =========================================================================
    // SOURCE & IDENTITY
    // =========================================================================

    std::string sourceUrl;      ///< Local filesystem path or network stream URL
    std::string displayName;    ///< Label shown in tooltip/context menu (filename or video title)
    std::string thumbnailPath;  ///< Optional filesystem path to cached poster thumbnail (PNG/JPG)

    // =========================================================================
    // VIDEO GEOMETRY (set after first frame decoded or from file metadata)
    // =========================================================================

    /// Aspect ratio of the video content (width / height). ALWAYS preserved during resize.
    /// Derived from decoded frame dimensions on first decode; defaults to 16:9 (1.777...).
    double aspectRatio = 16.0 / 9.0;

    /// Native decoded pixel dimensions of the video stream.
    /// Used to correctly report "native size" in the context menu.
    int nativeVideoW = 1920;
    int nativeVideoH = 1080;

    // =========================================================================
    // PLAYBACK STATE (synchronized with VideoPlayerInstance)
    // =========================================================================

    bool isPlaying = false;  ///< Tracks last requested play/pause command
    bool isLooping = false;  ///< Loop playback when end is reached
    bool isMuted   = false;  ///< Audio mute state
    float volume   = 1.0f;  ///< Volume level [0.0 .. 2.0] (1.0 = 100%)
    float playbackRate = 1.0f; ///< Speed multiplier (0.5 = half, 2.0 = double)

    // =========================================================================
    // TRANSPORT UI STATE (used by Render() to draw on-canvas controls)
    // =========================================================================

    /// Whether the transport controls bar is currently visible (fades on inactivity)
    mutable bool controlsVisible = true;
    /// Timestamp (SDL_GetTicks ms) of last mouse hover over the video rectangle
    mutable uint64_t lastHoverMs = 0;
    /// Duration in milliseconds before transport controls fade out on inactivity
    static constexpr uint64_t CONTROLS_FADE_DELAY_MS = 3000;

    // =========================================================================
    // THUMBNAIL (YouTube / first-frame poster)
    // =========================================================================

    /// Optional Blend2D poster image (YouTube thumbnail or first-frame snapshot)
    BLImage thumbnailImg;
    bool thumbnailLoaded = false;
    std::atomic<bool> isFetchingThumbnail{false};

    // =========================================================================
    // STREAM RESOLUTION (YouTube / online video stream resolution via yt-dlp)
    // =========================================================================

    /// Cached direct playable stream URL resolved from YouTube / web links
    std::string resolvedStreamUrl;
    /// Flag indicating background stream resolution is currently running
    std::atomic<bool> isResolvingStream{false};

    // =========================================================================
    // PLAYER ENGINE (created lazily on first Play())
    // =========================================================================

    /// Owned libVLC player engine. nullptr = not yet loaded / never played.
    std::unique_ptr<VideoPlayerInstance> player;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor: 16:9 standard canvas size (80mm × 45mm).
     */
    VideoObject();

    /**
     * @brief Parameterized constructor.
     *
     * @param[in] url   Source file path or stream URL.
     * @param[in] name  Descriptive display label (filename or video title).
     * @param[in] w     Initial width in world millimeters (default: 80mm = ~3.15 in).
     * @param[in] h     Initial height in world millimeters (default: 45mm for 16:9).
     */
    VideoObject(const std::string& url, const std::string& name,
                double w = 80.0, double h = 45.0);

    // =========================================================================
    // SOURCE TYPE HELPERS
    // =========================================================================

    /**
     * @brief Returns true if sourceUrl contains a recognized YouTube domain.
     * @return true if URL contains "youtube.com" or "youtu.be".
     */
    [[nodiscard]] bool IsYouTube() const noexcept;

    /**
     * @brief Returns true if sourceUrl is an HLS stream (.m3u8) or other network protocol.
     */
    [[nodiscard]] bool IsNetworkStream() const noexcept;

    /**
     * @brief Extracts the YouTube video ID from the source URL.
     *
     * Supported formats:
     *   - https://youtu.be/<ID>
     *   - https://youtube.com/watch?v=<ID>
     *   - https://youtube.com/embed/<ID>
     *
     * @return YouTube video ID string (e.g. "dQw4w9WgXcQ"), or empty if not YouTube.
     */
    [[nodiscard]] std::string GetYouTubeId() const;

    /**
     * @brief Builds the YouTube thumbnail URL for the given quality tier.
     *
     * @param quality  "maxresdefault" | "hqdefault" | "mqdefault" | "sddefault"
     * @return Full CDN URL: https://img.youtube.com/vi/<ID>/<quality>.jpg
     */
    [[nodiscard]] std::string GetYouTubeThumbnailUrl(const std::string& quality = "hqdefault") const;

    /**
     * @brief Asynchronously fetches and decodes the high-resolution YouTube poster thumbnail.
     *
     * Working Process:
     *   1. Extracts the YouTube video ID from `sourceUrl`.
     *   2. Checks local disk cache (`%TEMP%/FolioNote/yt_thumbs/<id>.jpg`).
     *   3. If not cached, downloads thumbnail from `https://img.youtube.com/vi/<id>/hqdefault.jpg`.
     *   4. Decodes image using `ImageDecoder::DecodeFromFile` into `thumbnailImg`.
     *   5. Sets `thumbnailLoaded = true` and triggers `onDirty()`.
     *
     * @param[in] onDirty Optional callback to trigger canvas repaint upon download completion.
     */
    void FetchYouTubeThumbnailAsync(std::function<void()> onDirty = nullptr);

    /**
     * @brief Resolves YouTube stream URL into a direct playable stream via yt-dlp.
     *
     * Working Process:
     *   1. Spawns asynchronous worker thread to query `python -m yt_dlp` or `yt-dlp`.
     *   2. If a direct `.googlevideo.com` stream URL is retrieved, caches it in `resolvedStreamUrl`
     *      and launches playback via `player->Open()`.
     *   3. If yt-dlp is unavailable or fails, gracefully falls back to opening the video in the
     *      default system web browser (`FileManager::OpenWithDefaultApp()`).
     *
     * @param[in] onDirty Optional callback to trigger canvas repaint.
     */
    void ResolveYouTubeStreamAsync(std::function<void()> onDirty = nullptr);

    // =========================================================================
    // PLAYBACK CONTROLS
    // =========================================================================

    /**
     * @brief Loads the media source into the VideoPlayerInstance and starts playback.
     *
     * Working Process:
     *   1. If player == nullptr, creates a new VideoPlayerInstance.
     *   2. Wires the onFrameReady callback to set isDirty via the provided delegate.
     *   3. Calls player->Open(sourceUrl).
     *   4. Sets isPlaying = true.
     *
     * @param[in] onDirty  Callable that marks the canvas compositor as dirty.
     *                     Typically: [&canvas]{ canvas.isDirty = true; }
     */
    void Play(std::function<void()> onDirty = nullptr);

    /**
     * @brief Pauses active playback; last decoded frame stays frozen in the pixel buffer.
     */
    void Pause();

    /**
     * @brief Stops playback completely and releases decoder resources.
     */
    void Stop();

    /**
     * @brief Seeks to the given position in seconds.
     * @param[in] positionSec Target playback time [seconds].
     */
    void SeekTo(float positionSec);

    /**
     * @brief Sets audio volume.
     * @param[in] vol  Volume level [0.0 .. 2.0]. 1.0 = original, 0.0 = silent.
     */
    void SetVolume(float vol);

    /**
     * @brief Toggles between Play and Pause.
     * @param[in] onDirty Callback to invalidate canvas.
     */
    void TogglePlay(std::function<void()> onDirty = nullptr);

    /**
     * @brief Handles user click interaction on the video canvas container.
     *
     * Mathematical Process & Interaction Rules:
     *   1. If the video is currently paused/stopped:
     *      Any click inside the video bounds triggers Play(), starting playback immediately.
     *   2. If the video is currently playing:
     *      - Double-click anywhere on the video toggles Play/Pause.
     *      - Click in the bottom transport bar region (within barH of bottom):
     *        * Left button region: toggles Play/Pause.
     *        * Center scrubber track: seeks to normalized progress = (clickX - scrubX) / scrubW.
     *        * Right volume icon region: toggles mute.
     *      - Click on the center play/pause triangle area: toggles Play/Pause.
     *   3. Always notifies onDirty callback to trigger canvas rebake.
     *
     * @param worldX World X coordinate of click in millimeters.
     * @param worldY World Y coordinate of click in millimeters.
     * @param isDoubleClick Whether the mouse click was a double click.
     * @param onDirty Callback to invalidate canvas and schedule rebake.
     * @return True if the click was handled by video transport (play/pause/seek/mute).
     */
    bool HandleCanvasClick(double worldXQuery, double worldYQuery, bool isDoubleClick,
                           std::function<void()> onDirty);

    /** @brief Toggles audio mute. */
    void ToggleMute();

    /**
     * @brief Resets zoom to natural video size (1:1 pixel mapping at current DPI).
     *
     * Mathematical note:
     *   naturalWidthMm  = nativeVideoW / pixelsPerMm
     *   naturalHeightMm = nativeVideoH / pixelsPerMm
     */
    void ResetToNativeSize(double pixelsPerMm = 3.7795);

    // =========================================================================
    // CANVAS INTERACTION OVERRIDES
    // =========================================================================

    /**
     * @brief Bakes accumulated transform into worldX/Y/W/H and recalculates aspectRatio.
     *
     * Important: aspectRatio is ALWAYS re-enforced after baking. If the user resized
     * using an edge handle (which would produce a non-locked ratio in images), we correct
     * the height to maintain: worldHeight = worldWidth / aspectRatio.
     */
    void BakeTransform() override;

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the video frame (or placeholder card) plus transport controls.
     *
     * Render Pass Order:
     *   1. [Frame]     If player has a new decoded frame: LockFrame → BLImage → blit_image
     *   2. [Thumbnail] If no frame yet: Draw poster thumbnail or player card placeholder
     *   3. [Controls]  If controlsVisible: Draw scrubber bar, play/pause, timecode, volume
     *   4. [Outline]   Draw 0.4mm rounded border
     *   5. [Badge]     YouTube badge (red pill) if IsYouTube()
     *
     * @param[in,out] ctx      Blend2D rendering context (screen-space coordinates).
     * @param[in]     viewport Current camera viewport.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // CLONING
    // =========================================================================

    /**
     * @brief Clones this VideoObject as an independent copy.
     *
     * Note: The VideoPlayerInstance (player) is NOT cloned; the new copy
     *       starts with player == nullptr and must call Play() independently.
     *
     * @return Unique pointer to a new VideoObject with same URL, dimensions, and settings.
     */
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;

private:
    // =========================================================================
    // INTERNAL HELPERS
    // =========================================================================

    /**
     * @brief Renders the dark "idle" player card when no decoded frame is available.
     *        Shows the play triangle, YouTube badge, and display name.
     *
     * @param[in,out] ctx      Blend2D rendering context (screen-space).
     * @param[in]     sx,sy    Screen-space top-left corner of the video box.
     * @param[in]     sw,sh    Screen-space width and height of the video box.
     */
    void RenderPlaceholderCard(BLContext& ctx, double sx, double sy,
                               double sw, double sh) const;

    /**
     * @brief Renders the on-canvas transport control bar (scrubber, timecode, volume).
     *
     * @param[in,out] ctx      Blend2D rendering context (screen-space).
     * @param[in]     sx,sy    Screen-space top-left of video box.
     * @param[in]     sw,sh    Screen-space size of video box.
     * @param[in]     timeSec  Current playback position [seconds].
     * @param[in]     durSec   Total duration [seconds].
     */
    void RenderTransportBar(BLContext& ctx, double sx, double sy, double sw, double sh,
                            float timeSec, float durSec) const;

    /**
     * @brief Formats seconds to "mm:ss" or "h:mm:ss" string.
     *
     * @param[in] seconds  Playback time in seconds.
     * @return Formatted time string, e.g. "1:23:45" or "05:30".
     */
    static std::string FormatTimecode(float seconds);
};

/// Canonical type alias for backwards compatibility
using VideoContainer = VideoObject;

} // namespace Folio
