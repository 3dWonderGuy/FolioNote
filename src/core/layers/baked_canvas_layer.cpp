/**
 * =========================================================================================
 * @file core/layers/baked_canvas_layer.cpp
 * @brief Implementation of Layer 1 High-Performance Cached Rasterizer for Committed Objects
 * =========================================================================================
 *
 * GENERAL ARCHITECTURAL PROCESS:
 * ------------------------------
 * This translation unit manages the caching lifecycle of all committed vector canvas entities.
 *
 * 1. Surface Allocation:
 *    - Uses Blend2D's 32-bit premultiplied ARGB format (BL_FORMAT_PRGB32) for fast SIMD
 *      rasterization and hardware blits.
 *    - The backing context (`m_bakedContext`) is detached (`end()`) before any re-allocation
 *      to ensure memory safety.
 *
 * 2. Invalidation Discipline:
 *    - Path A (Full Frustum Rebake):
 *      When the camera pans, zooms, or window resizes, the entire visible frustum is cleared
 *      and repainted. The page R-Tree spatial index queries all objects touching `viewport.visibleWorldBounds`.
 *    - Path B (Partial Dirty Region Patch):
 *      When an existing object moves, is erased, or is inserted, the damage bounds `m_dirtyWorldRegion`
 *      are merged. The raster context sets a screen scissor-clip (`clip_to_rect`), repaints the paper
 *      background only within the clipped rect, queries only objects touching the damage box,
 *      and redraws them in ascending z-order.
 *
 * 3. Deterministic Painter's Layering:
 *    - All candidates retrieved from spatial queries are sorted by ascending `zOrder` (`a->zOrder < b->zOrder`),
 *      guaranteeing identical visual stacking regardless of R-Tree node insertion order.
 */

#include "baked_canvas_layer.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/canvas_object.hpp"

#include <algorithm>

namespace Folio {

BakedCanvasLayer::BakedCanvasLayer() = default;

BakedCanvasLayer::~BakedCanvasLayer() {
    m_bakedContext.end();
    m_bakedSurface.reset();
}

/**
 * @brief Reallocates the offscreen backing surface when window dimensions change.
 *
 * @param[in] width  New viewport width in screen pixels.
 * @param[in] height New viewport height in screen pixels.
 */
void BakedCanvasLayer::Resize(int width, int height) {
    if (m_surfaceWidth == width && m_surfaceHeight == height) {
        return;
    }

    m_surfaceWidth = width;
    m_surfaceHeight = height;

    // Detach context before destroying surface memory to avoid dangling handles
    m_bakedContext.end();
    m_bakedSurface.create(width, height, BL_FORMAT_PRGB32);

    Invalidate();
}

/**
 * @brief Cleans up backing store and releases raster surface memory.
 */
void BakedCanvasLayer::Flush() {
    m_bakedContext.end();
    m_bakedSurface.reset();
    m_surfaceWidth = 0;
    m_surfaceHeight = 0;
    Invalidate();
}

/**
 * @brief Marks layer for a full frustum re-bake on the next presentation tick.
 */
void BakedCanvasLayer::Invalidate() {
    m_isDirty = true;
    m_needsFullRebake = true;
    m_dirtyWorldRegion.Reset();
}

/**
 * @brief Marks a specific world-space bounding box dirty for localized partial re-baking.
 *
 * MATHEMATICAL PROCESS:
 * - If a full rebake is already pending (`m_needsFullRebake == true`), any partial damage
 *   is inherently covered by the full pass; early-exit to avoid redundant AABB expansion.
 * - Otherwise, merges `dirtyBounds` into `m_dirtyWorldRegion`:
 *     minX = min(minX, dirtyBounds.minX), maxX = max(maxX, dirtyBounds.maxX)
 *     minY = min(minY, dirtyBounds.minY), maxY = max(maxY, dirtyBounds.maxY)
 *
 * @param[in] dirtyBounds The AABB in world millimeters requiring re-rasterization.
 */
void BakedCanvasLayer::InvalidateRect(const AABB& dirtyBounds) {
    if (m_needsFullRebake) {
        return; // Full rebake already queued; ignore sub-region
    }

    m_dirtyWorldRegion.Merge(dirtyBounds);
    m_isDirty = true;
}

/**
 * @brief Renders the scene into the backing cache surface (handles both full & partial rebakes).
 *
 * MATHEMATICAL & COMPOSITING PROCESS:
 * 1. Surface Allocation: Verifies or instantiates a 32-bit premultiplied ARGB backing store (BL_FORMAT_PRGB32)
 *    matching current viewport dimensions (m_surfaceWidth x m_surfaceHeight).
 * 2. Background Pass: Fills paper color across the active region.
 * 3. Coordinate Transformation:
 *    - Applies viewport.worldToScreenMatrix mapping world coordinates (mm) to screen buffer pixels:
 *      [ x_screen ]   [ m00 m01 dx ] [ x_world ]
 *      [ y_screen ] = [ m10 m11 dy ] [ y_world ]
 *      [    1     ]   [  0   0   1 ] [    1    ]
 * 4. Spatial Culling & Z-Ordering:
 *    - In Path A (Full Rebake): Queries page spatialIndex (R-Tree) using viewport.visibleWorldBounds.
 *    - In Path B (Partial Rebake): Clips the context to screen-transformed dirty rect,
 *      repaints background, and queries only objects intersecting m_dirtyWorldRegion.
 *    - Resolves object UIDs via page->FindObjectByUid(uid) and sorts candidates strictly by zOrder ascending
 *      (min to max) to guarantee back-to-front painter's algorithm rasterization.
 * 5. Lifecycle Flush: Resets dirty flags and bounding regions upon completion.
 *
 * @param[in] page     The active CanvasPage containing spatialIndex and document entities.
 * @param[in] viewport The current camera viewport containing matrices and visibility bounds.
 */
void BakedCanvasLayer::Render(CanvasPage* page, const Viewport& viewport) {
    if (!m_isDirty || m_surfaceWidth <= 0 || m_surfaceHeight <= 0 || !page) {
        return;
    }

    if (m_bakedSurface.is_empty()) {
        m_bakedSurface.create(m_surfaceWidth, m_surfaceHeight, BL_FORMAT_PRGB32);
    }

    m_bakedContext.begin(m_bakedSurface);

    if (m_needsFullRebake) {
        // --- Path A: Full Viewport Rebake (Camera pan/zoom/resize) ---
        DrawBackground(page, viewport);

        // 1. Spatial query visible entities via R-Tree frustum culling
        auto uids = page->spatialIndex.Query(viewport.visibleWorldBounds);

        // 2. Collect visible entities and resolve shared ownership
        std::vector<std::shared_ptr<CanvasObject>> candidates;
        candidates.reserve(uids.size());
        for (uint32_t uid : uids) {
            auto obj = page->FindObjectByUid(uid);
            if (obj && obj->isVisible) {
                candidates.push_back(std::move(obj));
            }
        }

        // 3. Sort strictly by ascending z-order to preserve back-to-front visual layering
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a->zOrder < b->zOrder;
        });

        // 4. Apply camera world-to-screen matrix transform and rasterize objects
        m_bakedContext.save();
        m_bakedContext.set_transform(viewport.worldToScreenMatrix);

        for (const auto& obj : candidates) {
            obj->Render(m_bakedContext, viewport);
        }

        m_bakedContext.restore();
    } 
    else {
        // --- Path B: Partial Region Patch (Single object mutation) ---
        if (m_dirtyWorldRegion.IsValid() && !m_dirtyWorldRegion.IsEmpty()) {
            m_bakedContext.save();

            // 1. Scissor-clip in screen space using the projected dirty bounds
            BLRect screenClip = viewport.WorldToScreenRect(m_dirtyWorldRegion);
            m_bakedContext.clip_to_rect(screenClip);

            // 2. Repaint paper backdrop inside the clipped screen patch
            DrawBackground(page, viewport);

            // 3. Apply camera world-to-screen transform for vector object rendering
            m_bakedContext.set_transform(viewport.worldToScreenMatrix);

            // 4. Query only objects touching the dirty bounding box via R-Tree
            auto uids = page->spatialIndex.Query(m_dirtyWorldRegion);

            std::vector<std::shared_ptr<CanvasObject>> candidates;
            candidates.reserve(uids.size());
            for (uint32_t uid : uids) {
                auto obj = page->FindObjectByUid(uid);
                if (obj && obj->isVisible) {
                    candidates.push_back(std::move(obj));
                }
            }

            // 5. Sort strictly by ascending z-order
            std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
                return a->zOrder < b->zOrder;
            });

            for (const auto& obj : candidates) {
                obj->Render(m_bakedContext, viewport);
            }

            m_bakedContext.restore();
        }
    }

    m_bakedContext.end();

    // Reset tracking flags for the next frame
    m_isDirty = false;
    m_needsFullRebake = false;
    m_dirtyWorldRegion.Reset();
}

/**
 * @brief Renders the paper color backdrop and surface clearing.
 *
 * @param[in] activePage The active canvas page with theme settings.
 * @param[in] viewport   Active viewport for coordinate reference.
 */
void BakedCanvasLayer::DrawBackground(CanvasPage* activePage, const Viewport& /*viewport*/) {
    ColorTheme(activePage);

    // Fill entire backing bitmap or active page region with paper color
    m_bakedContext.fill_all();
}

/**
 * @brief Resolves color styling and paper background fills.
 *
 * @param[in] activePage Current active page.
 */
void BakedCanvasLayer::ColorTheme(CanvasPage* /*activePage*/) {
    // Set Blend2D fill/stroke properties based on theme configuration
    m_bakedContext.set_fill_style(BLRgba32(245, 245, 247, 255)); // Default light paper
}

} // namespace Folio