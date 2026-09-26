#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/audio/audio_container.hpp
 * @brief Canvas Object Representing a Linked Audio Clip / Voice Note Badge
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTERACTION MODEL:
 * -----------------------------------------
 * AudioObject provides a compact, non-resizable interactive chip (60mm x 18mm) pinned to
 * the canvas, referencing external audio files (MP3, WAV, FLAC, OGG, AAC) stored in the
 * notebook companion sidecar folder (`imports/audio/`).
 *
 * Interaction & Spatial Invariants:
 *   1. Fixed Badge Dimensions:
 *      Locked to `chipW` (60mm) x `chipH` (18mm) in world millimeters.
 *   2. Pure Translation Gizmo:
 *      Uses `GizmoStyle::MoveOnly`. When transformed in multi-object selections, only anchor
 *      translation deltas are applied, preserving locked badge scale and orientation.
 *   3. Visual Identity:
 *      Distinctive purple accent band, placeholder waveform visualizer, play/pause state dot,
 *      and formatted duration indicator.
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
 * @class AudioObject
 * @brief Fixed-size audio chip on the canvas.
 */
class AudioObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS & ATTRIBUTES
    // =========================================================================

    std::string filePath;                  ///< Path to audio file (absolute or package-relative)
    std::string displayName;               ///< Display label shown on the badge
    double durationSeconds = 0.0;          ///< Audio duration hint in seconds (0 = unqueried)

    bool isPlaying = false;                ///< Playback state flag
    double currentPositionSeconds = 0.0;   ///< Active playback position timestamp in seconds

    /// Fixed badge dimensions in world millimeters (non-resizable)
    static constexpr double chipW = 60.0;
    static constexpr double chipH = 18.0;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor initializing default chip dimensions.
     */
    AudioObject();

    /**
     * @brief Parameterized constructor initializing path, label, and duration.
     *
     * @param[in] path File path on disk.
     * @param[in] name Filename or label.
     * @param[in] duration Optional duration in seconds.
     */
    AudioObject(const std::string& path, const std::string& name, double duration = 0.0);

    // =========================================================================
    // PLAYBACK CONTROLS
    // =========================================================================

    /**
     * @brief Starts audio playback.
     */
    void Play();

    /**
     * @brief Stops audio playback and resets playback timestamp.
     */
    void Stop();

    // =========================================================================
    // TRANSFORM & GIZMO INVARIANTS
    // =========================================================================

    /**
     * @brief Applies translation while preserving locked chip dimensions.
     *
     * Mathematical Process:
     *   Maps anchor through matrix $M$:
     *     $x_{new} = worldX \cdot m00 + worldY \cdot m10 + m20$
     *     $y_{new} = worldX \cdot m01 + worldY \cdot m11 + m21$
     *   Extracts translation delta:
     *     $dx = x_{new} - worldX, \quad dy = y_{new} - worldY$
     *   Accumulates pure translation into `transform`.
     *
     * @param[in] matrix 2D affine transformation matrix.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override;

    /**
     * @brief Bakes accumulated translation into worldX/Y and resets transform matrix.
     */
    void BakeTransform() override;

    /**
     * @brief Returns MoveOnly gizmo interaction style (no corner resize handles).
     */
    [[nodiscard]] GizmoStyle GetGizmoStyle() const noexcept override;

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Draws the audio badge, purple accent band, and waveform bars.
     *
     * @param[in,out] ctx Blend2D rendering context.
     * @param[in] viewport Current camera viewport.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // CLONING
    // =========================================================================

    /**
     * @brief Clones this AudioObject.
     *
     * @return Unique pointer to cloned instance.
     */
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;
};

// Canonical type alias for backwards compatibility
using AudioContainer = AudioObject;

} // namespace Folio
