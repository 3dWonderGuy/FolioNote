#pragma once
/**
 * @file text_run.hpp
 * @brief A single styled text span within a TextBoxObject's rich content model.
 *
 * TextRun is the atomic unit of rich text in FolioNote. A TextBoxObject stores
 * a vector<TextRun> where each element defines one contiguous styled region.
 * Adjacent runs with identical styling should be merged for efficiency.
 *
 * Phase 2 Rich Text Implementation Plan:
 * ─────────────────────────────────────────────────────────────────────────────
 * The rich text system operates on two layers:
 *
 *   LIVE LAYER (while isEditing == true):
 *     Re-rendered every frame at current zoom. Uses Blend2D BLFont with the
 *     platform font stack (fontFamily matched against system fonts via
 *     FreeType / DirectWrite / CoreText). Zoom-aware: fontSize * canvasZoom
 *     gives screen pixel size so text appears at its intended physical size.
 *
 *   BAKED LAYER (while isEditing == false):
 *     A BLImage snapshot rendered at the zoom level at time of baking (or at
 *     a fixed reference DPI). The engine draws the cached BLImage as a bitmap
 *     (fast, no font loading). On next selection+edit, the live layer resumes.
 *     The baked image is a selectable, moveable object — functionally identical
 *     to ImageObject once baked.
 *
 * Inline Link Reference:
 *   linkRef stores the UUID of a canvas LinkObject. The text editor draws
 *   runs with a non-empty linkRef in accent color with underline, and a single
 *   click in Phase 2 will call LinkObject::Navigate().
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Scalability:
 *   Add new style attributes (superscript, subscript, custom baseline offset,
 *   font stretch, language code for hyphenation) by extending this struct.
 *   The text layout engine reads all fields — no other files need modification.
 */

#include <string>
#include <blend2d/blend2d.h>

namespace Folio {

/**
 * @brief One contiguous styled text span in a rich text document.
 *
 * Multiple TextRun objects are stored in sequence inside TextBoxObject::runs.
 * The visual result is produced by iterating runs in order and laying out
 * each span with its specified style.
 *
 * Example (bold "Hello" followed by plain " world"):
 *   runs[0] = { text="Hello", bold=true,  fontSize=16 }
 *   runs[1] = { text=" world", bold=false, fontSize=16 }
 */
struct TextRun {
    // =========================================================================
    // CONTENT
    // =========================================================================

    std::string text;           ///< Raw UTF-8 text content of this span

    // =========================================================================
    // FONT PROPERTIES
    // =========================================================================

    std::string fontFamily = "Segoe UI"; ///< System font family name
    float       fontSize   = 16.0f;      ///< Font size in canvas world points (not pixels)

    // =========================================================================
    // STYLE FLAGS
    // =========================================================================

    bool bold          = false;  ///< Bold weight
    bool italic        = false;  ///< Italic style
    bool underline     = false;  ///< Underline decoration
    bool strikethrough = false;  ///< Strikethrough decoration
    bool superscript   = false;  ///< Raised superscript (Phase 2)
    bool subscript     = false;  ///< Lowered subscript (Phase 2)

    // =========================================================================
    // COLORS
    // =========================================================================

    BLRgba32 color{0xFF, 0xFF, 0xFF, 0xFF};           ///< Text color (default white)
    BLRgba32 highlightColor{0x00, 0x00, 0x00, 0x00};  ///< Highlight fill (alpha=0 = none)

    // =========================================================================
    // INLINE LINK REFERENCE
    // =========================================================================

    /**
     * @brief UUID of a canvas LinkObject this run links to.
     *
     * If empty: plain text, no hyperlink behavior.
     * If non-empty: text renderer draws this run in accent color + underline,
     *   and a click triggers LinkObject::Navigate() on the referenced object.
     *
     * The LinkObject lives in the canvas object list (same Section/Page).
     * Its guuid must match this string exactly.
     */
    std::string linkRef = "";

    // =========================================================================
    // HELPERS
    // =========================================================================

    /**
     * @brief Returns true if this run has no visible inline link.
     */
    [[nodiscard]] bool IsLinked() const noexcept {
        return !linkRef.empty();
    }

    /**
     * @brief Returns true if this run and another have identical styling.
     *
     * Used for merging adjacent runs during text editing to keep the run
     * list compact. Content (text) is not compared — only style attributes.
     */
    [[nodiscard]] bool SameStyleAs(const TextRun& other) const noexcept {
        return fontFamily     == other.fontFamily
            && fontSize       == other.fontSize
            && bold           == other.bold
            && italic         == other.italic
            && underline      == other.underline
            && strikethrough  == other.strikethrough
            && superscript    == other.superscript
            && subscript      == other.subscript
            && color.value    == other.color.value
            && highlightColor.value == other.highlightColor.value
            && linkRef        == other.linkRef;
    }
};

} // namespace Folio
