#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/audio/audio_container.hpp
 * @brief Canvas Object Representing a Playable Audio Track / Voice Note Badge
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTERACTION MODEL:
 * -----------------------------------------
 * AudioObject provides an interactive, compact chip (75mm x 22mm) pinned to the canvas.
 * It references external audio files (MP3, WAV, FLAC, M4A, AAC, OGG, OPUS, WMA) stored in
 * the notebook companion sidecar folder (`imports/audio/`).
 *
 * Audio Subsystem Integration:
 *   Powered by libVLC (`FOLIO_HAS_LIBVLC`) for hardware-accelerated, robust playback of all
 *   standard audio codecs with zero external decoder dependencies.
 *
 * Capabilities:
 *   1. Hardware-accelerated audio playback via libVLC.
 *   2. Interactive play/pause toggle button directly on the canvas chip.
 *   3. Scrub bar allowing direct seek scrubbing to any timestamp.
 *   4. Dynamic animated equalizer bars visualizer when playing.
 *   5. Pure translation gizmo (MoveOnly) preserving fixed badge scale.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <functional>
#include <cstdint>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @class AudioObject
 * @brief Interactive, playable audio badge on the canvas.
 */
class AudioObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS & ATTRIBUTES
    // =========================================================================

    std::string filePath;                  ///< Path to audio file (absolute or sidecar-relative)
    std::string displayName;               ///< Display label shown on the badge
    double durationSeconds = 0.0;          ///< Total audio duration in seconds
    double currentPositionSeconds = 0.0;   ///< Active playback position in seconds

    bool isPlaying = false;                ///< True if audio is actively playing
    bool isPaused = false;                 ///< True if audio is paused
    int volume = 100;                      ///< Volume (0 to 100)

    /// Badge dimensions in world millimeters
    static constexpr double chipW = 75.0;
    static constexpr double chipH = 22.0;

    // =========================================================================
    // CONSTRUCTORS & DESTRUCTOR
    // =========================================================================

    AudioObject();
    AudioObject(const std::string& path, const std::string& name, double duration = 0.0);
    ~AudioObject() override;

    // Copying and moves
    AudioObject(const AudioObject& other);
    AudioObject& operator=(const AudioObject& other);

    // =========================================================================
    // PLAYBACK CONTROLS
    // =========================================================================

    /**
     * @brief Starts or resumes audio playback.
     */
    void Play();

    /**
     * @brief Pauses audio playback.
     */
    void Pause();

    /**
     * @brief Stops audio playback and resets position to beginning.
     */
    void Stop();

    /**
     * @brief Toggles between play and pause states.
     */
    void TogglePlay();

    /**
     * @brief Seeks to a proportional position in the audio track.
     * @param ratio Fractional position from 0.0 (start) to 1.0 (end).
     */
    void Seek(double ratio);

    /**
     * @brief Updates internal playback position and duration from the audio engine.
     */
    void UpdateProgress();

    /**
     * @brief Adjusts audio playback volume [0 to 100].
     * @param vol Target volume level.
     */
    void SetVolume(int vol);

    /**
     * @brief Injects audio-specific actions into the unified context menu.
     * @param actions Reference to context menu item list.
     */
    void CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) override;

    // =========================================================================
    // HIT TESTING & INTERACTION
    // =========================================================================

    /**
     * @brief Handles canvas mouse clicks on the audio badge (play button, scrubber).
     *
     * Mathematical Hit Testing:
     *   Badge origin: (worldX, worldY), dimensions (chipW, chipH).
     *   Play/Pause button: circular zone at left [worldX + 2.0, worldY + 2.0] to [worldX + 18.0, worldY + 20.0].
     *   Scrubber bar: horizontal zone [worldX + 20.0, worldY + 14.0] to [worldX + chipW - 4.0, worldY + 19.0].
     *
     * @param wx World X coordinate of mouse click in millimeters.
     * @param wy World Y coordinate of mouse click in millimeters.
     * @param onDirty Callback to signal canvas repaint.
     * @return True if click hit a control (play button or scrubber); false otherwise.
     */
    bool HandleCanvasClick(double wx, double wy, std::function<void()> onDirty = nullptr);

    // =========================================================================
    // TRANSFORM & GIZMO INVARIANTS
    // =========================================================================

    void ApplyTransform(const BLMatrix2D& matrix) override;
    void BakeTransform() override;

    // =========================================================================
    // RENDERING
    // =========================================================================

    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // CLONING
    // =========================================================================

    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;

private:
    void InitPlayer();
    void ReleasePlayer();

    void* vlcInstance = nullptr;  ///< libvlc_instance_t*
    void* vlcPlayer = nullptr;    ///< libvlc_media_player_t*
    void* vlcMedia = nullptr;     ///< libvlc_media_t*
};

using AudioContainer = AudioObject;

} // namespace Folio
