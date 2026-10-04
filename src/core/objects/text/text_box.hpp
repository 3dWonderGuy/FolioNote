#pragma once
/**
 * @file text_box.hpp
 * @brief Canvas text box object with 3-layer vector styling and headless editing integration.
 *
 * Combines 3-layer visual styling (Layer 1: Background/Sticky Note fill, Layer 2: Outline/Border,
 * Layer 3: Blend2D Vector Typography & Caret Pass) with the headless TextEditorState.
 *
 * Inactive State: Pure Blend2D vector shaping at native resolution.
 * Active State:   Blend2D renders selection boxes and blinking caret directly to canvas.
 * Zero ImGui dependencies inside this class.
 */

#include <string>
#include <vector>
#include <memory>
#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/objects/text/text_run.hpp"
#include "core/objects/primitives/shape_types.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

class TextEditorState;

enum class StickyPreset : uint8_t {
    Transparent = 0,
    PaleYellow,
    SoftBlue,
    SoftGreen,
    SoftPink,
    SubtleCharcoal
};

/**
 * @class TextBoxObject
 * @brief Canvas text box with rich runs, 3-layer visual container styling, and vector layout.
 */
class TextBoxObject : public CanvasObject {
public:
    // =========================================================================
    // LAYER 1 & LAYER 2: 3-LAYER CONTAINER STYLING (Reused from ShapeObject)
    // =========================================================================
    ShapeFillType fillType        = ShapeFillType::None;        ///< Transparent default
    ShapeOutlineType outlineType  = ShapeOutlineType::None;     ///< Invisible default unless selected/hovered
    BLRgba32 fillColor{0x00, 0x00, 0x00, 0x00};                 ///< Fill color (e.g. sticky note pastel)
    BLRgba32 strokeColor{0x5C, 0x8D, 0xD6, 0xFF};               ///< Border color
    double strokeWidth  = 0.4;                                  ///< Border thickness (world mm)
    double cornerRadius = 3.0;                                  ///< Fillet radius (world mm)
    StickyPreset currentPreset = StickyPreset::Transparent;

    // =========================================================================
    // LAYER 3: TYPOGRAPHY & CONTENT MODEL
    // =========================================================================
    std::string text = "";                                      ///< Plain text buffer
    std::string fontFamily = "Segoe UI";                        ///< Typographical font family
    float fontSize = 14.0f;                                     ///< Font size in points
    BLRgba32 textColor{0x1F, 0x29, 0x37, 0xFF};                 ///< Text glyph fill color (Dark graphite default)
    BLRgba32 highlightColor{0x00, 0x00, 0x00, 0x00};            ///< Background highlight color (alpha 0 = none)
    bool isBold          = false;                               ///< Bold typographical weight
    bool isItalic        = false;                               ///< Oblique/italic slant
    bool isUnderline     = false;                               ///< Underline line decoration
    bool isStrikethrough = false;                               ///< Center horizontal strike line
    uint8_t alignment    = 0;                                   ///< 0: Left, 1: Center, 2: Right
    std::vector<TextRun> runs;                                  ///< Rich styled runs

    // =========================================================================
    // INTERACTION & STATE
    // =========================================================================
    bool isEditing = false;            ///< True when headless text controller is active
    bool isHovered = false;            ///< True when mouse hovers over box
    bool isWrap    = true;             ///< True for word-wrapped text
    bool isDirty   = true;             ///< Needs layout reflow

    TextBoxObject();
    TextBoxObject(double x, double y, double w = 70.0, double h = 20.0);

    // =========================================================================
    // STYLING PRESETS
    // =========================================================================
    void ApplyStickyPreset(StickyPreset preset);

    // =========================================================================
    // RICH TEXT SPAN METRICS & HELPERS
    // =========================================================================
    struct FormattedSpan {
        std::string text;           ///< Slice of text within this span
        const TextRun* run = nullptr;///< Pointer to active styling run (or nullptr for defaults)
        double width = 0.0;         ///< Advance width in world millimeters
    };

    /**
     * @brief Computes list of formatted styled spans covering a character range [rangeStart, rangeEnd).
     * @param rangeStart Zero-indexed starting byte offset into PlainText().
     * @param rangeEnd   Zero-indexed ending byte offset into PlainText().
     * @return Vector of FormattedSpans with individual widths and run references.
     */
    [[nodiscard]] std::vector<FormattedSpan> GetSpansForRange(size_t rangeStart, size_t rangeEnd) const;

    /**
     * @brief Measures aggregate advance width of a character range across all overlapping styled runs.
     * @param rangeStart Zero-indexed starting byte offset.
     * @param rangeEnd   Zero-indexed ending byte offset.
     * @return Cumulative advance width in world millimeters.
     */
    [[nodiscard]] double MeasureRange(size_t rangeStart, size_t rangeEnd) const;

    /**
     * @brief Computes typographical line metrics (ascent and total line height) for a range across runs.
     * @param rangeStart Zero-indexed starting byte offset.
     * @param rangeEnd   Zero-indexed ending byte offset.
     * @param[out] outAscent Maximum font ascent among overlapping runs (mm).
     * @param[out] outLineHeight Maximum line height among overlapping runs (mm).
     */
    void GetLineMetricsForRange(size_t rangeStart, size_t rangeEnd, double& outAscent, double& outLineHeight) const;

    // =========================================================================
    // CONTENT HELPERS
    // =========================================================================
    [[nodiscard]] std::string PlainText() const;
    void SyncTextToRuns();
    void CompactRuns();
    [[nodiscard]] size_t CharCount() const noexcept;

    // =========================================================================
    // CANVAS OBJECT CONTRACT
    // =========================================================================
    void UpdateBounds() override;
    bool HitTest(double wx, double wy) const override;
    void ApplyTransform(const BLMatrix2D& matrix) override;
    void BakeTransform() override;

    void Render(BLContext& ctx, const Viewport& viewport) const override;

    /**
     * @brief Custom render pass when active editing controller is attached.
     */
    void RenderWithEditor(BLContext& ctx, const Viewport& viewport, const TextEditorState& editor) const;

    std::unique_ptr<CanvasObject> Clone() const override;
};

} // namespace Folio
