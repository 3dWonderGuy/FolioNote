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
    TextRun r;
    r.color = textColor;
    r.fontSize = fontSize;
    r.fontFamily = fontFamily;
    runs.push_back(r);
    UpdateBounds();
}

TextBoxObject::TextBoxObject(double x, double y, double w, double h)
    : worldX(x), worldY(y), worldWidth(w), worldHeight(h) {
    type = ObjectType::Text;
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
    } else {
        runs[0].text = text;
        runs[0].fontFamily = fontFamily;
        runs[0].fontSize = fontSize;
        runs[0].color = textColor;
        runs[0].bold = isBold;
        runs[0].italic = isItalic;
        runs[0].underline = isUnderline;
        runs[0].strikethrough = isStrikethrough;
        runs[0].highlightColor = highlightColor;
    }
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
    BLPoint p[4] = {
        transform.map_point(worldX,              worldY),
        transform.map_point(worldX + worldWidth, worldY),
        transform.map_point(worldX + worldWidth, worldY + worldHeight),
        transform.map_point(worldX,              worldY + worldHeight)
    };
    double minX = p[0].x, maxX = p[0].x;
    double minY = p[0].y, maxY = p[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = (std::min)(minX, p[i].x);
        maxX = (std::max)(maxX, p[i].x);
        minY = (std::min)(minY, p[i].y);
        maxY = (std::max)(maxY, p[i].y);
    }
    bounds = AABB(minX, minY, maxX, maxY);
}

bool TextBoxObject::HitTest(double wx, double wy) const {
    // Include 4mm top grab bar area when testing for hit
    AABB hitBox = bounds;
    hitBox.minY -= 4.0;
    return hitBox.Contains(wx, wy);
}

bool TextBoxObject::Intersects(const AABB& sel) const {
    return bounds.Intersects(sel);
}

void TextBoxObject::ApplyTransform(const BLMatrix2D& matrix) {
    transform.post_transform(matrix);
    UpdateBounds();
}

void TextBoxObject::BakeTransform() {
    if (std::abs(transform.m01) < 1e-6 && std::abs(transform.m10) < 1e-6) {
        if (transform.m00 == 1.0 && transform.m11 == 1.0 &&
            transform.m20 == 0.0 && transform.m21 == 0.0) return;

        double p0x = transform.m00 * worldX + transform.m20;
        double p0y = transform.m11 * worldY + transform.m21;
        double p1x = transform.m00 * (worldX + worldWidth)  + transform.m20;
        double p1y = transform.m11 * (worldY + worldHeight) + transform.m21;

        worldX      = (std::min)(p0x, p1x);
        worldY      = (std::min)(p0y, p1y);
        worldWidth  = (std::max)(10.0, std::abs(p1x - p0x));
        worldHeight = (std::max)(10.0, std::abs(p1y - p0y));

        transform = BLMatrix2D::make_identity();
        isDirty   = true;
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

    // OneNote-style Container Header Handle (shown when hovered or actively editing)
    if (isHovered || isEditing) {
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
    // LAYER 3: BLEND2D VECTOR TYPOGRAPHY PASS
    // =========================================================================
    BLFont font = FontManager::Instance().GetFont(fontFamily, fontSize, isBold, isItalic);
    if (font.is_valid()) {
        BLFontMetrics fm = FontManager::Instance().GetMetrics(font);
        double lineHeight = fm.ascent + fm.descent + fm.line_gap;
        if (lineHeight <= 0.001) lineHeight = fontSize * 1.25;
        double ascent = (fm.ascent > 0.001) ? fm.ascent : (fontSize * 0.9);

        double curY = worldY + 2.0;
        const double maxWrapWidth = (std::max)(10.0, worldWidth - 4.0);

        auto drawFormattedLine = [&](const std::string& lineText, double yTop) {
            double lineW = FontManager::Instance().MeasureTextWidth(font, lineText);
            double lineX = worldX + 2.0;
            if (alignment == 1 && maxWrapWidth > lineW) {
                lineX += (maxWrapWidth - lineW) * 0.5; // Center
            } else if (alignment == 2 && maxWrapWidth > lineW) {
                lineX += (maxWrapWidth - lineW); // Right
            }
            // Highlight background if set
            if (highlightColor.a() > 0) {
                ctx.set_fill_style(highlightColor);
                ctx.fill_rect(BLRect(lineX - 0.5, yTop, lineW + 1.0, lineHeight));
            }
            // Text fill
            double baselineY = yTop + ascent;
            ctx.fill_utf8_text(BLPoint(lineX, baselineY), font, lineText.data(), lineText.size(), textColor);

            // Underline decoration
            if (isUnderline) {
                ctx.set_stroke_style(textColor);
                ctx.set_stroke_width(0.5);
                ctx.stroke_line(lineX, baselineY + 1.2, lineX + lineW, baselineY + 1.2);
            }
            // Strikethrough decoration
            if (isStrikethrough) {
                ctx.set_stroke_style(textColor);
                ctx.set_stroke_width(0.5);
                double strikeY = baselineY - (ascent * 0.35);
                ctx.stroke_line(lineX, strikeY, lineX + lineW, strikeY);
            }
        };

        std::string fullText = PlainText();
        if (!fullText.empty()) {
            size_t i = 0;
            while (i < fullText.size()) {
                size_t newlinePos = fullText.find('\n', i);
                std::string paragraph = (newlinePos != std::string::npos)
                    ? fullText.substr(i, newlinePos - i)
                    : fullText.substr(i);

                if (!isWrap || maxWrapWidth <= 10.0) {
                    drawFormattedLine(paragraph, curY);
                    curY += lineHeight;
                } else {
                    std::istringstream iss(paragraph);
                    std::string word;
                    std::string currentLine;

                    while (iss >> word) {
                        std::string testLine = currentLine.empty() ? word : (currentLine + " " + word);
                        double testW = FontManager::Instance().MeasureTextWidth(font, testLine);
                        if (testW <= maxWrapWidth || currentLine.empty()) {
                            currentLine = testLine;
                        } else {
                            drawFormattedLine(currentLine, curY);
                            curY += lineHeight;
                            currentLine = word;
                        }
                    }
                    if (!currentLine.empty()) {
                        drawFormattedLine(currentLine, curY);
                        curY += lineHeight;
                    }
                }

                if (newlinePos != std::string::npos) {
                    i = newlinePos + 1;
                } else {
                    break;
                }
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

void TextBoxObject::Serialize(Serializer& /*writer*/) const {}
void TextBoxObject::Deserialize(Deserializer& /*reader*/) {}

} // namespace Folio
