#include "core/layers/layer_compositor_manager.hpp"
#include "core/document/canvas_page.hpp"

namespace Folio {

LayerCompositorManager::LayerCompositorManager() = default;

void LayerCompositorManager::SetSurfaceSize(int32_t widthPx, int32_t heightPx) {
    m_widthPx = widthPx;
    m_heightPx = heightPx;
    m_bakedLayer.Resize(widthPx, heightPx);
}

void LayerCompositorManager::RenderFrame(BLContext& finalContext,
                                        CanvasPage* activePage,
                                        const Viewport& viewport,
                                        const std::unordered_map<uint32_t, const CanvasObject*>& activeAppObjects,
                                        double deltaTime) {
    if (!activePage || m_widthPx <= 0 || m_heightPx <= 0) return;

    // --- PASS 1: Layer 1 Baked Canvas (Committed Entities Cache) ---
    // BakedCanvasLayer acts as a passive raster cache: Render() updates its internal
    // backing surface (handling full or partial dirty regions), and we composite the
    // resulting raster directly to the destination frame context at (0, 0).
    m_bakedLayer.Render(activePage, viewport);
    const BLImage& bakedSurface = m_bakedLayer.GetSurface();
    if (!bakedSurface.is_empty()) {
        finalContext.blit_image(BLPointI(0, 0), bakedSurface);
    }

    // --- PASS 2: Layer 2 Live Interaction (Ink / Marquee / Gizmo / Text / Laser) ---
    if (m_liveLayer.HasActiveInteraction()) {
        m_liveLayer.Render(finalContext, viewport);
    }

    // --- PASS 3: Layer 3 Embedded Running Apps ---
    m_appLayer.UpdateAndRender(viewport, activeAppObjects, deltaTime);
}

} // namespace Folio