#pragma once

#include <cstdint>
#include <vector>
#include <blend2d/blend2d.h>

#include "core/spatial/aabb.hpp"
#include "core/engine/viewport.hpp"

class CanvasPage;
class CanvasObject;

namespace Folio {

/**
 * @brief Layer 1 Host: Manages offscreen cached rasterization of committed document entities.
 * Re-bakes only when document entities mutate or the camera changes.
 */
class BakedCanvasLayer {
public:
    BakedCanvasLayer();
    ~BakedCanvasLayer();

    BakedCanvasLayer(const BakedCanvasLayer&) = delete;
    BakedCanvasLayer& operator=(const BakedCanvasLayer&) = delete;

    void Resize(int32_t widthPx, int32_t heightPx);
    void Invalidate() noexcept { m_isDirty = true; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_isDirty; }

    /**
     * @brief Evaluates whether a re-bake is needed and executes the Layer 1 draw pass.
     */
    void Update(CanvasPage* activePage, const Viewport& viewport);

    /**
     * @brief Blits the baked cache onto the destination rendering context.
     */
    void Composite(BLContext& targetCtx, const Viewport& viewport);

    [[nodiscard]] const BLImage& GetSurface() const noexcept { return m_bakedSurface; }

private:
    BLImage   m_bakedSurface;
    BLContext m_bakedContext;

    int32_t   m_widthPx  = 0;
    int32_t   m_heightPx = 0;
    bool      m_isDirty  = true;

    double    m_lastCameraX    = 0.0;
    double    m_lastCameraY    = 0.0;
    double    m_lastCameraZoom = 0.0;

    void Rebake(CanvasPage* activePage, const Viewport& viewport);
    void DrawBackground(CanvasPage* activePage, const Viewport& viewport);
};

} // namespace Folio