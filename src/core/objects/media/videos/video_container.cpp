/**
 * =========================================================================================
 * @file core/objects/media/videos/video_container.cpp
 * @brief Implementation of VideoObject Methods and Player Card Rendering
 * =========================================================================================
 */

#include "core/objects/media/videos/video_container.hpp"
#include "core/spatial/aabb_utils.hpp"

namespace Folio {

// =============================================================================
// CONSTRUCTORS
// =============================================================================

VideoObject::VideoObject() {
    type = ObjectType::Video;
    worldWidth = 80.0;
    worldHeight = 45.0;
    aspectRatio = 16.0 / 9.0;
    UpdateBounds();
}

VideoObject::VideoObject(const std::string& url, const std::string& name, double w, double h)
    : sourceUrl(url), displayName(name)
{
    type = ObjectType::Video;
    worldWidth = w;
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

std::string VideoObject::GetYouTubeId() const {
    // 1. Check youtu.be/<ID> format
    auto pos = sourceUrl.find("youtu.be/");
    if (pos != std::string::npos) {
        auto id = sourceUrl.substr(pos + 9);
        auto end = id.find_first_of("?&#");
        return (end != std::string::npos) ? id.substr(0, end) : id;
    }

    // 2. Check youtube.com/watch?v=<ID> format
    pos = sourceUrl.find("v=");
    if (pos != std::string::npos) {
        auto id = sourceUrl.substr(pos + 2);
        auto end = id.find_first_of("&");
        return (end != std::string::npos) ? id.substr(0, end) : id;
    }

    return "";
}

// =============================================================================
// PLAYBACK CONTROLS
// =============================================================================

void VideoObject::Play() {
    isPlaying = true;
}

void VideoObject::Stop() {
    isPlaying = false;
}

// =============================================================================
// TRANSFORM & BAKING
// =============================================================================

void VideoObject::BakeTransform() {
    if (Folio::AABBUtils::BakeTransformedRect(worldX, worldY, worldWidth, worldHeight, transform, 0.5)) {
        if (worldHeight > 0.0) {
            aspectRatio = worldWidth / worldHeight;
        }
        UpdateBounds();
    }
}

// =============================================================================
// RENDERING
// =============================================================================

void VideoObject::Render(BLContext& ctx, const Viewport& /*viewport*/) const {
    if (!isVisible) return;

    ctx.save();
    ctx.apply_transform(transform);

    const double x = worldX;
    const double y = worldY;
    const double w = worldWidth;
    const double h = worldHeight;
    const double r = 2.0; // corner radius in mm

    // 1. Draw main player card background
    ctx.set_fill_style(BLRgba32(0x12, 0x14, 0x1A, static_cast<uint8_t>(opacity * 240)));
    ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

    // 2. Compute center point and play button geometry
    const double cx = x + w * 0.5;
    const double cy = y + h * 0.5;
    const double tSize = (std::min)(w, h) * 0.18;

    if (!isPlaying) {
        // Subtle ambient glow behind play button
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 30));
        ctx.fill_circle(cx, cy, tSize * 1.4);

        // Crisp white play triangle
        BLPath tri;
        tri.move_to(cx - tSize * 0.4, cy - tSize);
        tri.line_to(cx + tSize, cy);
        tri.line_to(cx - tSize * 0.4, cy + tSize);
        tri.close();
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, static_cast<uint8_t>(opacity * 220)));
        ctx.fill_path(tri);
    } else {
        // Pause icon: two clean vertical bars
        const double bw = tSize * 0.3;
        const double bh = tSize * 1.2;
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 200));
        ctx.fill_rect(cx - bw * 1.4, cy - bh * 0.5, bw, bh);
        ctx.fill_rect(cx + bw * 0.4, cy - bh * 0.5, bw, bh);
    }

    // 3. YouTube brand indicator badge in top-right corner
    if (IsYouTube()) {
        ctx.set_fill_style(BLRgba32(0xFF, 0x00, 0x00, 220));
        ctx.fill_round_rect(BLRoundRect(x + w - 9.0, y + 1.5, 7.5, 3.5, 1.0, 1.0));
    }

    // 4. Subtle outline border
    ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 180));
    ctx.set_stroke_width(0.4);
    ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

    ctx.restore();
}

std::unique_ptr<CanvasObject> VideoObject::Clone() const {
    return std::make_unique<VideoObject>(*this);
}

} // namespace Folio
