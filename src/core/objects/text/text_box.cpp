/**
 * @file text_box.cpp
 * @brief Implementation of TextBoxObject with 3-layer styling and Blend2D vector typography.
 *
 * GENERAL WORKING PROCESS:
 * ------------------------
 * 1. Layer 1 (Infill): Renders rounded container background with solid or semi-transparent
 *    pastel washes (Sticky Note presets) or transparent canvas notes.
 * 2. Layer 2 (Border & Grip Chrome): When hovered or editing, renders a subtle top grab handle
 *    bar (OneNote style) allowing the user to reposition notes effortlessly on canvas.
 * 3. Layer 3 (Vector Typography): Resolves crisp typography via FontManager and draws text
 *    runs using native sub-pixel glyph rasterization. When actively editing, draws selection
 *    highlight rectangles and the synchronized blinking caret.
 *
 * MATHEMATICAL FOUNDATIONS:
 * -------------------------
 * - Typographical Baseline Positioning:
 *     Baseline = worldY + localLineY + ascent
 * - Stroke Scaling Invariance:
 *     strokeW = baseStrokeW / sqrt(det(transform))
 *     Prevents line fattening or thinning during active canvas zooms.
 */

#include "core/objects/text/text_box.hpp"
#include "core/objects/text/text_editor_state.hpp"
#include "core/text/font_manager.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Folio {

TextBoxObject::TextBoxObject() {
    type = ObjectType::Text;
    worldWidth = 70.0;
    worldHeight = 20.0;
    TextRun r;
    r.color = textColor;
    r.fontSize = fontSize;
    r.fontFamily = fontFamily;
    runs.push_back(r);
    UpdateBounds();
}

TextBoxObject::TextBoxObject(double x, double y, double w, double h) {
    type = ObjectType::Text;
    worldX = x;
    worldY = y;
    worldWidth = w;
    worldHeight = h;
    TextRun r;
    r.color = textColor;
    r.fontSize = fontSize;
    r.fontFamily = fontFamily;
    runs.push_back(r);
    UpdateBounds();
}

void TextBoxObject::ApplyStickyPreset(StickyPreset preset) {
    currentPreset = preset;
    switch (preset) {
        case StickyPreset::Transparent:
            fillType = ShapeFillType::None;
            outlineType = ShapeOutlineType::None;
            textColor = BLRgba32(0x1F, 0x29, 0x37, 0xFF); // Dark graphite
            break;

        case StickyPreset::PaleYellow:
            fillType = ShapeFillType::Solid;
            fillColor = BLRgba32(0xFE, 0xF9, 0xC3, 0xF5);       // Soft pastel yellow
            strokeColor = BLRgba32(0xCA, 0x8A, 0x04, 0xC0);     // Subtle amber border
            outlineType = ShapeOutlineType::Solid;
            textColor = BLRgba32(0x1F, 0x29, 0x37, 0xFF);       // Dark graphite text
            strokeWidth = 0.5;
            cornerRadius = 4.0;
            break;

        case StickyPreset::SoftBlue:
            fillType = ShapeFillType::Solid;
            fillColor = BLRgba32(0xE0, 0xF2, 0xFE, 0xF5);       // Soft pastel sky blue
            strokeColor = BLRgba32(0x02, 0x84, 0xC7, 0xC0);     // Cyan-blue border
            outlineType = ShapeOutlineType::Solid;
            textColor = BLRgba32(0x0C, 0x4A, 0x6E, 0xFF);       // Dark navy text
            strokeWidth = 0.5;
            cornerRadius = 4.0;
            break;

        case StickyPreset::SoftGreen:
            fillType = ShapeFillType::Solid;
            fillColor = BLRgba32(0xDC, 0xFC, 0xE7, 0xF5);       // Soft pastel mint
            strokeColor = BLRgba32(0x16, 0xA3, 0x4A, 0xC0);     // Emerald border
            outlineType = ShapeOutlineType::Solid;
            textColor = BLRgba32(0x14, 0x53, 0x2D, 0xFF);       // Dark forest text
            strokeWidth = 0.5;
            cornerRadius = 4.0;
            break;

        case StickyPreset::SoftPink:
            fillType = ShapeFillType::Solid;
            fillColor = BLRgba32(0xFC, 0xE7, 0xF3, 0xF5);       // Soft pastel rose
            strokeColor = BLRgba32(0xDB, 0x27, 0x77, 0xC0);     // Rose-pink border
            outlineType = ShapeOutlineType::Solid;
            textColor = BLRgba32(0x83, 0x18, 0x43, 0xFF);       // Dark berry text
            strokeWidth = 0.5;
            cornerRadius = 4.0;
            break;

        case StickyPreset::SubtleCharcoal:
            fillType = ShapeFillType::Solid;
            fillColor = BLRgba32(0x1E, 0x23, 0x2A, 0xF5);       // Dark slate
            strokeColor = BLRgba32(0x4C, 0x56, 0x6A, 0xC0);     // Muted steel border
            outlineType = ShapeOutlineType::Solid;
            textColor = BLRgba32(0xEC, 0xEF, 0xF4, 0xFF);       // Crisp off-white text
            strokeWidth = 0.5;
            cornerRadius = 4.0;
            break;
    }
    SyncTextToRuns();
    isDirty = true;
}

std::string TextBoxObject::PlainText() const {
    if (!text.empty()) return text;
    std::string result;
    for (const auto& r : runs) result += r.text;
    return result;
}

void TextBoxObject::SyncTextToRuns() {
    if (runs.empty()) {
        TextRun r;
        r.text = text;
        r.fontFamily = fontFamily;
        r.fontSize = fontSize;
        r.color = textColor;
        r.bold = isBold;
        r.italic = isItalic;
        r.underline = isUnderline;
        r.strikethrough = isStrikethrough;
        r.highlightColor = highlightColor;
        runs.push_back(r);
    } else if (runs.size() == 1) {
        runs[0].text = text;
        runs[0].fontFamily = fontFamily;
        runs[0].fontSize = fontSize;
        runs[0].color = textColor;
        runs[0].bold = isBold;
        runs[0].italic = isItalic;
        runs[0].underline = isUnderline;
        runs[0].strikethrough = isStrikethrough;
        runs[0].highlightColor = highlightColor;
    } else {
        // Multi-run container: ensure PlainText() matches text
        if (text.empty()) {
            text = PlainText();
        } else {
            std::string runText;
            for (const auto& r : runs) runText += r.text;
            if (runText != text) {
                // Text was edited globally in editor: keep styling on primary run
                runs[0].text = text;
                runs.resize(1);
            }
        }
    }
}

std::vector<TextBoxObject::FormattedSpan> TextBoxObject::GetSpansForRange(size_t rangeStart, size_t rangeEnd) const {
    std::vector<FormattedSpan> spans;
    if (rangeStart >= rangeEnd) return spans;

    std::string full = PlainText();
    if (full.empty()) return spans;
    rangeStart = (std::min)(rangeStart, full.size());
    rangeEnd = (std::min)(rangeEnd, full.size());
    if (rangeStart >= rangeEnd) return spans;

    if (runs.empty()) {
        FormattedSpan s;
        s.text = full.substr(rangeStart, rangeEnd - rangeStart);
        s.run = nullptr;
        BLFont f = FontManager::Instance().GetFont(fontFamily, fontSize, isBold, isItalic);
        s.width = FontManager::Instance().MeasureTextWidth(f, s.text);
        spans.push_back(s);
        return spans;
    }

    size_t curRunStart = 0;
    for (const auto& r : runs) {
        size_t curRunEnd = curRunStart + r.text.size();
        if (rangeEnd <= curRunStart) break;
        if (rangeStart < curRunEnd) {
            size_t s = (std::max)(rangeStart, curRunStart);
            size_t e = (std::min)(rangeEnd, curRunEnd);
            FormattedSpan span;
            span.text = r.text.substr(s - curRunStart, e - s);
            span.run = &r;
            if (r.elementType == InlineElementType::TextRun) {
                BLFont rf = FontManager::Instance().GetFont(r.fontFamily, r.fontSize, r.bold, r.italic);
                span.width = FontManager::Instance().MeasureTextWidth(rf, span.text);
            } else {
                span.width = r.inlineWidth;
            }
            spans.push_back(span);
        }
        curRunStart = curRunEnd;
    }

    if (spans.empty()) {
        FormattedSpan s;
        s.text = full.substr(rangeStart, rangeEnd - rangeStart);
        s.run = nullptr;
        BLFont f = FontManager::Instance().GetFont(fontFamily, fontSize, isBold, isItalic);
        s.width = FontManager::Instance().MeasureTextWidth(f, s.text);
        spans.push_back(s);
    }
    return spans;
}

double TextBoxObject::MeasureRange(size_t rangeStart, size_t rangeEnd) const {
    auto spans = GetSpansForRange(rangeStart, rangeEnd);
    double totalW = 0.0;
    for (const auto& s : spans) {
        totalW += s.width;
    }
    return totalW;
}

void TextBoxObject::GetLineMetricsForRange(size_t rangeStart, size_t rangeEnd, double& outAscent, double& outLineHeight) const {
    auto spans = GetSpansForRange(rangeStart, rangeEnd);
    double maxAscent = 0.0;
    double maxHeight = 0.0;

    for (const auto& s : spans) {
        if (s.run && s.run->elementType == InlineElementType::TextRun) {
            BLFont f = FontManager::Instance().GetFont(s.run->fontFamily, s.run->fontSize, s.run->bold, s.run->italic);
            BLFontMetrics fm = FontManager::Instance().GetMetrics(f);
            double h = fm.ascent + fm.descent + fm.line_gap;
            if (h <= 0.001) h = s.run->fontSize * 1.25;
            double a = (fm.ascent > 0.001) ? fm.ascent : (s.run->fontSize * 0.9);
            maxAscent = (std::max)(maxAscent, a);
            maxHeight = (std::max)(maxHeight, h);
        } else if (s.run) {
            maxAscent = (std::max)(maxAscent, s.run->inlineHeight);
            maxHeight = (std::max)(maxHeight, s.run->inlineHeight);
        }
    }

    if (maxHeight <= 0.001) {
        BLFont f = FontManager::Instance().GetFont(fontFamily, fontSize, isBold, isItalic);
        BLFontMetrics fm = FontManager::Instance().GetMetrics(f);
        maxHeight = fm.ascent + fm.descent + fm.line_gap;
        if (maxHeight <= 0.001) maxHeight = fontSize * 1.25;
        maxAscent = (fm.ascent > 0.001) ? fm.ascent : (fontSize * 0.9);
    }

    outAscent = maxAscent;
    outLineHeight = maxHeight;
}

void TextBoxObject::CompactRuns() {
    if (runs.size() < 2) return;
    std::vector<TextRun> compacted;
    compacted.push_back(runs[0]);
    for (size_t i = 1; i < runs.size(); ++i) {
        if (!runs[i].text.empty() && compacted.back().SameStyleAs(runs[i])) {
            compacted.back().text += runs[i].text;
        } else if (!runs[i].text.empty()) {
            compacted.push_back(runs[i]);
        }
    }
    runs = std::move(compacted);
}

size_t TextBoxObject::CharCount() const noexcept {
    return text.size();
}

void TextBoxObject::UpdateBounds() {
    bounds = Folio::AABBUtils::ComputeTransformedBounds(worldX, worldY, worldWidth, worldHeight, transform);
}

/**
 * @brief Evaluates whether a world-space point intersects the text box or its top grab bar.
 * 
 * Working Process:
 *   1. Check visibility & selectability: Rejects hidden or non-selectable text boxes.
 *   2. Expand bounding box vertically by 4.0mm at the top (hitBox.minY -= 4.0) to account
 *      for the interactive text drag handle / grab bar.
 *   3. Containment test: If (wx, wy) falls inside the expanded hitBox:
 *      - If already selected (isSelected == true), returns true immediately to support
 *        frictionless double-click text editing and drag operations.
 *      - Otherwise returns true as an interior hit.
 * 
 * @param wx World X coordinate in mm.
 * @param wy World Y coordinate in mm.
 * @return True if (wx, wy) hits the text box or grab handle.
 */
bool TextBoxObject::HitTest(double wx, double wy) const {
    if (!isVisible || !isSelectable) return false;

    // Include 4mm top grab bar area when testing for hit
    AABB hitBox = bounds;
    hitBox.minY -= 4.0;
    if (!hitBox.Contains(wx, wy)) return false;

    // Fast-path: already selected text box allows immediate interaction
    if (isSelected) return true;

    return true;
}


void TextBoxObject::ApplyTransform(const BLMatrix2D& matrix) {
    transform.post_transform(matrix);
    UpdateBounds();
}

void TextBoxObject::BakeTransform() {
    if (Folio::AABBUtils::BakeTransformedRect(worldX, worldY, worldWidth, worldHeight, transform, 10.0)) {
        isDirty = true;
        UpdateBounds();
    }
}

void TextBoxObject::Render(BLContext& ctx, const Viewport& viewport) const {
    if (!isVisible) return;

    ctx.save();
    ctx.apply_transform(transform);

    // =========================================================================
    // LAYER 1: CONTAINER INFILL (Sticky Note wash or Transparent)
    // =========================================================================
    if (fillType != ShapeFillType::None) {
        BLRoundRect rr(worldX, worldY, worldWidth, worldHeight, cornerRadius, cornerRadius);
        ctx.set_fill_style(fillColor);
        ctx.fill_round_rect(rr);
    }

    // =========================================================================
    // LAYER 2: BORDER & ONENOTE CONTAINER HEADER
    // =========================================================================
    double det = std::abs(transform.m00 * transform.m11 - transform.m01 * transform.m10);
    double scale = (det > 1e-6) ? std::sqrt(det) : 1.0;
    double invScale = (scale > 1e-4) ? (1.0 / scale) : 1.0;

    if (outlineType == ShapeOutlineType::Solid) {
        ctx.set_stroke_style(strokeColor);
        ctx.set_stroke_width(strokeWidth * invScale);
        BLRoundRect rr(worldX, worldY, worldWidth, worldHeight, cornerRadius, cornerRadius);
        ctx.stroke_round_rect(rr);
    }

    // OneNote-style Container Header Handle (shown when hovered, selected, or actively editing)
    if (isHovered || isSelected || isEditing) {
        double headerH = 3.5;
        BLRoundRect topBar(worldX, worldY - headerH - 0.5, worldWidth, headerH, 2.0, 2.0);
        BLRgba32 barColor = (isEditing) ? BLRgba32(0x3B, 0x82, 0xF6, 0xD0) : BLRgba32(0x94, 0xA3, 0xB8, 0x90);
        ctx.set_fill_style(barColor);
        ctx.fill_round_rect(topBar);

        // Subtle frame around container
        ctx.set_stroke_style(BLRgba32(0x3B, 0x82, 0xF6, 0x80));
        ctx.set_stroke_width(0.4 * invScale);
        BLRoundRect frame(worldX - 1.0, worldY - 1.0, worldWidth + 2.0, worldHeight + 2.0, cornerRadius, cornerRadius);
        ctx.stroke_round_rect(frame);
    }

    // =========================================================================
    // LAYER 3: BLEND2D VECTOR TYPOGRAPHY PASS (Rich Inline & Styled Runs)
    // =========================================================================
    std::string fullText = PlainText();
    if (!fullText.empty()) {
        double curY = worldY + 2.0;
        const double maxWrapWidth = (std::max)(10.0, worldWidth - 4.0);

        /**
         * @brief Renders a single measured line slice with rich styled spans and inline elements.
         *
         * Mathematical Layout:
         * - Span start X: lineX + \sum_{prev} span.width
         * - Baseline: yTop + maxLineAscent
         * - Text baseline render: ctx.fill_utf8_text(..., spanFont, ...)
         */
        auto drawFormattedLine = [&](size_t lineStart, size_t lineEnd, double yTop, double lineAscent, double lineH) {
            double totalLineWidth = MeasureRange(lineStart, lineEnd);
            double lineX = worldX + 2.0;
            if (alignment == 1 && maxWrapWidth > totalLineWidth) {
                lineX += (maxWrapWidth - totalLineWidth) * 0.5; // Center alignment
            } else if (alignment == 2 && maxWrapWidth > totalLineWidth) {
                lineX += (maxWrapWidth - totalLineWidth); // Right alignment
            }

            auto spans = GetSpansForRange(lineStart, lineEnd);
            double spanX = lineX;

            for (const auto& span : spans) {
                if (span.width <= 0.0001 && span.text.empty()) continue;

                // Resolve styles for this span
                BLRgba32 spanTextColor = textColor;
                BLRgba32 spanHighlightColor = highlightColor;
                bool spanBold = isBold;
                bool spanItalic = isItalic;
                bool spanUnderline = isUnderline;
                bool spanStrikethrough = isStrikethrough;
                std::string spanFontFamily = fontFamily;
                double spanFontSize = fontSize;

                if (span.run) {
                    spanTextColor = span.run->color;
                    spanHighlightColor = span.run->highlightColor;
                    spanBold = span.run->bold;
                    spanItalic = span.run->italic;
                    spanUnderline = span.run->underline;
                    spanStrikethrough = span.run->strikethrough;
                    spanFontFamily = span.run->fontFamily;
                    spanFontSize = span.run->fontSize;
                }

                // Check for inline rich elements (InlineImage, InlineMath, InlineTable)
                if (span.run && span.run->elementType != InlineElementType::TextRun) {
                    if (!span.run->renderedImage.is_empty()) {
                        // Center vertically within the line height
                        double imgY = yTop + (lineH - span.run->inlineHeight) * 0.5;
                        ctx.blit_image(BLPoint(spanX, imgY), span.run->renderedImage);
                    } else if (span.run->elementType == InlineElementType::InlineMath) {
                        // Fallback: draw LaTeX source text in italic font
                        BLFont mathFont = FontManager::Instance().GetFont(spanFontFamily, spanFontSize, false, true);
                        double baselineY = yTop + lineAscent;
                        ctx.fill_utf8_text(BLPoint(spanX, baselineY), mathFont, span.run->latexSource.data(), span.run->latexSource.size(), spanTextColor);
                    }
                    spanX += span.width;
                    continue;
                }

                // Standard TextRun formatting
                BLFont spanFont = FontManager::Instance().GetFont(spanFontFamily, spanFontSize, spanBold, spanItalic);
                if (!spanFont.is_valid()) {
                    spanFont = FontManager::Instance().GetFont(fontFamily, fontSize, isBold, isItalic);
                }

                // Background highlight if present
                if (spanHighlightColor.a() > 0) {
                    ctx.set_fill_style(spanHighlightColor);
                    ctx.fill_rect(BLRect(spanX - 0.5, yTop, span.width + 1.0, lineH));
                }

                // Text glyph vector rasterization
                double baselineY = yTop + lineAscent;
                ctx.fill_utf8_text(BLPoint(spanX, baselineY), spanFont, span.text.data(), span.text.size(), spanTextColor);

                // Underline decoration
                if (spanUnderline) {
                    ctx.set_stroke_style(spanTextColor);
                    ctx.set_stroke_width(0.5);
                    ctx.stroke_line(spanX, baselineY + 1.2, spanX + span.width, baselineY + 1.2);
                }

                // Strikethrough decoration
                if (spanStrikethrough) {
                    ctx.set_stroke_style(spanTextColor);
                    ctx.set_stroke_width(0.5);
                    double strikeY = baselineY - (lineAscent * 0.35);
                    ctx.stroke_line(spanX, strikeY, spanX + span.width, strikeY);
                }

                spanX += span.width;
            }
        };

        size_t i = 0;
        while (i < fullText.size()) {
            size_t newlinePos = fullText.find('\n', i);
            size_t paraEnd = (newlinePos != std::string::npos) ? newlinePos : fullText.size();
            size_t paraLen = paraEnd - i;

            if (!isWrap || maxWrapWidth <= 10.0) {
                // Entire paragraph rendered on single line
                double lineAscent = 0.0, lineH = 0.0;
                GetLineMetricsForRange(i, paraEnd, lineAscent, lineH);
                drawFormattedLine(i, paraEnd, curY, lineAscent, lineH);
                curY += lineH;
            } else {
                // Word wrapping using per-span width measurements
                size_t lineStartIdx = i;
                size_t scanIdx = i;

                while (scanIdx < paraEnd) {
                    // Find next word boundary
                    size_t nextSpace = fullText.find(' ', scanIdx);
                    if (nextSpace > paraEnd) nextSpace = std::string::npos;
                    size_t wordEnd = (nextSpace != std::string::npos) ? nextSpace : paraEnd;
                    size_t candidateEnd = (nextSpace != std::string::npos) ? nextSpace + 1 : paraEnd;

                    double testW = MeasureRange(lineStartIdx, wordEnd);
                    if (testW <= maxWrapWidth || scanIdx == lineStartIdx) {
                        // Word fits, advance scan
                        scanIdx = candidateEnd;
                    } else {
                        // Word overflows, commit current line slice [lineStartIdx, scanIdx]
                        size_t actualLineEnd = scanIdx;
                        if (actualLineEnd > lineStartIdx && fullText[actualLineEnd - 1] == ' ') {
                            actualLineEnd--; // trim trailing space from measure
                        }
                        double lineAscent = 0.0, lineH = 0.0;
                        GetLineMetricsForRange(lineStartIdx, actualLineEnd, lineAscent, lineH);
                        drawFormattedLine(lineStartIdx, actualLineEnd, curY, lineAscent, lineH);
                        curY += lineH;
                        lineStartIdx = scanIdx;
                        scanIdx = candidateEnd;
                    }
                }

                if (lineStartIdx < paraEnd) {
                    double lineAscent = 0.0, lineH = 0.0;
                    GetLineMetricsForRange(lineStartIdx, paraEnd, lineAscent, lineH);
                    drawFormattedLine(lineStartIdx, paraEnd, curY, lineAscent, lineH);
                    curY += lineH;
                }
            }

            if (newlinePos != std::string::npos) {
                i = newlinePos + 1;
            } else {
                break;
            }
        }
    }

    ctx.restore();
}

void TextBoxObject::RenderWithEditor(BLContext& ctx, const Viewport& viewport, const TextEditorState& editor) const {
    // 1. Draw base container and vector text
    Render(ctx, viewport);

    ctx.save();
    ctx.apply_transform(transform);

    // 2. Draw selection highlight boxes
    std::vector<AABB> selBoxes = editor.GetSelectionBoxes();
    if (!selBoxes.empty()) {
        ctx.set_fill_style(BLRgba32(0x3B, 0x82, 0xF6, 0x66)); // Translucent accent selection
        for (const auto& box : selBoxes) {
            ctx.fill_rect(box.minX, box.minY, box.Width(), box.Height());
        }
    }

    // 3. Draw blinking caret line
    if (editor.IsCaretVisible()) {
        Point2D caretPt = editor.GetCursorWorldPos();
        double caretH = editor.GetCaretHeight();

        ctx.set_stroke_style(textColor);
        ctx.set_stroke_width(0.7);
        ctx.stroke_line(caretPt.x, caretPt.y, caretPt.x, caretPt.y + caretH);
    }

    ctx.restore();
}

std::unique_ptr<CanvasObject> TextBoxObject::Clone() const {
    return std::make_unique<TextBoxObject>(*this);
}

} // namespace Folio
