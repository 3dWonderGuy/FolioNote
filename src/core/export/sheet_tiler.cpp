/**
 * =========================================================================================
 * @file sheet_tiler.cpp
 * @brief Implementation of RNote-Style Standard Sheet Tiling & Spatial Grid Slicing
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Physical Paper Dimension Resolution:
 *    Resolves ISO and ANSI standard paper sizes (A4, US Letter, A3, A5) accounting for
 *    portrait vs. landscape orientation.
 * 2. Automatic Width Fit (Aspect-Ratio Preserved Zoom):
 *    Calculates $W_{\text{content}} = X_{\max} - X_{\min}$. When drawn space exceeds the
 *    physical sheet width, computes uniform scaling factor $s = W_{\text{sheet}} / W_{\text{content}}$.
 *    Virtual sheet dimensions in canvas coordinates become $V_W = W_{\text{sheet}} / s$ and
 *    $V_H = H_{\text{sheet}} / s$, ensuring the drawing fits standard paper with zero distortion.
 * 3. Dual Traversal Slicing:
 *    - Column-Major: Column by column (top-to-bottom, advancing left-to-right).
 *    - Row-Major: Row by row (left-to-right, advancing top-to-bottom).
 * 4. Spatial Binning & Bounding Intersection:
 *    Each sheet tile filters canvas objects whose AABBs intersect the tile's world rectangle.
 */

#include "core/export/sheet_tiler.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/canvas_object.hpp"
#include "utils/logger.hpp"
#include <cmath>
#include <algorithm>

namespace Folio {

void SheetTiler::GetStandardSheetDimensions(const CanvasPage& page, double& outWidthMm, double& outHeightMm) {
    double w = 210.0;
    double h = 297.0;

    switch (page.pageSizeFormat) {
        case PageSizeFormat::Letter:
            w = 215.9;
            h = 279.4;
            break;
        case PageSizeFormat::A4:
            w = 210.0;
            h = 297.0;
            break;
        case PageSizeFormat::A3:
            w = 297.0;
            h = 420.0;
            break;
        case PageSizeFormat::A5:
            w = 148.0;
            h = 210.0;
            break;
        case PageSizeFormat::Custom:
            w = page.pageWidthMm > 10.0 ? page.pageWidthMm : 210.0;
            h = page.pageHeightMm > 10.0 ? page.pageHeightMm : 297.0;
            break;
    }

    if (page.pageIsLandscape) {
        std::swap(w, h);
    }

    outWidthMm = w;
    outHeightMm = h;
}

AABB SheetTiler::ComputeContentAABB(const CanvasPage& page) {
    if (page.objects.empty()) {
        double w = 210.0, h = 297.0;
        GetStandardSheetDimensions(page, w, h);
        return AABB(0.0, 0.0, w, h);
    }

    bool hasValidAABB = false;
    AABB totalAABB;

    for (const auto& obj : page.objects) {
        if (!obj) continue;
        const AABB& box = obj->bounds;
        if (box.Width() > 0.0 || box.Height() > 0.0) {
            if (!hasValidAABB) {
                totalAABB = box;
                hasValidAABB = true;
            } else {
                totalAABB.Merge(box);
            }
        }
    }

    if (!hasValidAABB) {
        double w = 210.0, h = 297.0;
        GetStandardSheetDimensions(page, w, h);
        return AABB(0.0, 0.0, w, h);
    }

    return totalAABB;
}

std::vector<SheetTile> SheetTiler::TilePage(
    const CanvasPage& page,
    SheetTraversalOrder traversalOrder,
    PageScalingMode scalingMode,
    bool skipEmptySheets
) {
    std::vector<SheetTile> tiles;

    double sheetWidthMm = 210.0;
    double sheetHeightMm = 297.0;
    GetStandardSheetDimensions(page, sheetWidthMm, sheetHeightMm);

    AABB contentAABB = ComputeContentAABB(page);

    // Calculate uniform aspect-ratio scaling factor
    double uniformScale = 1.0;
    if (scalingMode == PageScalingMode::AutomaticWidthFit) {
        double contentWidth = std::max(10.0, contentAABB.maxX - contentAABB.minX);
        if (contentWidth > sheetWidthMm) {
            uniformScale = sheetWidthMm / contentWidth;
        }
    }

    // Virtual sheet dimensions in unscaled canvas millimeters
    double virtualTileW = sheetWidthMm / uniformScale;
    double virtualTileH = sheetHeightMm / uniformScale;

    // Grid coordinates
    int cMin = 0;
    int cMax = 0;
    int rMin = 0;
    int rMax = 0;

    if (!page.objects.empty()) {
        cMin = std::max(0, static_cast<int>(std::floor(contentAABB.minX / virtualTileW)));
        cMax = std::max(0, static_cast<int>(std::floor(contentAABB.maxX / virtualTileW)));
        rMin = std::max(0, static_cast<int>(std::floor(contentAABB.minY / virtualTileH)));
        rMax = std::max(0, static_cast<int>(std::floor(contentAABB.maxY / virtualTileH)));
    }

    // Traversal Order:
    // 1. ColumnMajor_TopBottom_LeftRight: Print each column top-to-bottom, advancing left-to-right
    // 2. RowMajor_LeftRight_TopBottom: Print each row left-to-right, advancing top-to-bottom
    std::vector<std::pair<int, int>> gridSequence;

    if (traversalOrder == SheetTraversalOrder::ColumnMajor_TopBottom_LeftRight) {
        for (int c = cMin; c <= cMax; ++c) {
            for (int r = rMin; r <= rMax; ++r) {
                gridSequence.emplace_back(c, r);
            }
        }
    } else {
        for (int r = rMin; r <= rMax; ++r) {
            for (int c = cMin; c <= cMax; ++c) {
                gridSequence.emplace_back(c, r);
            }
        }
    }

    size_t sequenceIndex = 0;

    for (const auto& [c, r] : gridSequence) {
        double tileWorldX = static_cast<double>(c) * virtualTileW;
        double tileWorldY = static_cast<double>(r) * virtualTileH;
        AABB tileAABB(tileWorldX, tileWorldY, tileWorldX + virtualTileW, tileWorldY + virtualTileH);

        // Spatial binning: discover objects intersecting this sheet tile
        std::vector<std::shared_ptr<CanvasObject>> intersecting;
        for (const auto& obj : page.objects) {
            if (!obj) continue;
            if (tileAABB.Intersects(obj->bounds)) {
                intersecting.push_back(obj);
            }
        }

        bool hasContent = !intersecting.empty();

        // If skipEmptySheets is true, skip tiles with 0 intersecting objects (unless grid is only 1 sheet)
        if (skipEmptySheets && !hasContent && gridSequence.size() > 1) {
            continue;
        }

        SheetTile tile;
        tile.columnIndex = c;
        tile.rowIndex = r;
        tile.sequenceIndex = sequenceIndex++;
        tile.worldX = tileWorldX;
        tile.worldY = tileWorldY;
        tile.widthMm = sheetWidthMm;
        tile.heightMm = sheetHeightMm;
        tile.uniformScale = uniformScale;
        tile.hasContent = hasContent;
        tile.sheetAnchorId = "sheet_" + page.guid + "_c" + std::to_string(c) + "_r" + std::to_string(r);
        tile.intersectingObjects = std::move(intersecting);

        tiles.push_back(std::move(tile));
    }

    // Safety fallback: guarantee at least one sheet tile
    if (tiles.empty()) {
        SheetTile fallback;
        fallback.columnIndex = 0;
        fallback.rowIndex = 0;
        fallback.sequenceIndex = 0;
        fallback.worldX = 0.0;
        fallback.worldY = 0.0;
        fallback.widthMm = sheetWidthMm;
        fallback.heightMm = sheetHeightMm;
        fallback.uniformScale = 1.0;
        fallback.hasContent = false;
        fallback.sheetAnchorId = "sheet_" + page.guid + "_c0_r0";
        tiles.push_back(std::move(fallback));
    }

    return tiles;
}

} // namespace Folio
