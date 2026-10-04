#pragma once

#include <string>
#include <vector>
#include "core/spatial/aabb.hpp"
#include "core/pdf_engine/pdf_types.hpp"

namespace Folio {

/**
 * @brief Manages the extracted text stream, reading lines, character layout boxes,
 * and text selection ranges for a single PDF page.
 */
class PdfTextLayer {
public:
    std::vector<PdfCharInfo> chars;
    std::vector<PdfTextLine> lines;
    double pageWidthMm = 210.0;
    double pageHeightMm = 297.0;
    double medianFontSize = 12.0;

    void Clear();

    /**
     * @brief Extracts text, glyph bounding boxes, font sizes, and line groupings
     * directly from a PDFium FPDF_PAGE handle.
     * @param fpdfPagePtr Opaque pointer to FPDF_PAGE.
     * @return true if text was found and loaded successfully.
     */
    bool LoadFromPage(void* fpdfPagePtr);

    /**
     * @brief Tests if a point (localX, localY in mm) hits a character glyph box.
     */
    [[nodiscard]] int HitTestChar(double localX, double localY, double toleranceMm = 3.0) const;

    /**
     * @brief Robust line-aware character finder for drag selection.
     * Snaps to the closest reading line and clamps to line bounds in margins without erratic jumping.
     */
    [[nodiscard]] int FindNearestChar(double localX, double localY) const;

    /**
     * @brief Extracts bounding box rectangles for all selected characters.
     */
    [[nodiscard]] std::vector<AABB> GetSelectionBoxes(const PdfTextSelection& sel) const;

    /**
     * @brief Returns selected text as clean UTF-8 string.
     */
    [[nodiscard]] std::string GetSelectedText(const PdfTextSelection& sel) const;

    /**
     * @brief Reconstructs structured Markdown formatting from selected text,
     * detecting headings from font scale, lists from bullet characters, and paragraphs.
     */
    [[nodiscard]] std::string GetSelectedMarkdown(const PdfTextSelection& sel) const;
};

} // namespace Folio
