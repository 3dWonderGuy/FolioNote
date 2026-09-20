#pragma once
/**
 * =========================================================================================
 * @file sheet_tiler.hpp
 * @brief RNote-Style Standard Sheet Tiling & Bounding Grid Slicing Engine
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * On an infinite or continuous 2D canvas, content can extend arbitrarily in world space.
 * SheetTiler decomposes the continuous canvas into a discrete grid of standardized physical
 * pages (e.g. A4: 210 x 297 mm, US Letter: 215.9 x 279.4 mm):
 * 1. Evaluates 2D content AABB across all canvas strokes and polymorphic objects.
 * 2. Computes the bounding grid of standard sheet tiles [c_min..c_max] x [r_min..r_max].
 * 3. Supports dual traversal patterns:
 *    - Column-Major: Prints each page column from top to bottom, advancing left to right.
 *    - Row-Major: Prints each page row from left to right, advancing top to bottom.
 * 4. Supports "Automatic Width Fit": Dynamically calculates uniform aspect-ratio scale
 *    based on the widest drawn space so wide drawings fit on a standard sheet with zero distortion.
 * 5. Spatial binning: Maps intersecting canvas objects into their respective sheet tiles.
 */

#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include "core/spatial/aabb.hpp"
#include "core/engine/canvas_transform.hpp"

class CanvasPage;
class CanvasObject;

namespace Folio {

/**
 * @enum SheetTraversalOrder
 * @brief Order in which 2D sheet tiles are sequenced into an exported document.
 */
enum class SheetTraversalOrder {
    ColumnMajor_TopBottom_LeftRight, ///< Print column by column: (0,0), (0,1)... then (1,0), (1,1)...
    RowMajor_LeftRight_TopBottom     ///< Print row by row: (0,0), (1,0)... then (0,1), (1,1)...
};

/**
 * @enum PageScalingMode
 * @brief Viewport scaling strategy applied during sheet tiling.
 */
enum class PageScalingMode {
    OriginalScale,     ///< 1:1 scale matching standard sheet size
    AutomaticWidthFit  ///< Uniformly scales widest drawn space to fit sheet width preserving aspect ratio
};

/**
 * @struct SheetTile
 * @brief Represents a discrete, standardized physical page extracted from the canvas.
 */
struct SheetTile {
    int columnIndex = 0;             ///< 0-based column index in the sheet grid
    int rowIndex = 0;                ///< 0-based row index in the sheet grid
    size_t sequenceIndex = 0;        ///< 0-based sequential page index in export order
    double worldX = 0.0;             ///< Top-left X in canvas world millimeters
    double worldY = 0.0;             ///< Top-left Y in canvas world millimeters
    double widthMm = 210.0;          ///< Standard sheet width in mm (e.g. 210.0 for A4)
    double heightMm = 297.0;         ///< Standard sheet height in mm (e.g. 297.0 for A4)
    double uniformScale = 1.0;       ///< Aspect-ratio scaling factor applied to coordinates
    bool hasContent = false;         ///< True if strokes or objects intersect this sheet
    std::string sheetAnchorId;       ///< Unique anchor ID for hyperlinks (e.g. "sheet_<guid>_c0_r1")
    std::vector<std::shared_ptr<CanvasObject>> intersectingObjects; ///< Objects inside this tile
};

/**
 * @class SheetTiler
 * @brief Decomposes a continuous CanvasPage into an ordered sequence of standard sheet tiles.
 */
class SheetTiler {
public:
    /**
     * @brief Resolves standard sheet dimensions (width, height in mm) from page configuration.
     * @param page CanvasPage to inspect.
     * @param[out] outWidthMm Resolved width in millimeters.
     * @param[out] outHeightMm Resolved height in millimeters.
     */
    static void GetStandardSheetDimensions(const CanvasPage& page, double& outWidthMm, double& outHeightMm);

    /**
     * @brief Computes the content AABB of a page by unioning all object and stroke bounds.
     * @param page CanvasPage to evaluate.
     * @return Bounding rectangle in world millimeters.
     */
    static AABB ComputeContentAABB(const CanvasPage& page);

    /**
     * @brief Slices a CanvasPage into an ordered sequence of physical sheet tiles.
     *
     * @param page CanvasPage to tile.
     * @param traversalOrder Traversal sequence (Column-Major or Row-Major).
     * @param scalingMode Original scale or Automatic Width Fit.
     * @param skipEmptySheets If true, omits tiles that contain zero objects or strokes.
     * @return Vector of populated SheetTile records.
     */
    static std::vector<SheetTile> TilePage(
        const CanvasPage& page,
        SheetTraversalOrder traversalOrder = SheetTraversalOrder::ColumnMajor_TopBottom_LeftRight,
        PageScalingMode scalingMode = PageScalingMode::OriginalScale,
        bool skipEmptySheets = true
    );
};

} // namespace Folio
