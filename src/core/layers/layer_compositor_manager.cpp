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

    // --- PASS 1: Layer 1 Baked Canvas ---
    m_bakedLayer.Update(activePage, viewport);
    m_bakedLayer.Composite(finalContext, viewport);

    // --- PASS 2: Layer 2 Live Interaction (Ink / Marquee / Snapping) ---
    if (m_liveLayer.HasActiveInteraction()) {
        m_liveLayer.Render(finalContext, viewport);
    }

    // --- PASS 3: Layer 3 Embedded Running Apps ---
    m_appLayer.UpdateAndRender(viewport, activeAppObjects, deltaTime);
}

} // namespace Folio