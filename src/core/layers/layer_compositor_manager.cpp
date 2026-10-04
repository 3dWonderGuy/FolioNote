/**
 * =========================================================================================
 * @file core/layers/layer_compositor_manager.cpp
 * @brief Implementation of LayerCompositorManager (3-Layer Master Compositor)
 * =========================================================================================
 *
 * GENERAL ARCHITECTURAL FLOW:
 * ---------------------------
 * This translation unit orchestrates the frame composition pipeline:
 * 1. Surface Allocation: Propagates window dimensions to `BakedCanvasLayer` to ensure backing
 *    PRGB32 memory buffers match screen metrics.
 * 2. Invalidation Routing: Directs full or partial dirty bounding boxes to Layer 1.
 * 3. Deterministic 3-Pass Frame Assembly:
 *    - Pass 1: BakedCanvasLayer::Render() updates static cache if dirty, then blits cached
 *      PRGB32 surface at (0, 0).
 *    - Pass 2: LiveInteractionLayer::Render() overlays active ephemeral vector graphics
 *      (pen ink, gizmo, marquee box, eraser reticle).
 *    - Pass 3: EmbeddedAppLayer::UpdateAndRender() updates active runtime app frames and controls.
 */

#include "core/layers/layer_compositor_manager.hpp"
#include "core/document/canvas_page.hpp"
#include "utils/logger.hpp"

#include <string>

namespace Folio {

LayerCompositorManager::LayerCompositorManager() {
    LOG_INFO(LayerCompositorManager, "LayerCompositorManager initialized (3-tier compositing active)");
}

/**
 * @brief Resizes compositor viewport surface dimensions.
 *
 * @param[in] widthPx  Screen width in pixels.
 * @param[in] heightPx Screen height in pixels.
 */
void LayerCompositorManager::SetSurfaceSize(int32_t widthPx, int32_t heightPx) {
    if (m_widthPx == widthPx && m_heightPx == heightPx) {
        return;
    }

    LOG_INFO(LayerCompositorManager, "SetSurfaceSize: Resizing canvas surface to " +
             std::to_string(widthPx) + "x" + std::to_string(heightPx) + " px");

    m_widthPx = widthPx;
    m_heightPx = heightPx;
    m_bakedLayer.Resize(widthPx, heightPx);
}

/**
 * @brief Invalidate entire Layer 1 static cache.
 */
void LayerCompositorManager::InvalidateBakedCanvas() noexcept {
    LOG_INFO(LayerCompositorManager, "Invalidating BakedCanvasLayer (full rebake queued)");
    m_bakedLayer.Invalidate();
}

/**
 * @brief Invalidate specific sub-region in world millimeters for localized partial baking.
 *
 * @param[in] dirtyBounds The AABB in world millimeters.
 */
void LayerCompositorManager::InvalidateBakedCanvasRect(const AABB& dirtyBounds) noexcept {
    LOG_INFO(LayerCompositorManager, "Invalidating BakedCanvasLayer sub-rect: [" +
             std::to_string(dirtyBounds.minX) + ", " + std::to_string(dirtyBounds.minY) + " to " +
             std::to_string(dirtyBounds.maxX) + ", " + std::to_string(dirtyBounds.maxY) + "] mm");
    m_bakedLayer.InvalidateRect(dirtyBounds);
}

/**
 * @brief Executes multi-tier frame composition into destination context.
 *
 * @param[in,out] finalContext     Target context for blitting and composite output.
 * @param[in]     activePage       The current active canvas page.
 * @param[in]     viewport         Camera viewport matrices and frustum bounds.
 * @param[in]     activeAppObjects Running interactive Layer 3 objects.
 * @param[in]     deltaTime        Frame delta time in seconds.
 */
void LayerCompositorManager::RenderFrame(BLContext& finalContext,
                                         CanvasPage* activePage,
                                         const Viewport& viewport,
                                         const std::unordered_map<uint32_t, const CanvasObject*>& activeAppObjects,
                                         double deltaTime) {
    if (!activePage || m_widthPx <= 0 || m_heightPx <= 0) {
        return;
    }

    // --- PASS 1: Layer 1 Baked Canvas (Committed Entities Cache) ---
    // BakedCanvasLayer acts as a passive raster cache: Render() updates its internal
    // backing surface (handling full or partial dirty regions), and we composite the
    // resulting raster directly to the destination frame context at (0, 0).
    m_bakedLayer.Render(activePage, viewport);
    const BLImage& bakedSurface = m_bakedLayer.GetSurface();
    if (!bakedSurface.is_empty()) {
        finalContext.blit_image(BLPointI(0, 0), bakedSurface);
    }

    // --- PASS 2: Layer 2 Live Interaction (Ink / Marquee / Gizmo / Text / Eraser) ---
    if (m_liveLayer.HasActiveInteraction()) {
        m_liveLayer.Render(finalContext, viewport);
    }

    // --- PASS 3: Layer 3 Embedded Running Apps ---
    m_appLayer.UpdateAndRender(viewport, activeAppObjects, deltaTime);
}

} // namespace Folio