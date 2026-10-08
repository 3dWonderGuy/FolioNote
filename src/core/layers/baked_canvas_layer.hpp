#pragma once
/**
 * =========================================================================================
 * @file core/layers/baked_canvas_layer.hpp
 * @brief Layer 1 Host: High-Performance Cached Rasterizer for Committed Document Objects
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE & INVARIANTS:
 * --------------------------------
 * In FolioNote's 3-layer compositing system, `BakedCanvasLayer` operates as Layer 1:
 * 1. Static Raster Cache:
 *    - Caches all committed vector entities (strokes, text boxes, images, shapes, tables)
 *      into an offscreen 32-bit premultiplied ARGB raster surface (`m_bakedSurface`, `BL_FORMAT_PRGB32`).
 *    - Completely static during active user interaction gestures (pan, zoom, live inking, gizmo dragging).
 *    - Blitted directly to the destination frame context at (0, 0) during presentation ticks.
 *
 * 2. Two-Tier Invalidation Discipline:
 *    - Path A (Full Viewport Rebake): Triggered when camera transforms mutate (pan/zoom) or window
 *      resizes. Queries visible objects touching the camera frustum via R-Tree spatial index
 *      (`viewport.visibleWorldBounds`), clears backdrop, and rasterizes all candidates.
 *    - Path B (Partial Dirty-Region Patch): Triggered when single objects mutate (e.g., stroke added,
 *      erased, or moved). Uses scissor clipping (`ctx.clip_to_rect`) to redraw ONLY the bounding box
 *      damage area, completely avoiding full-canvas rasterization overhead.
 *
 * 3. Strict Z-Order Ascending Compositing:
 *    - Objects returned from spatial queries are sorted strictly by `zOrder` ascending before drawing
 *      to ensure deterministic back-to-front painter's algorithm rendering without layer inversions.
 */

#include <cstdint>
#include <vector>
#include <blend2d/blend2d.h>

#include "core/spatial/aabb.hpp"
#include "core/canvas_engine/canvas_transform.hpp"

class CanvasPage;
class CanvasObject;

namespace Folio {

/**
 * @class BakedCanvasLayer
 * @brief Manages offscreen cached rasterization of committed canvas objects.
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
     * @brief Allocates or reallocates the offscreen backing raster surface matching window size.
     *
     * WORKING PROCESS:
     * - Detaches active rendering context before releasing backing buffer memory.
     * - Creates `m_bakedSurface` with `BL_FORMAT_PRGB32` format.
     * - Marks layer dirty to force a full re-bake on the next presentation tick.
     *
     * @param[in] width  Physical screen width in pixels (> 0).
     * @param[in] height Physical screen height in pixels (> 0).
     */
    void Resize(int width, int height);

    /**
     * @brief Discards the current baked surface and context state, releasing GPU/CPU buffer memory.
     */
    void Flush();

    /**
     * @brief Renders committed page entities into the backing cache surface.
     *
     * MATHEMATICAL & COMPOSITING PROCESS:
     * 1. Re-bake Evaluation: Aborts immediately if `!m_isDirty`, surface dimensions <= 0, or page is null.
     * 2. Path A (Full Frustum Rebake):
     *    - Clears context and repaints paper background (`DrawBackground`).
     *    - R-Tree Broadphase: `uids = page->spatialIndex.Query(viewport.visibleWorldBounds)`.
     *    - Candidate Resolution: Resolves shared pointers and filters `obj->isVisible`.
     *    - Z-Sort: `std::sort` candidates by `a->zOrder < b->zOrder`.
     *    - World-to-Screen Transform: Maps world mm to screen pixels via `viewport.worldToScreenMatrix`.
     *    - Rasterization: Draws objects in ascending z-order.
     * 3. Path B (Partial Dirty Region Patch):
     *    - Scissor-clips context to `viewport.WorldToScreenRect(m_dirtyWorldRegion)`.
     *    - Repaints paper background strictly within the scissor clip.
     *    - Queries only entities intersecting `m_dirtyWorldRegion`.
     *    - Sorts and redraws candidates into the clipped patch.
     * 4. State Reset: Resets dirty flags and merges bounding tracking.
     *
     * @param[in] page     The active CanvasPage containing spatialIndex and document entities.
     * @param[in] viewport Current camera viewport containing matrices and visibility bounds.
     */
    void Render(CanvasPage* page, const Viewport& viewport);

    /**
     * @brief Marks the entire layer dirty (e.g. camera pan/zoom, resize, dark mode / theme change).
     * Forces Path A full re-bake on the subsequent frame.
     */
    void Invalidate();

    /**
     * @brief Marks a specific world-space bounding box dirty for partial region invalidation.
     *
     * If a full re-bake is already queued, the dirty region merge is ignored to prevent redundant math.
     *
     * @param[in] dirtyBounds The AABB in world millimeters requiring re-rasterization.
     */
    void InvalidateRect(const AABB& dirtyBounds);

    /**
     * @brief Queries whether the baked layer requires re-rasterization before presentation.
     * @return true if drawing commands are pending, false if cached surface is up to date.
     */
    [[nodiscard]] bool IsDirty() const noexcept { return m_isDirty; }

    /**
     * @brief Read-only accessor for LayerCompositorManager to blit or upload to OpenGL texture.
     * @return Const reference to the internal 32-bit PRGB32 raster surface.
     */
    [[nodiscard]] const BLImage& GetSurface() const noexcept { return m_bakedSurface; }

    struct PaperTheme {
        BLRgba32 bgColor{0xFF, 0xFF, 0xFF};
        BLRgba32 gridColor{0xEB, 0xEE, 0xF2};
        BLRgba32 borderColor{0xD0, 0xD4, 0xDC};
    };

    void SetPaperTheme(BLRgba32 bgColor, BLRgba32 gridColor, BLRgba32 borderColor) noexcept {
        if (m_theme.bgColor.value != bgColor.value ||
            m_theme.gridColor.value != gridColor.value ||
            m_theme.borderColor.value != borderColor.value) {
            m_theme.bgColor = bgColor;
            m_theme.gridColor = gridColor;
            m_theme.borderColor = borderColor;
            Invalidate();
        }
    }

    [[nodiscard]] const PaperTheme& GetPaperTheme() const noexcept { return m_theme; }

private:
    // --- Backing Store ---
    BLImage   m_bakedSurface;  ///< Cached 32-bit premultiplied ARGB raster surface
    BLContext m_bakedContext;  ///< Blend2D hardware-accelerated raster context

    int m_surfaceWidth  = 0;   ///< Backing buffer width in screen pixels
    int m_surfaceHeight = 0;   ///< Backing buffer height in screen pixels

    // --- Dirty State Tracking ---
    bool m_isDirty         = true; ///< True when any rasterization pass is queued
    bool m_needsFullRebake = true; ///< True for Path A (full frustum rebake), false for Path B (partial)
    AABB m_dirtyWorldRegion;       ///< Accumulated world-space bounding box requiring re-baking
    PaperTheme m_theme;            ///< Paper background, grid line, and border styling colors

    /**
     * @brief Renders paper color backdrop, page margins, and grid/dot patterns.
     *
     * @param[in] activePage Current active page with background grid and color styling.
     * @param[in] viewport   Active viewport for world-space grid line spacing.
     */
    void DrawBackground(CanvasPage* activePage, const Viewport& viewport);
};

} // namespace Folio