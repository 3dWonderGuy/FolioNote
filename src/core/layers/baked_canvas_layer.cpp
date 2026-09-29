#include "baked_canvas_layer.hpp"
#include "core/document/canvas_page.hpp"

#include <algorithm>

namespace Folio {

BakedCanvasLayer::BakedCanvasLayer() = default;

BakedCanvasLayer::~BakedCanvasLayer() {
    m_bakedContext.end();
    m_bakedSurface.reset();
}

// Memory management if canvas window has been resized
void BakedCanvasLayer::Resize(int width, int height) {
    if (m_surfaceWidth == width && m_surfaceHeight == height) {
        return;
    }

    m_surfaceWidth = width;
    m_surfaceHeight = height;

    // Detach context before destroying surface memory
    m_bakedContext.end();
    m_bakedSurface.create(width, height, BL_FORMAT_PRGB32);

    Invalidate();
}

// Cleans up everything including memory
void BakedCanvasLayer::Flush() {
    m_bakedContext.end();
    m_bakedSurface.reset();
    m_surfaceWidth = 0;
    m_surfaceHeight = 0;
    Invalidate();
}

// Marks layer for a full rebake
void BakedCanvasLayer::Invalidate() {
    m_isDirty = true;
    m_needsFullRebake = true;
    m_dirtyWorldRegion.Reset();
}

// Marks layer for a partial rebake
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
 * @param page The active CanvasPage containing spatialIndex and document entities.
 * @param viewport The current camera viewport containing matrices and visibility bounds.
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

void BakedCanvasLayer::DrawBackground(CanvasPage* activePage, const Viewport& viewport) {
    ColorTheme(activePage);

    // Fill entire backing bitmap or active page region with paper color
    m_bakedContext.fill_all();

    // Render grid dots/lines if enabled on the page
}

void BakedCanvasLayer::ColorTheme(CanvasPage* activePage) {
    // Set Blend2D fill/stroke properties based on theme configuration
    m_bakedContext.set_fill_style(BLRgba32(245, 245, 247, 255)); // Default light paper
}

} // namespace Folio