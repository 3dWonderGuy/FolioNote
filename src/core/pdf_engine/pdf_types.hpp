#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <blend2d/blend2d.h>
#include "core/spatial/aabb.hpp"
#include "imgui.h"

namespace Folio {

/**
 * @brief Clickable hyperlinked rectangular region on a PDF page.
 */
struct PdfPageLink {
    double minX_mm = 0.0;
    double minY_mm = 0.0;
    double maxX_mm = 0.0;
    double maxY_mm = 0.0;
    int targetPageIndex = -1;
    std::string uri;
};

/**
 * @brief Result of rasterizing a single PDF page.
 */
struct PdfPageRenderResult {
    bool success = false;
    BLImage image;
    double widthMm = 210.0;
    double heightMm = 297.0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    std::vector<PdfPageLink> links;
    std::string errorMessage;
};

/**
 * @brief Hierarchical table-of-contents / outline entry in a PDF document.
 */
struct PdfOutlineItem {
    std::string title;
    int pageIndex = -1;
    std::vector<PdfOutlineItem> children;
};

/**
 * @brief Physical millimeter dimensions of a single PDF page.
 */
struct PdfPageDimension {
    double widthMm = 210.0;
    double heightMm = 297.0;
};

/**
 * @brief User-created custom bookmark referencing a page index with a custom title.
 */
struct PdfUserBookmark {
    int pageIndex = 0;
    std::string title;
};

/**
 * @brief Character glyph box and Unicode codepoint metadata.
 */
struct PdfCharInfo {
    int charIndex = 0;
    uint32_t unicode = 0;
    double left = 0.0;    // in mm
    double top = 0.0;     // in mm
    double right = 0.0;   // in mm
    double bottom = 0.0;  // in mm
    double fontSize = 12.0;
};

/**
 * @brief Active selection range in a PDF page's text stream.
 */
struct PdfTextSelection {
    int startChar = -1;
    int endChar = -1;
    bool hasSelection = false;

    void Clear() noexcept {
        startChar = -1;
        endChar = -1;
        hasSelection = false;
    }

    [[nodiscard]] int MinIdx() const noexcept { return std::min(startChar, endChar); }
    [[nodiscard]] int MaxIdx() const noexcept { return std::max(startChar, endChar); }
};

/**
 * @brief Logical reading line grouping sequential character glyphs.
 */
struct PdfTextLine {
    int startChar = 0;
    int endChar = 0;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

/**
 * @brief Persistent text highlight span overlaid on a PDF page.
 */
struct TextHighlightSpan {
    AABB boundsMm;
    ImU32 color = IM_COL32(255, 235, 59, 115);
    int startChar = -1;
    int endChar = -1;
    std::string text;
    int pageIndex = 0;
};

/**
 * @brief Full structural summary of a PDF document obtained via single-pass inspection.
 */
struct PdfDocSummary {
    int pageCount = 0;
    std::vector<std::pair<double, double>> dimensions; ///< (widthMm, heightMm)
    std::vector<PdfOutlineItem> outline;
};

/**
 * @brief Curated digital highlighter color preset.
 */
struct PdfHighlightColorPreset {
    const char* name;
    ImU32 lightColor;
    ImU32 darkColor;
    ImVec4 swatch;
};

/**
 * @brief Standard curated palette of digital notebook highlighter colors.
 */
inline const std::vector<PdfHighlightColorPreset>& GetHighlightColorPresets() {
    static const std::vector<PdfHighlightColorPreset> s_presets = {
        { "Sunshine Yellow", IM_COL32(255, 235, 59, 115),  IM_COL32(255, 235, 59, 130),  ImVec4(1.00f, 0.92f, 0.23f, 1.0f) },
        { "Neon Green",      IM_COL32(76, 217, 100, 115),  IM_COL32(76, 217, 100, 130),  ImVec4(0.30f, 0.85f, 0.39f, 1.0f) },
        { "Sky Blue",        IM_COL32(33, 150, 243, 115),  IM_COL32(0, 210, 255, 130),   ImVec4(0.13f, 0.59f, 0.95f, 1.0f) },
        { "Rose Pink",       IM_COL32(255, 64, 129, 115),  IM_COL32(255, 64, 129, 130),  ImVec4(1.00f, 0.25f, 0.51f, 1.0f) },
        { "Warm Orange",     IM_COL32(255, 152, 0, 115),   IM_COL32(255, 152, 0, 130),   ImVec4(1.00f, 0.60f, 0.00f, 1.0f) },
        { "Lavender Purple", IM_COL32(171, 71, 188, 115),  IM_COL32(186, 104, 200, 130), ImVec4(0.67f, 0.28f, 0.74f, 1.0f) }
    };
    return s_presets;
}

enum class PdfSidebarTab : uint8_t {
    Thumbnails = 0,
    Outline,
    Bookmarks
};

enum class PdfToolMode : uint8_t {
    Highlight = 0,    // Text-snapping highlighter
    Select,           // Text selection (copy, quote to notes, export)
    Eraser,           // Stroke and text highlight eraser
    Pen,              // Freehand ink drawing with active pen preset
    FreeHighlight     // Freehand highlighter ink drawing
};

} // namespace Folio
