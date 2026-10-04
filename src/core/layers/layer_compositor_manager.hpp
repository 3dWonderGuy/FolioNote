#pragma once
/**
 * =========================================================================================
 * @file core/layers/layer_compositor_manager.hpp
 * @brief Master Coordinator for FolioNote's 3-Layer Rendering Architecture
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INVARIANTS:
 * ----------------------------------
 * `LayerCompositorManager` serves as the central rendering authority in FolioNote.
 * It encapsulates and coordinates three specialized, isolated rendering tiers:
 *
 * 1. Layer 1 (BakedCanvasLayer):
 *    - Caches all committed document objects (strokes, text, images, shapes, tables)
 *      into an offscreen 32-bit premultiplied ARGB raster surface (`BL_FORMAT_PRGB32`).
 *    - Re-bakes ONLY when camera bounds change or document entities are modified.
 *    - Blitted at (0, 0) directly into the destination frame context.
 *
 * 2. Layer 2 (LiveInteractionLayer):
 *    - Ephemeral, non-cached overlay rendered every frame on top of Layer 1.
 *    - Renders in-flight pen strokes, laser pointer trails, selection gizmos/handles,
 *      marquee boxes/lassos, text insertion carets, and live eraser reticles.
 *    - Performs ZERO heap allocations on the hot path via borrowed spans and preallocated buffers.
 *
 * 3. Layer 3 (EmbeddedAppLayer):
 *    - Manages interactive running applications, native controls, web view sessions,
 *      and animated video decoders.
 *    - Maps world millimeter bounds to screen pixels and invokes per-app tick/render routines.
 *
 * COORDINATE SYSTEM DISCIPLINE:
 * -----------------------------
 * - World Space (mm): Document entities and in-flight pen ink reside in physical world millimeters.
 * - Screen Space (px): Selection gizmo frames, scale handles, rotation stems, and eraser reticles
 *   are rendered in device pixels so hit targets maintain fixed physical sizes regardless of camera zoom.
 */

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <blend2d/blend2d.h>

#include "core/canvas_engine/canvas_transform.hpp"
#include "core/layers/baked_canvas_layer.hpp"
#include "core/layers/live_interaction_layer.hpp"
#include "core/layers/embedded_app_layer.hpp"

class CanvasPage;
class CanvasObject;

namespace Folio {

/**
 * @class LayerCompositorManager
 * @brief Master compositor orchestrating the 3-layer frame assembly.
 */
class LayerCompositorManager {
public:
    LayerCompositorManager();
    ~LayerCompositorManager() = default;

    LayerCompositorManager(const LayerCompositorManager&) = delete;
    LayerCompositorManager& operator=(const LayerCompositorManager&) = delete;

    /**
     * @brief Re-allocates backing surfaces across all managed layers matching window size.
     *
     * @param[in] widthPx  Viewport width in physical screen pixels (> 0).
     * @param[in] heightPx Viewport height in physical screen pixels (> 0).
     */
    void SetSurfaceSize(int32_t widthPx, int32_t heightPx);

    /**
     * @brief Marks Layer 1 (BakedCanvasLayer) dirty for a full viewport re-bake.
     * Invoked when camera pan/zoom occurs, theme changes, or major document mutation.
     */
    void InvalidateBakedCanvas() noexcept;

    /**
     * @brief Marks a localized world-space bounding box dirty for partial region invalidation.
     *
     * @param[in] dirtyBounds The AABB in world millimeters requiring re-rasterization.
     */
    void InvalidateBakedCanvasRect(const AABB& dirtyBounds) noexcept;

    /**
     * @brief Executes multi-layer frame assembly into the target presentation context.
     *
     * WORKING PROCESS & PASS EXECUTION:
     * 1. Precondition Check: Aborts if activePage is null or dimensions <= 0.
     * 2. Pass 1 (Baked Canvas Layer): Re-bakes dirty regions of Layer 1 if needed,
     *    and blits the resulting cached bitmap at (0, 0) to `finalContext`.
     * 3. Pass 2 (Live Interaction Layer): If live interactions are active (pen stroke,
     *    gizmo, marquee, eraser reticle), renders dynamic vector graphics directly on top.
     * 4. Pass 3 (Embedded App Layer): Updates and renders active running runtime widgets.
     *
     * @param[in,out] finalContext     Target Blend2D context (composite surface or backbuffer).
     * @param[in]     activePage       The current active canvas page.
     * @param[in]     viewport         Active camera viewport containing matrices and bounds.
     * @param[in]     activeAppObjects Active Layer 3 overlay targets on the current page.
     * @param[in]     deltaTime        Frame delta time in seconds for running embedded apps.
     */
    void RenderFrame(BLContext& finalContext,
                     CanvasPage* activePage,
                     const Viewport& viewport,
                     const std::unordered_map<uint32_t, const CanvasObject*>& activeAppObjects,
                     double deltaTime);

    /**
     * @brief Direct access to Layer 1 (BakedCanvasLayer).
     * @return Reference to Layer 1 host.
     */
    [[nodiscard]] BakedCanvasLayer& GetBakedCanvasLayer() noexcept { return m_bakedLayer; }

    /**
     * @brief Direct access to Layer 2 (LiveInteractionLayer).
     * @return Reference to Layer 2 host.
     */
    [[nodiscard]] LiveInteractionLayer& GetLiveInteractionLayer() noexcept { return m_liveLayer; }

    /**
     * @brief Direct access to Layer 3 (EmbeddedAppLayer).
     * @return Reference to Layer 3 host.
     */
    [[nodiscard]] EmbeddedAppLayer& GetEmbeddedAppLayer() noexcept { return m_appLayer; }

private:
    BakedCanvasLayer     m_bakedLayer;  ///< Layer 1: Static committed entity cache
    LiveInteractionLayer m_liveLayer;   ///< Layer 2: Ephemeral real-time interaction overlay
    EmbeddedAppLayer     m_appLayer;    ///< Layer 3: Interactive running widgets & media

    int32_t m_widthPx  = 0;             ///< Current surface width in screen pixels
    int32_t m_heightPx = 0;             ///< Current surface height in screen pixels
};

} // namespace Folio