#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <blend2d/blend2d.h>

#include "core/engine/canvas_transform.hpp"
#include "core/layers/baked_canvas_layer.hpp"
#include "core/layers/live_interaction_layer.hpp"
#include "core/layers/embedded_app_layer.hpp"

class CanvasPage;
class CanvasObject;

namespace Folio {

/**
 * @brief Master compositor orchestrating the 3-layer frame assembly.
 */
class LayerCompositorManager {
public:
    LayerCompositorManager();
    ~LayerCompositorManager() = default;

    LayerCompositorManager(const LayerCompositorManager&) = delete;
    LayerCompositorManager& operator=(const LayerCompositorManager&) = delete;

    void SetSurfaceSize(int32_t widthPx, int32_t heightPx);
    void InvalidateBakedCanvas() noexcept { m_bakedLayer.Invalidate(); }

    /**
     * @brief Marks a specific world-space bounding box dirty for partial baking.
     * @param dirtyBounds The AABB in world millimeters requiring re-rasterization.
     */
    void InvalidateBakedCanvasRect(const AABB& dirtyBounds) noexcept { m_bakedLayer.InvalidateRect(dirtyBounds); }

    /**
     * @brief Assembles BakedCanvas (L1), LiveInteraction (L2), and EmbeddedApps (L3).
     */
    void RenderFrame(BLContext& finalContext,
                     CanvasPage* activePage,
                     const Viewport& viewport,
                     const std::unordered_map<uint32_t, const CanvasObject*>& activeAppObjects,
                     double deltaTime);

    [[nodiscard]] BakedCanvasLayer&     GetBakedCanvasLayer() noexcept     { return m_bakedLayer; }
    [[nodiscard]] LiveInteractionLayer& GetLiveInteractionLayer() noexcept { return m_liveLayer; }
    [[nodiscard]] EmbeddedAppLayer&     GetEmbeddedAppLayer() noexcept     { return m_appLayer; }

private:
    BakedCanvasLayer     m_bakedLayer;  // Layer 1
    LiveInteractionLayer m_liveLayer;   // Layer 2
    EmbeddedAppLayer     m_appLayer;    // Layer 3

    int32_t m_widthPx  = 0;
    int32_t m_heightPx = 0;
};

} // namespace Folio