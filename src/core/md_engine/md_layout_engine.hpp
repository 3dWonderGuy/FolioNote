#pragma once

#include <vector>
#include <memory>
#include <string>
#include "core/md_engine/md_types.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @struct MdLayoutLine
 * @brief Represents a single wrapped line within a block.
 */
struct MdLayoutLine {
    std::vector<MdSpan> spans;
    double localX_mm = 0.0;
    double localY_mm = 0.0;
    double width_mm = 0.0;
    double height_mm = 0.0;
    double baselineY_mm = 0.0;
    size_t startOffset = 0;
    size_t endOffset = 0;
};

/**
 * @class MdLayoutEngine
 * @brief Typesetting and physical millimeter layout engine for Markdown documents.
 * Handles font metrics, line breaking, table sizing, and caret/selection geometry.
 */
class MdLayoutEngine {
public:
    /**
     * @brief Computes physical millimeter layout for all blocks in the document.
     * @param blocks Blocks to measure and lay out.
     * @param config Document typography and geometry configuration.
     * @return Total document height in millimeters.
     */
    static double LayoutDocument(std::vector<MdBlock>& blocks, const MdStyleConfig& config);

    /**
     * @brief Performs hit-testing from physical millimeters to document character offset.
     * @param localX_mm Local X coordinate from page left in millimeters.
     * @param localY_mm Local Y coordinate from page top in millimeters.
     * @param blocks Formatted blocks.
     * @param config Layout configuration.
     * @return Closest character offset in the raw Markdown document.
     */
    [[nodiscard]] static size_t HitTestOffset(
        double localX_mm,
        double localY_mm,
        const std::vector<MdBlock>& blocks,
        const MdStyleConfig& config
    );

    /**
     * @brief Resolves physical millimeter position of the caret for a given character offset.
     * @param offset Document character offset.
     * @param blocks Formatted blocks.
     * @param config Layout configuration.
     * @return MdCaret with physical world coordinates and height in millimeters.
     */
    [[nodiscard]] static MdCaret GetCaretForOffset(
        size_t offset,
        const std::vector<MdBlock>& blocks,
        const MdStyleConfig& config
    );

    /**
     * @brief Calculates physical millimeter selection bounding boxes for an offset range.
     * @param startOffset Start character offset.
     * @param endOffset End character offset.
     * @param blocks Formatted blocks.
     * @param config Layout configuration.
     * @return List of AABB bounding rectangles in physical millimeters.
     */
    [[nodiscard]] static std::vector<AABB> GetSelectionBoxes(
        size_t startOffset,
        size_t endOffset,
        const std::vector<MdBlock>& blocks,
        const MdStyleConfig& config
    );
};

} // namespace Folio
