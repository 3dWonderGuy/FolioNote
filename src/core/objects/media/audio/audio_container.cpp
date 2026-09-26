/**
 * =========================================================================================
 * @file core/objects/media/audio/audio_container.cpp
 * @brief Implementation of AudioObject Methods and Chip Rendering Pipeline
 * =========================================================================================
 */

#include "core/objects/media/audio/audio_container.hpp"

namespace Folio {

// =============================================================================
// CONSTRUCTORS
// =============================================================================

AudioObject::AudioObject() {
    type = ObjectType::Audio;
    worldWidth = chipW;
    worldHeight = chipH;
    UpdateBounds();
}

AudioObject::AudioObject(const std::string& path, const std::string& name, double duration)
    : filePath(path), displayName(name), durationSeconds(duration)
{
    type = ObjectType::Audio;
    worldWidth = chipW;
    worldHeight = chipH;
    UpdateBounds();
}

// =============================================================================
// PLAYBACK CONTROLS
// =============================================================================

void AudioObject::Play() {
    isPlaying = true;
}

void AudioObject::Stop() {
    isPlaying = false;
    currentPositionSeconds = 0.0;
}

// =============================================================================
// TRANSFORM & GIZMO INVARIANTS
// =============================================================================

void AudioObject::ApplyTransform(const BLMatrix2D& matrix) {
    const double newX = (worldX * matrix.m00) + (worldY * matrix.m10) + matrix.m20;
    const double newY = (worldX * matrix.m01) + (worldY * matrix.m11) + matrix.m21;

    const double dx = newX - worldX;
    const double dy = newY - worldY;

    BLMatrix2D translationOnly = BLMatrix2D::make_translation(dx, dy);
    transform.post_transform(translationOnly);
    UpdateBounds();
}

void AudioObject::BakeTransform() {
    worldX += transform.m20;
    worldY += transform.m21;
    transform = BLMatrix2D::make_identity();
    UpdateBounds();
}

GizmoStyle AudioObject::GetGizmoStyle() const noexcept {
    return GizmoStyle::MoveOnly;
}

// =============================================================================
// RENDERING
// =============================================================================

void AudioObject::Render(BLContext& ctx, const Viewport& /*viewport*/) const {
    if (!isVisible) return;

    ctx.save();
    ctx.apply_transform(transform);

    const double x = worldX;
    const double y = worldY;
    const double w = chipW;
    const double h = chipH;
    const double r = 2.0; // corner radius in mm

    // 1. Draw chip card body
    ctx.set_fill_style(BLRgba32(0x1A, 0x1C, 0x26, static_cast<uint8_t>(opacity * 235)));
    ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

    // 2. Purple brand accent band on the left edge
    ctx.set_fill_style(BLRgba32(0x5C, 0x2D, 0x91, 220));
    ctx.fill_round_rect(BLRoundRect(x, y, 7.0 + r, h, r, r));

    // 3. Stylized waveform indicator bars
    const double barX0  = x + 10.0;
    const double barW   = 1.2;
    const double barGap = 2.2;
    const float  heights[] = { 0.35f, 0.65f, 1.0f, 0.55f, 0.40f };
    ctx.set_fill_style(BLRgba32(0x9B, 0x59, 0xB6, 200));
    for (int i = 0; i < 5; ++i) {
        const double bh = h * 0.55 * heights[i];
        const double by = y + (h - bh) * 0.5;
        ctx.fill_round_rect(BLRoundRect(barX0 + i * (barW + barGap), by, barW, bh, 0.5, 0.5));
    }

    // 4. Playback state indicator dot
    if (isPlaying) {
        ctx.set_fill_style(BLRgba32(0x00, 0xD2, 0x6A, 220)); // Active green
    } else {
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 160)); // Soft white
    }
    ctx.fill_circle(x + 4.5, y + h * 0.5, 1.5);

    // 5. Border outline
    ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 180));
    ctx.set_stroke_width(0.4);
    ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

    ctx.restore();
}

std::unique_ptr<CanvasObject> AudioObject::Clone() const {
    return std::make_unique<AudioObject>(*this);
}

} // namespace Folio
