#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <blend2d/blend2d.h>

#include "core/spatial/aabb.hpp"
#include "core/ink_engine/stroke_smoother.hpp"

namespace Folio {

enum class MdBlockType : uint8_t {
    Paragraph = 0,
    Heading,
    BulletList,
    NumberedList,
    TaskList,
    Blockquote,
    CodeBlock,
    Table,
    MathBlock,
    HorizontalRule
};

enum class MdSpanType : uint8_t {
    Text = 0,
    Bold,
    Italic,
    BoldItalic,
    Strikethrough,
    Code,
    Link,
    Highlight,
    MathInline
};

struct MdSpan {
    MdSpanType type = MdSpanType::Text;
    std::string text;
    std::string targetUrl;
    float sizePt = 14.0f;
    double widthMm = 0.0;
    double heightMm = 0.0;
    size_t sourceOffset = 0;
    size_t sourceLength = 0;
};

struct MdTable {
    std::vector<std::string> headers;
    std::vector<uint8_t> alignments; // 0: Left, 1: Center, 2: Right
    std::vector<std::vector<std::string>> rows;
    std::vector<double> colWidthsMm;
};

struct MdBlock {
    MdBlockType type = MdBlockType::Paragraph;
    int level = 1;                     ///< Heading level (1-6) or list nesting indent
    bool checked = false;              ///< Task list checkbox state: [x] vs [ ]
    int listIndex = 1;                 ///< Numbered list item counter (1, 2, 3...)
    std::string language;              ///< Fenced code block syntax tag (e.g. "cpp", "python")
    std::string codeContent;           ///< Fenced code block or MathBlock raw text
    std::vector<MdSpan> spans;         ///< Rich styled inline spans
    MdTable table;                     ///< Tabular data grid (if type == Table)

    size_t sourceStartOffset = 0;      ///< Byte offset in raw Markdown document
    size_t sourceEndOffset = 0;

    // Layout metrics (calculated by MdLayoutEngine in physical mm)
    double localY_mm = 0.0;
    double height_mm = 0.0;
    double width_mm = 0.0;
};

struct MdSelection {
    size_t anchorOffset = 0;
    size_t cursorOffset = 0;
    bool hasSelection = false;

    void Clear() noexcept {
        anchorOffset = cursorOffset;
        hasSelection = false;
    }

    [[nodiscard]] size_t MinOffset() const noexcept {
        return std::min(anchorOffset, cursorOffset);
    }

    [[nodiscard]] size_t MaxOffset() const noexcept {
        return std::max(anchorOffset, cursorOffset);
    }
};

struct MdCaret {
    size_t offset = 0;
    int line = 0;
    int column = 0;
    Point2D worldPosMm{ 0.0, 0.0 };
    double heightMm = 5.0;
    bool visible = true;
};

struct MdStyleConfig {
    std::string bodyFontFamily = "Segoe UI";
    std::string codeFontFamily = "Consolas";
    std::string mathFontFamily = "Cambria Math";

    float bodyFontSizePt = 14.0f;
    float h1FontSizePt   = 28.0f;
    float h2FontSizePt   = 22.0f;
    float h3FontSizePt   = 18.0f;
    float h4FontSizePt   = 16.0f;
    float codeFontSizePt = 13.0f;

    float lineHeightMultiplier = 1.45f;
    double paragraphSpacingMm  = 3.5;
    double headingSpacingTopMm = 5.0;
    double headingSpacingBottomMm = 2.5;
    double blockquoteIndentMm = 6.0;
    double listIndentMm = 6.0;

    // Physical Page Geometry
    double pageWidthMm  = 210.0; // A4 Standard
    double pageHeightMm = 297.0;
    double marginLeftMm = 20.0;
    double marginRightMm = 20.0;
    double marginTopMm = 20.0;
    double marginBottomMm = 20.0;

    // Theme Palette
    BLRgba32 textColor{ 0x1E, 0x22, 0x2B, 0xFF };
    BLRgba32 headingColor{ 0x0F, 0x17, 0x2A, 0xFF };
    BLRgba32 linkColor{ 0x25, 0x63, 0xEB, 0xFF };
    BLRgba32 codeBgColor{ 0xF1, 0xF5, 0xF9, 0xFF };
    BLRgba32 codeTextColor{ 0x0F, 0x17, 0x2A, 0xFF };
    BLRgba32 codeBorderColor{ 0xE2, 0xE8, 0xF0, 0xFF };
    BLRgba32 quoteBorderColor{ 0x94, 0xA3, 0xB8, 0xFF };
    BLRgba32 quoteBgColor{ 0xF8, 0xFA, 0xFC, 0x88 };
    BLRgba32 hrColor{ 0xE2, 0xE8, 0xF0, 0xFF };
    BLRgba32 highlightBgColor{ 0xFE, 0xF0, 0x8A, 0xC0 }; // Pastel yellow
    BLRgba32 tableBorderColor{ 0xCB, 0xD5, 0xE1, 0xFF };
    BLRgba32 tableHeaderBgColor{ 0xF1, 0xF5, 0xF9, 0xFF };
    BLRgba32 selectionColor{ 0xBF, 0xDB, 0xFE, 0x80 }; // Soft blue translucent selection

    [[nodiscard]] double ContentWidthMm() const noexcept {
        return std::max(20.0, pageWidthMm - marginLeftMm - marginRightMm);
    }
};

} // namespace Folio
