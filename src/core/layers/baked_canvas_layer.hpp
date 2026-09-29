#pragma once

#include <cstdint>
#include <vector>
#include <blend2d/blend2d.h>

#include "core/spatial/aabb.hpp"
#include "core/engine/canvas_transform.hpp"

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
    BakedCanvasLayer(BakedCanvasLayer&&) noexcept = default;
    BakedCanvasLayer& operator=(BakedCanvasLayer&&) noexcept = default;

    /**
     * @brief Allocates or reallocates the offscreen backing surface when window size changes.
     */
    void Resize(int width, int height);

    /**
     * @brief Discards the current baked surface or clears context state.
     */
    void Flush();

    /**
     * @brief Renders the scene into the backing cache surface (handles both full & partial rebakes).
     */
    void Render(CanvasPage* page, const Viewport& viewport);

    /**
     * @brief Marks the entire layer dirty (e.g., camera pan/zoom, resize, theme change).
     */
    void Invalidate();

    /**
     * @brief Marks a specific world-space bounding box dirty for partial invalidation.
     */
    void InvalidateRect(const AABB& dirtyBounds);

    /**
     * @brief Returns true if the layer has pending redraw work.
     */
    [[nodiscard]] bool IsDirty() const noexcept { return m_isDirty; }

    /**
     * @brief Read-only accessor for LayerCompositorManager to blit or upload to OpenGL.
     */
    [[nodiscard]] const BLImage& GetSurface() const noexcept { return m_bakedSurface; }

private:

    // --- Backing Store ---
    BLImage m_bakedSurface;
    BLContext m_bakedContext;

    int m_surfaceWidth = 0;
    int m_surfaceHeight = 0;

    // --- Dirty State Tracking ---
    bool m_isDirty = true;
    bool m_needsFullRebake = true;
    AABB m_dirtyWorldRegion;

    /**
     * @brief Renders paper color, page margins, and grid/dot patterns.
     */
    void DrawBackground(CanvasPage* activePage, const Viewport& viewport);

    /**
     * @brief Resolves color palette based on system/page theme settings.
     */
    void ColorTheme(CanvasPage* activePage);

};

} // namespace Folio