#pragma once
/**
 * @file video_container.hpp
 * @brief Canvas object representing a linked video file or YouTube URL.
 *
 * VideoObject is a resizable canvas element (unlike audio) because video has
 * a natural aspect ratio. It renders as a thumbnail placeholder with a play
 * button overlay until Phase 2 embeds a real media player.
 *
 * File Storage:
 *   Local files are stored in the notebook sidecar folder:
 *     <notebook_dir>/imports/video/<displayName>
 *   YouTube links store only the URL string (no download).
 *
 * Source Types (auto-detected by IsYouTube()):
 *   - Local file: filePath points to MP4 / MOV / AVI / MKV etc.
 *   - YouTube: sourceUrl starts with "https://www.youtube.com/..." or "https://youtu.be/..."
 *              Future: embed YouTube IFrame player via a CEF/WebView2 viewport.
 *
 * Interaction:
 *   - Resizable via standard 8-point bounding box gizmo (corner lock = aspect ratio).
 *   - Moveable via body-drag.
 *   - Double-click (Phase 2): triggers Play() / launches embedded player.
 *
 * Phase 2 Integration Points:
 *   - Local: libVLC embedded player (vlc.h), or SDL_mixer for simple formats.
 *   - YouTube: Windows WebView2 or CEF (Chromium Embedded Framework) IFrame.
 *   - Both modes update thumbnailPath via ExtractThumbnail() called at import time.
 *
 * Scalability:
 *   VideoObject does not know HOW to play — that is the responsibility of
 *   a future VideoPlayer class in core/media/. This object only owns the data
 *   and the canvas rendering.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <vector>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @brief Resizable video player canvas object — file link or YouTube URL.
 */
class VideoObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    std::string sourceUrl;       ///< File path OR "https://youtube.com/watch?v=..." etc.
    std::string displayName;     ///< Shown as overlay label (filename or video title)
    std::string thumbnailPath;   ///< Path to cached thumbnail image (empty = no thumb yet)

    double worldX      = 0.0;    ///< Top-left X (world mm)
    double worldY      = 0.0;    ///< Top-left Y (world mm)
    double worldWidth  = 80.0;   ///< Width (world mm) — resizable, default 16:9 proportion
    double worldHeight = 45.0;   ///< Height (world mm) — resizable

    double aspectRatio = 16.0 / 9.0; ///< Cached aspect ratio; used for corner-lock resize

    bool isPlaying = false;          ///< Playback state (stub)

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    VideoObject() {
        type = ObjectType::Video;
        UpdateBounds();
    }

    VideoObject(const std::string& url, const std::string& name,
                double w = 80.0, double h = 45.0)
        : sourceUrl(url), displayName(name), worldWidth(w), worldHeight(h)
    {
        type = ObjectType::Video;
        aspectRatio = (h > 0.0) ? (w / h) : (16.0 / 9.0);
        UpdateBounds();
    }

    // =========================================================================
    // SOURCE TYPE HELPERS
    // =========================================================================

    /**
     * @brief Returns true if sourceUrl is a YouTube link.
     *
     * Detected patterns:
     *   "https://www.youtube.com/..."
     *   "https://youtube.com/..."
     *   "https://youtu.be/..."
     */
    [[nodiscard]] bool IsYouTube() const noexcept {
        return sourceUrl.find("youtube.com") != std::string::npos ||
               sourceUrl.find("youtu.be")    != std::string::npos;
    }

    /**
     * @brief Extracts the YouTube video ID from a URL.
     *
     * Handles:
     *   https://www.youtube.com/watch?v=<ID>
     *   https://youtu.be/<ID>
     *
     * Returns empty string if not a valid YouTube URL.
     */
    [[nodiscard]] std::string GetYouTubeId() const {
        // youtu.be/<ID>
        auto pos = sourceUrl.find("youtu.be/");
        if (pos != std::string::npos) {
            auto id = sourceUrl.substr(pos + 9);
            auto end = id.find_first_of("?&#");
            return (end != std::string::npos) ? id.substr(0, end) : id;
        }
        // youtube.com/watch?v=<ID>
        pos = sourceUrl.find("v=");
        if (pos != std::string::npos) {
            auto id = sourceUrl.substr(pos + 2);
            auto end = id.find_first_of("&");
            return (end != std::string::npos) ? id.substr(0, end) : id;
        }
        return "";
    }

    // =========================================================================
    // PLAYBACK (STUB — Phase 2)
    // =========================================================================

    /**
     * @brief Starts video playback.
     *
     * STUB: For local files, integrate libVLC or SDL2_video.
     * For YouTube links, launch a WebView2 / CEF IFrame embedded in the canvas.
     *
     * TODO: implement in core/media/video_player.hpp
     */
    void Play() {
        isPlaying = true;
        // TODO: launch embedded player
    }

    void Stop() {
        isPlaying = false;
    }

    // =========================================================================
    // BOUNDS & SPATIAL
    // =========================================================================

    void UpdateBounds() override {
        BLPoint p[4] = {
            transform.map_point(worldX,              worldY),
            transform.map_point(worldX + worldWidth, worldY),
            transform.map_point(worldX + worldWidth, worldY + worldHeight),
            transform.map_point(worldX,              worldY + worldHeight)
        };
        double minX = p[0].x, maxX = p[0].x;
        double minY = p[0].y, maxY = p[0].y;
        for (int i = 1; i < 4; ++i) {
            minX = (std::min)(minX, p[i].x);
            maxX = (std::max)(maxX, p[i].x);
            minY = (std::min)(minY, p[i].y);
            maxY = (std::max)(maxY, p[i].y);
        }
        bounds = AABB(minX, minY, maxX, maxY);
    }

    bool HitTest(double wx, double wy) const override {
        return bounds.Contains(wx, wy);
    }

    bool Intersects(const AABB& sel) const override {
        return bounds.Intersects(sel);
    }

    // =========================================================================
    // TRANSFORM — full resize + translate
    // =========================================================================

    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    /**
     * @brief Bakes axis-aligned scale + translation into worldX/Y/W/H.
     *
     * After baking, aspectRatio is updated from the new dimensions so
     * the next corner-lock resize uses the correct ratio.
     */
    void BakeTransform() override {
        if (std::abs(transform.m01) < 1e-6 && std::abs(transform.m10) < 1e-6) {
            if (transform.m00 == 1.0 && transform.m11 == 1.0 &&
                transform.m20 == 0.0 && transform.m21 == 0.0) return;

            double p0x = transform.m00 * worldX + transform.m20;
            double p0y = transform.m11 * worldY + transform.m21;
            double p1x = transform.m00 * (worldX + worldWidth)  + transform.m20;
            double p1y = transform.m11 * (worldY + worldHeight) + transform.m21;

            worldX      = (std::min)(p0x, p1x);
            worldY      = (std::min)(p0y, p1y);
            worldWidth  = (std::max)(0.5, std::abs(p1x - p0x));
            worldHeight = (std::max)(0.5, std::abs(p1y - p0y));

            if (worldHeight > 0.0) aspectRatio = worldWidth / worldHeight;
            transform = BLMatrix2D::make_identity();
            UpdateBounds();
        }
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders a video thumbnail placeholder with play button overlay.
     *
     * When thumbnailPath is set (Phase 2): blit the cached thumbnail BLImage.
     * Until then: solid dark grey rectangle with centered play triangle.
     *
     * YouTube videos show a "YT" badge in the top-right corner.
     */
    void Render(BLContext& ctx, const Viewport& /*viewport*/) const override {
        if (!isVisible) return;

        ctx.save();
        ctx.apply_transform(transform);

        const double x = worldX, y = worldY, w = worldWidth, h = worldHeight;
        const double r = 2.0; // corner radius (mm)

        // Background placeholder
        ctx.set_fill_style(BLRgba32(0x12, 0x14, 0x1A, static_cast<uint8_t>(opacity * 240)));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

        // TODO (Phase 2): blit thumbnailPath BLImage here when available

        // Play button triangle (centered in the frame)
        const double cx     = x + w * 0.5;
        const double cy     = y + h * 0.5;
        const double tSize  = (std::min)(w, h) * 0.18;

        if (!isPlaying) {
            // Soft glow circle behind the play button
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 30));
            ctx.fill_circle(cx, cy, tSize * 1.4);

            // Play triangle (pointing right)
            BLPath tri;
            tri.move_to(cx - tSize * 0.4, cy - tSize);
            tri.line_to(cx + tSize,       cy);
            tri.line_to(cx - tSize * 0.4, cy + tSize);
            tri.close();
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, static_cast<uint8_t>(opacity * 220)));
            ctx.fill_path(tri);
        } else {
            // Pause indicator: two vertical bars
            double bw = tSize * 0.3, bh = tSize * 1.2;
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 200));
            ctx.fill_rect(cx - bw * 1.4, cy - bh * 0.5, bw, bh);
            ctx.fill_rect(cx + bw * 0.4, cy - bh * 0.5, bw, bh);
        }

        // YouTube badge (top-right corner)
        if (IsYouTube()) {
            ctx.set_fill_style(BLRgba32(0xFF, 0x00, 0x00, 220));
            ctx.fill_round_rect(BLRoundRect(x + w - 9.0, y + 1.5, 7.5, 3.5, 1.0, 1.0));
            // White "YT" text — rendered via font in Phase 2
        }

        // Border
        ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 180));
        ctx.set_stroke_width(0.4);
        ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

        ctx.restore();
    }

    // =========================================================================
    // DUPLICATION & PERSISTENCE
    // =========================================================================

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<VideoObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

} // namespace Folio
