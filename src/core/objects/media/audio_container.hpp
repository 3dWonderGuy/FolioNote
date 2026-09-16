#pragma once
/**
 * @file audio_container.hpp
 * @brief Canvas object representing a linked audio file — non-resizable icon chip.
 *
 * AudioObject is a fixed-size, move-only canvas element that links to an
 * external audio file (MP3, WAV, FLAC, OGG, etc.). It renders as a
 * waveform-icon chip with a play button and filename label.
 *
 * File Storage:
 *   Audio files are stored in the notebook's companion sidecar folder:
 *     <notebook_dir>/imports/audio/<displayName>
 *   They are NEVER embedded in the .folio binary.
 *
 * Future Playback (Phase 2):
 *   The Play() method is a documented stub. When the audio playback feature
 *   is implemented, it will integrate with SDL_mixer or miniaudio. The
 *   architecture is prepared: the method exists, the file path is stored,
 *   and duration is tracked so a playback progress bar can be drawn.
 *
 *   The long-term vision is synchronized playback with ink stroke replay:
 *   as the recorded audio plays, the canvas animates the ink strokes drawn
 *   at that timestamp — allowing students to re-experience lecture note-taking.
 *   This object is the first anchor point for that future feature.
 *
 * Interaction:
 *   - Non-resizable: chip has a fixed size in world mm.
 *   - Moveable: body-drag via standard gizmo body-move.
 *   - Double-click (Phase 2): triggers Play().
 *
 * Scalability:
 *   To upgrade from stub to real playback, implement Play(), Stop(), Seek()
 *   in a new audio_player.hpp in core/media/ and call them from here.
 *   No other files need modification.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <vector>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @brief Fixed-size audio file link chip on the canvas.
 *
 * Chip size: chipW × chipH world mm (not user-resizable).
 * Position: (worldX, worldY) top-left in world mm.
 */
class AudioObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    std::string filePath;           ///< Absolute or sidecar-relative path to audio file
    std::string displayName;        ///< Shown on chip label
    double      durationSeconds = 0.0; ///< Duration hint (0 = unknown)

    double worldX = 0.0;            ///< Chip position X (world mm, top-left)
    double worldY = 0.0;            ///< Chip position Y (world mm, top-left)

    /// Fixed chip dimensions (world mm) — not user-resizable
    static constexpr double chipW = 60.0;
    static constexpr double chipH = 18.0;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    AudioObject() {
        type = ObjectType::Audio;
        UpdateBounds();
    }

    AudioObject(const std::string& path, const std::string& name, double duration = 0.0)
        : filePath(path), displayName(name), durationSeconds(duration)
    {
        type = ObjectType::Audio;
        UpdateBounds();
    }

    // =========================================================================
    // PLAYBACK (STUB — Phase 2)
    // =========================================================================

    /**
     * @brief Plays the linked audio file.
     *
     * STUB: No implementation yet. When the audio subsystem (SDL_mixer or
     * miniaudio) is integrated in Phase 2, this method will:
     *  1. Load the file via the audio player singleton
     *  2. Start playback from currentPositionSeconds
     *  3. Update isPlaying = true
     *
     * TODO: implement in core/media/audio_player.hpp
     */
    void Play() {
        // TODO: integrate audio player (SDL_mixer / miniaudio)
        isPlaying = true;
    }

    /**
     * @brief Stops audio playback.
     * STUB — Phase 2.
     */
    void Stop() {
        isPlaying = false;
        currentPositionSeconds = 0.0;
    }

    bool isPlaying = false;                 ///< Playback state (stub)
    double currentPositionSeconds = 0.0;    ///< Playback position (stub)

    // =========================================================================
    // BOUNDS & SPATIAL
    // =========================================================================

    void UpdateBounds() override {
        BLPoint p[4] = {
            transform.map_point(worldX,         worldY),
            transform.map_point(worldX + chipW, worldY),
            transform.map_point(worldX + chipW, worldY + chipH),
            transform.map_point(worldX,         worldY + chipH)
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
    // TRANSFORM — translation only
    // =========================================================================

    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    void BakeTransform() override {
        worldX += transform.m20;
        worldY += transform.m21;
        transform = BLMatrix2D::make_identity();
        UpdateBounds();
    }

    // =========================================================================
    // GIZMO — no resize handles
    // =========================================================================

    bool GetCustomGizmoHandles(std::vector<GizmoHandle>& /*outHandles*/,
                                const CanvasTransform& /*transform*/) const override {
        return false; // body-move only
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Draws the audio chip on the Blend2D canvas.
     *
     * Visual structure (world mm):
     *   ┌─[purple band]────────────────────────────┐
     *   │  [▶]  displayName         [0:00 / 0:00]  │
     *   └──────────────────────────────────────────┘
     *
     * The waveform visualization and play button are placeholder geometry.
     * Phase 2 will replace them with actual waveform data from the audio file.
     */
    void Render(BLContext& ctx, const Viewport& /*viewport*/) const override {
        if (!isVisible) return;

        ctx.save();
        ctx.apply_transform(transform);

        const double x = worldX, y = worldY, w = chipW, h = chipH;
        const double r = 2.0;   // corner radius (mm)

        // Background
        ctx.set_fill_style(BLRgba32(0x1A, 0x1C, 0x26, static_cast<uint8_t>(opacity * 235)));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

        // Purple accent band (left side — audio identity color)
        ctx.set_fill_style(BLRgba32(0x5C, 0x2D, 0x91, 220));
        ctx.fill_round_rect(BLRoundRect(x, y, 7.0 + r, h, r, r));

        // Placeholder waveform bars (5 vertical bars of varying height)
        const double barX0  = x + 10.0;
        const double barW   = 1.2;
        const double barGap = 2.2;
        const float  heights[] = { 0.35f, 0.65f, 1.0f, 0.55f, 0.40f };
        ctx.set_fill_style(BLRgba32(0x9B, 0x59, 0xB6, 200));
        for (int i = 0; i < 5; ++i) {
            double bh = h * 0.55 * heights[i];
            double by = y + (h - bh) * 0.5;
            ctx.fill_round_rect(BLRoundRect(barX0 + i * (barW + barGap), by, barW, bh, 0.5, 0.5));
        }

        // Play / pause indicator dot (placeholder)
        if (isPlaying) {
            ctx.set_fill_style(BLRgba32(0x00, 0xD2, 0x6A, 220));
        } else {
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 160));
        }
        ctx.fill_circle(x + 4.5, y + h * 0.5, 1.5);

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
        return std::make_unique<AudioObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

} // namespace Folio
