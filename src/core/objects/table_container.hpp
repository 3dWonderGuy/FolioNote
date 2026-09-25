#pragma once
/**
 * @file table_container.hpp
 * @brief Placeholder stub for future canvas table container.
 *
 * TableObject will represent a structured table (rows × columns) on the canvas.
 * Each cell can contain text, images, or other inline content.
 *
 * This stub exists to:
 *  1. Reserve the ObjectType::Table enum slot
 *  2. Document the planned data model for Phase 2+
 *  3. Allow the engine to construct an empty TableObject without crashing if
 *     the serializer encounters a Table object in a saved file
 *
 * PHASE 2 PLANNED IMPLEMENTATION:
 * ─────────────────────────────────────────────────────────────────────────────
 *  struct TableCell {
 *      std::string content;              // Plain or rich text
 *      BLRgba32 backgroundColor;         // Cell fill
 *      BLRgba32 borderColor;             // Cell border
 *      TextAlignment alignment = Center;
 *      std::vector<TextRun> richRuns;    // Phase 3: rich text per cell
 *  };
 *
 *  class TableObject : public CanvasObject {
 *      int rows = 3, cols = 3;
 *      std::vector<TableCell> cells;     // row-major: cells[row * cols + col]
 *      std::vector<double> colWidths;    // Column widths in world mm
 *      std::vector<double> rowHeights;   // Row heights in world mm
 *      BLRgba32 headerColor;             // First row distinct color
 *      bool hasHeader = true;
 *      // Resizable: user drags row/column separators
 *      // Gizmo: custom handles at each column/row divider
 *  };
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <string>
#include <memory>
#include <vector>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @brief Unimplemented placeholder that satisfies the CanvasObject interface.
 *
 * If a serialized file contains an ObjectType::Table entry, the engine will
 * construct this stub and skip rendering. No crash, no data loss.
 */
class TableObject : public CanvasObject {
public:
    int    rows = 3, cols = 3;

    TableObject() {
        type = ObjectType::Table;
        worldWidth = 80.0;
        worldHeight = 50.0;
        UpdateBounds();
    }

    void Render(BLContext& ctx, const Viewport& /*viewport*/) const override {
        if (!isVisible) return;
        // Draw a placeholder grey grid so tables are visible but clearly un-implemented
        ctx.save();
        ctx.apply_transform(transform);
        ctx.set_fill_style(BLRgba32(0x1E, 0x22, 0x2A, 200));
        ctx.fill_rect(worldX, worldY, worldWidth, worldHeight);
        ctx.set_stroke_style(BLRgba32(0x4A, 0x50, 0x60, 200));
        ctx.set_stroke_width(0.5);
        ctx.stroke_rect(worldX, worldY, worldWidth, worldHeight);
        // Simple grid lines (3×3)
        double cw = worldWidth / cols, rh = worldHeight / rows;
        for (int c = 1; c < cols; ++c)
            ctx.stroke_line(worldX + c * cw, worldY, worldX + c * cw, worldY + worldHeight);
        for (int r = 1; r < rows; ++r)
            ctx.stroke_line(worldX, worldY + r * rh, worldX + worldWidth, worldY + r * rh);
        ctx.restore();
    }

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<TableObject>(*this);
    }
};

} // namespace Folio
