#pragma once
/**
 * =========================================================================================
 * @file core/actions/action_types.hpp
 * @brief Common types, resource definitions, and execution context for the Action Scheduler
 * =========================================================================================
 */

#include <cstdint>
#include <string_view>
#include <functional>

struct AABB;
class CanvasPage;
class CanvasEngine;
class DocumentSession;

namespace Folio {

class CommandHistory;
class LayerCompositorManager;

/**
 * @brief Resource flags for mutual exclusion arbitration across active canvas operations.
 * Actions declare required resources; when an incoming action conflicts with active ones,
 * the scheduler arbitrates preemption or cancellation.
 */
enum class CanvasResource : uint32_t {
    None              = 0,
    Selection         = 1 << 0, ///< Manipulating active selected objects (drag, rotate, scale)
    Camera            = 1 << 1, ///< Viewport navigation, smooth pan/zoom easing
    LiveMathSolver    = 1 << 2, ///< Real-time formula solving, parametric curve recalculation
    BackgroundCulling = 1 << 3, ///< Spatial index R-Tree rebuilding or geometry decimation
    DocumentStructure = 1 << 4, ///< Section/page hierarchy mutations or layer modifications
    All               = 0xFFFFFFFF
};

constexpr inline CanvasResource operator|(CanvasResource a, CanvasResource b) noexcept {
    return static_cast<CanvasResource>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr inline CanvasResource operator&(CanvasResource a, CanvasResource b) noexcept {
    return static_cast<CanvasResource>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr inline CanvasResource operator~(CanvasResource a) noexcept {
    return static_cast<CanvasResource>(~static_cast<uint32_t>(a));
}

constexpr inline uint32_t ToResourceMask(CanvasResource res) noexcept {
    return static_cast<uint32_t>(res);
}

/**
 * @brief Termination reason passed to ICanvasAction::OnEnd.
 */
enum class ActionEndReason : uint8_t {
    Finished,    ///< Completed normally and successfully (synthesizes undo command if applicable)
    Interrupted, ///< Preempted early by user gesture, conflicting action, or explicit cancellation
    Failed       ///< Aborted due to numerical instability, invalid state, or missing data
};

/**
 * @brief Context passed to action lifecycle methods providing access to engine, layer manager & document state.
 */
struct ActionContext {
    CanvasPage* page{nullptr};
    CanvasEngine* engine{nullptr};
    DocumentSession* session{nullptr};
    CommandHistory* history{nullptr};
    LayerCompositorManager* layerManager{nullptr};
    std::function<void()> onInvalidateLayer;
    std::function<void(const AABB&)> onInvalidateLayerRect;
    double frameTimeSeconds{0.0};

    /**
     * @brief Instantly marks the active canvas layer dirty for full viewport re-bake.
     * Propagates immediately from ActionManager into LayerManager to ensure zero skipped frames.
     */
    void InvalidateLayer() noexcept {
        if (onInvalidateLayer) {
            onInvalidateLayer();
        }
    }

    /**
     * @brief Instantly marks a localized bounding box dirty on the active canvas layer.
     */
    void InvalidateLayerRect(const AABB& dirtyBounds) noexcept {
        if (onInvalidateLayerRect) {
            onInvalidateLayerRect(dirtyBounds);
        }
    }
};

using ActionId = uint64_t;

} // namespace Folio

