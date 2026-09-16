/**
 * @file special_action_manager.hpp
 * @brief Extensible shortcut and special gesture action system.
 *
 * ARCHITECTURAL ROLE:
 * -------------------
 * As FolioNote evolves with richer interaction paradigms (pen double-taps, long-press holds,
 * multi-finger tap shortcuts like Undo/Redo, radial menu triggers), hardcoding these behaviors
 * inside device dispatchers creates spaghetti code and breaks maintainability.
 *
 * SpecialActionManager decouples gesture/shortcut recognition from action execution:
 *   1. Recognition: Analyzes timing and spatial windows for double-clicks, dwell holds, and multi-finger taps.
 *   2. Semantic Action: Emits strongly typed `SpecialActionType` events (e.g. Undo, Redo, ToggleEraser).
 *   3. Dispatch: Triggers registered action callbacks or executes built-in default canvas actions.
 */

#pragma once
#include <cstdint>
#include <cmath>
#include <functional>
#include <vector>
#include <string>
#include "input/stateMachine/input_configuration.hpp"

class CanvasEngine;
class DocumentSession;

namespace FolioInput {

/**
 * @enum SpecialActionType
 * @brief High-level semantic actions triggered by gestures, double-clicks, or shortcuts.
 */
enum class SpecialActionType : uint8_t {
    None = 0,
    Undo,            ///< Revert last stroke or modification (e.g. 2-finger tap)
    Redo,            ///< Re-apply last reverted modification (e.g. 3-finger tap)
    ToggleEraser,    ///< Toggle between inking and eraser (e.g. pen double-tap)
    SelectAtPoint,   ///< Select object under pointer or clear selection (e.g. screen tap)
    OpenContextMenu, ///< Open radial menu or context popup (e.g. press-and-hold)
    ClearSelection,  ///< Deselect all active objects
    ZoomToFit        ///< Reset zoom to fit page content
};

/**
 * @struct TapRecord
 * @brief Historical record of a single tap/click event for temporal/spatial clustering.
 */
struct TapRecord {
    float x = 0.0f;
    float y = 0.0f;
    uint64_t timestampMs = 0;
    bool isValid = false;
};

/**
 * @class SpecialActionManager
 * @brief Evaluates double-clicks, holds, and multi-touch shortcuts and executes semantic actions.
 */
class SpecialActionManager {
public:
    using ActionCallback = std::function<void(SpecialActionType action, float x, float y)>;

    SpecialActionManager() = default;

    /**
     * @brief Evaluates a click/tap event to determine if it completes a double-click gesture.
     *
     * MATHEMATICAL LOGIC:
     * -------------------
     * Let previous tap be at $(x_1, y_1)$ at time $t_1$.
     * Let incoming tap be at $(x_2, y_2)$ at time $t_2$.
     *
     * The incoming tap is classified as a Double-Click if and only if:
     *   1. Temporal constraint: $\Delta t = t_2 - t_1 \le T_{\text{max}} \quad (\text{default } 300\,\text{ms})$
     *   2. Spatial constraint:   $D = \sqrt{(x_2 - x_1)^2 + (y_2 - y_1)^2} \le R_{\text{max}} \quad (\text{default } 12\,\text{px})$
     *
     * @param x       Pixel X position of the click in window coordinates.
     * @param y       Pixel Y position of the click in window coordinates.
     * @param nowMs   Current system timestamp in milliseconds.
     * @param config  Configuration parameters supplying temporal and spatial thresholds.
     * @return true if a double-click was recognized, false otherwise.
     */
    bool EvaluateDoubleClick(float x, float y, uint64_t nowMs, const InputConfiguration& config);

    /**
     * @brief Resets the click history, discarding any pending double-click state.
     */
    void ResetClickHistory() noexcept;

    /**
     * @brief Registers an external listener callback to receive special action events.
     *
     * @param callback Function to invoke when a special action is triggered.
     */
    void RegisterActionCallback(ActionCallback callback);

    /**
     * @brief Dispatches a special action to all registered callbacks and executes default handling.
     *
     * @param action  The semantic action to execute.
     * @param x       Local X coordinate where the action originated.
     * @param y       Local Y coordinate where the action originated.
     * @param canvas  Reference to CanvasEngine for viewport and object updates.
     * @param session Reference to DocumentSession for undo/redo and object mutations.
     */
    void TriggerAction(SpecialActionType action, float x, float y, CanvasEngine& canvas, DocumentSession& session);

    /**
     * @brief Converts a SpecialActionType enum value into a human-readable string for logging.
     */
    [[nodiscard]] static std::string ActionToString(SpecialActionType action);

private:
    TapRecord lastTap;
    std::vector<ActionCallback> callbacks;
};

} // namespace FolioInput
