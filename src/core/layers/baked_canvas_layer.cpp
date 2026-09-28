#include "core/layers/baked_canvas_layer.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/canvas_object.hpp"

#include <cmath>
#include <algorithm>
#include <vector>

namespace Folio {

BakedCanvasLayer::BakedCanvasLayer() = default;

BakedCanvasLayer::~BakedCanvasLayer() {
    m_bakedContext.end();
}

void BakedCanvasLayer::Resize(int32_t widthPx, int32_t heightPx) {
    if (widthPx <= 0 || heightPx <= 0) return;
    if (m_widthPx == widthPx && m_heightPx == heightPx) return;

    m_widthPx = widthPx;
    m_heightPx = heightPx;

    m_bakedContext.end();

    m_bakedSurface.create(m_widthPx, m_heightPx, BL_FORMAT_PRGB32);
    m_bakedContext.begin(m_bakedSurface);
    m_isDirty = true;
}

void BakedCanvasLayer::Update(CanvasPage* activePage, const Viewport& viewport) {
    if (!activePage || m_widthPx <= 0 || m_heightPx <= 0) return;

    constexpr double EPSILON = 1e-4;
    const bool cameraChanged = std::abs(viewport.cameraX - m_lastCameraX) > EPSILON ||
                               std::abs(viewport.cameraY - m_lastCameraY) > EPSILON ||
                               std::abs(viewport.zoom - m_lastCameraZoom) > EPSILON;

    if (m_isDirty || cameraChanged) {
        Rebake(activePage, viewport);
        m_lastCameraX    = viewport.cameraX;
        m_lastCameraY    = viewport.cameraY;
        m_lastCameraZoom = viewport.zoom;
        m_isDirty        = false;
    }
}

/**
 * @brief Draws paper backdrop, desk coloring, and physical ruled/grid lines directly onto Layer 1.
 *
 * MATHEMATICAL PROCESS & WORKING LOGIC:
 * 1. Fills the raster backing store with paper color (pure white default: 0xFFFFFFFF).
 * 2. If paper style is not Blank, aligns grid snapping lines to physical millimeters:
 *      startX = floor(visibleWorldBounds.minX / stepMm) * stepMm
 *      endX   = ceil(visibleWorldBounds.maxX / stepMm) * stepMm
 *      startY = floor(visibleWorldBounds.minY / stepMm) * stepMm
 *      endY   = ceil(visibleWorldBounds.maxY / stepMm) * stepMm
 * 3. Transforms grid primitives through `viewport.worldToScreenMatrix` to maintain exact alignment
 *    with committed stroke and shape geometries.
 */
void BakedCanvasLayer::DrawBackground(CanvasPage* activePage, const Viewport& viewport) {
    if (!activePage || m_bakedSurface.width() == 0 || m_bakedSurface.height() == 0) return;

    // Fill background with paper color (pure white default)
    BLRgba32 paperBgColor(0xFF, 0xFF, 0xFF, 0xFF);
    m_bakedContext.fill_all(paperBgColor);

    if (activePage->paperStyle == PaperStyle::Blank) {
        return;
    }

    const double stepMm = activePage->gridSpacingMm > 0.0 ? activePage->gridSpacingMm : 5.0;
    const BLRgba32 gridColor(0xEB, 0xEE, 0xF2, 0xFF);

    m_bakedContext.save();
    m_bakedContext.set_transform(viewport.worldToScreenMatrix);

    double startX = std::floor(viewport.visibleWorldBounds.minX / stepMm) * stepMm;
    double endX   = std::ceil(viewport.visibleWorldBounds.maxX / stepMm) * stepMm;
    double startY = std::floor(viewport.visibleWorldBounds.minY / stepMm) * stepMm;
    double endY   = std::ceil(viewport.visibleWorldBounds.maxY / stepMm) * stepMm;

    if (activePage->paperStyle == PaperStyle::Dotted) {
        m_bakedContext.set_fill_style(gridColor);
        for (double wy = startY; wy <= endY; wy += stepMm) {
            for (double wx = startX; wx <= endX; wx += stepMm) {
                m_bakedContext.fill_circle(wx, wy, 0.35); // 0.35mm radius dots
            }
        }
    } else {
        m_bakedContext.set_stroke_style(gridColor);
        double strokeWidthMm = (viewport.zoom > 0.0) ? (0.26 / viewport.zoom) : 0.26;
        m_bakedContext.set_stroke_width(strokeWidthMm);

        if (activePage->paperStyle == PaperStyle::Grid) {
            for (double wx = startX; wx <= endX; wx += stepMm) {
                m_bakedContext.stroke_line(wx, viewport.visibleWorldBounds.minY, wx, viewport.visibleWorldBounds.maxY);
            }
        }
        for (double wy = startY; wy <= endY; wy += stepMm) {
            m_bakedContext.stroke_line(viewport.visibleWorldBounds.minX, wy, viewport.visibleWorldBounds.maxX, wy);
        }
    }

    m_bakedContext.restore();
}

/**
 * @brief Executes Layer 1 full rasterization: queries R-tree spatial index, orders objects by zOrder,
 * and bakes the background and all visible document entities to m_bakedSurface.
 *
 * @param activePage Current active CanvasPage containing document objects and spatial index.
 * @param viewport   Current camera viewport containing transform matrix and visible bounding box.
 */
void BakedCanvasLayer::Rebake(CanvasPage* activePage, const Viewport& viewport) {
    if (!activePage || m_bakedSurface.width() == 0 || m_bakedSurface.height() == 0) return;

    // 1. Clear backing store
    m_bakedContext.clear_all();

    // 2. Draw paper background and grid lines
    DrawBackground(activePage, viewport);

    // 3. Set camera world-to-screen matrix
    m_bakedContext.save();
    m_bakedContext.set_transform(viewport.worldToScreenMatrix);

    // 4. Query the page's spatial index using the viewport world bounds
    std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(viewport.visibleWorldBounds);

    // 5. Retrieve visible object pointers, filtering out null or invisible entities
    std::vector<std::shared_ptr<CanvasObject>> visibleObjects;
    visibleObjects.reserve(candidateUids.size());

    for (uint32_t uid : candidateUids) {
        auto obj = activePage->FindObjectByUid(uid);
        if (!obj || !obj->isVisible) continue;
        visibleObjects.push_back(std::move(obj));
    }

    // 6. Stable-sort visible objects ascending by obj->zOrder
    std::stable_sort(visibleObjects.begin(), visibleObjects.end(), [](const auto& a, const auto& b) {
        return a->zOrder < b->zOrder;
    });

    // 7. Render each object via obj->Render(m_bakedContext, viewport)
    for (const auto& obj : visibleObjects) {
        obj->Render(m_bakedContext, viewport);
    }

    m_bakedContext.restore();
}

void BakedCanvasLayer::Composite(BLContext& targetCtx, const Viewport& /*viewport*/) {
    if (m_bakedSurface.width() == 0 || m_bakedSurface.height() == 0) return;
    targetCtx.blit_image(BLPointI(0, 0), m_bakedSurface);
}

} // namespace Folio