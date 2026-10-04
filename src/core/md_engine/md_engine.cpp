#include "core/md_engine/md_engine.hpp"
#include "core/text/font_manager.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <sstream>

namespace Folio {

MdEngine::MdEngine() {
    isDirty = true;
}

void MdEngine::LoadMarkdown(const std::string& text) {
    editor.SetText(text);
    Reflow();
}

void MdEngine::Reflow() {
    blocks = MdParser::ParseDocument(editor.GetText());
    totalDocumentHeightMm = MdLayoutEngine::LayoutDocument(blocks, config);
    isDirty = false;
}

void MdEngine::RenderToBlend2D(
    BLContext& ctx,
    double originX_mm,
    double originY_mm,
    double /*viewW_mm*/,
    double /*viewH_mm*/,
    bool invert
) {
    if (isDirty) {
        Reflow();
    }

    double pageW = config.pageWidthMm;
    double pageH = std::max(config.pageHeightMm, totalDocumentHeightMm);

    ctx.save();

    // 1. Soft Drop Shadow
    ctx.set_fill_style(BLRgba32(0x00, 0x00, 0x00, 0x1A));
    ctx.fill_round_rect(originX_mm + 2.0, originY_mm + 2.5, pageW, pageH, 2.5);

    // 2. Paper Surface
    BLRgba32 paperBg = invert ? BLRgba32(0x18, 0x1A, 0x20, 0xFF) : BLRgba32(0xFF, 0xFF, 0xFF, 0xFF);
    ctx.set_fill_style(paperBg);
    ctx.fill_round_rect(originX_mm, originY_mm, pageW, pageH, 2.0);

    // Paper border
    BLRgba32 paperBorder = invert ? BLRgba32(0x2E, 0x32, 0x3E, 0xFF) : BLRgba32(0xE2, 0xE8, 0xF0, 0xFF);
    ctx.set_stroke_style(paperBorder);
    ctx.set_stroke_width(0.4);
    ctx.stroke_round_rect(originX_mm, originY_mm, pageW, pageH, 2.0);

    // 3. Setup Fonts
    BLFont bodyFont = FontManager::Instance().GetFont(config.bodyFontFamily, config.bodyFontSizePt);
    BLFont boldFont = FontManager::Instance().GetFont(config.bodyFontFamily, config.bodyFontSizePt, true, false);
    BLFont italicFont = FontManager::Instance().GetFont(config.bodyFontFamily, config.bodyFontSizePt, false, true);
    BLFont codeFont = FontManager::Instance().GetFont(config.codeFontFamily, config.codeFontSizePt);
    BLFont h1Font = FontManager::Instance().GetFont(config.bodyFontFamily, config.h1FontSizePt, true, false);
    BLFont h2Font = FontManager::Instance().GetFont(config.bodyFontFamily, config.h2FontSizePt, true, false);
    BLFont h3Font = FontManager::Instance().GetFont(config.bodyFontFamily, config.h3FontSizePt, true, false);
    BLFont h4Font = FontManager::Instance().GetFont(config.bodyFontFamily, config.h4FontSizePt, true, false);

    auto getSpanFont = [&](const MdSpan& sp, const BLFont& defaultBase) -> BLFont {
        switch (sp.type) {
            case MdSpanType::Bold: return boldFont;
            case MdSpanType::Italic: return italicFont;
            case MdSpanType::Code: return codeFont;
            default: return defaultBase;
        }
    };

    // 4. Render Blocks
    BLRgba32 textColor = invert ? BLRgba32(0xF1, 0xF5, 0xF9, 0xFF) : config.textColor;
    BLRgba32 headingColor = invert ? BLRgba32(0xFE, 0xF9, 0xC3, 0xFF) : config.headingColor;

    for (const auto& block : blocks) {
        double bx = originX_mm + config.marginLeftMm;
        double by = originY_mm + block.localY_mm;

        switch (block.type) {
            case MdBlockType::Heading: {
                BLFont headFont = h1Font;
                if (block.level == 2) headFont = h2Font;
                else if (block.level == 3) headFont = h3Font;
                else if (block.level >= 4) headFont = h4Font;

                ctx.set_fill_style(headingColor);
                double curX = bx;
                for (const auto& sp : block.spans) {
                    ctx.fill_utf8_text(BLPoint(curX, by + sp.heightMm * 0.8), headFont, sp.text.c_str());
                    curX += sp.widthMm;
                }
                break;
            }

            case MdBlockType::Paragraph: {
                ctx.set_fill_style(textColor);
                double curX = bx;
                double curY = by;
                double maxW = config.ContentWidthMm();

                for (const auto& sp : block.spans) {
                    if (curX + sp.widthMm > bx + maxW && curX > bx) {
                        curX = bx;
                        curY += sp.heightMm;
                    }

                    if (sp.type == MdSpanType::Highlight) {
                        ctx.set_fill_style(config.highlightBgColor);
                        ctx.fill_round_rect(curX - 0.5, curY, sp.widthMm + 1.0, sp.heightMm, 1.0);
                    } else if (sp.type == MdSpanType::Code) {
                        ctx.set_fill_style(invert ? BLRgba32(0x28, 0x2C, 0x37, 0xFF) : config.codeBgColor);
                        ctx.fill_round_rect(curX - 0.5, curY, sp.widthMm + 1.0, sp.heightMm, 1.0);
                    }

                    if (sp.type == MdSpanType::Link) {
                        ctx.set_fill_style(config.linkColor);
                        ctx.fill_utf8_text(BLPoint(curX, curY + sp.heightMm * 0.8), getSpanFont(sp, bodyFont), sp.text.c_str());
                        // Underline
                        ctx.set_stroke_style(config.linkColor);
                        ctx.set_stroke_width(0.35);
                        ctx.stroke_line(curX, curY + sp.heightMm * 0.9, curX + sp.widthMm, curY + sp.heightMm * 0.9);
                    } else {
                        ctx.set_fill_style(textColor);
                        ctx.fill_utf8_text(BLPoint(curX, curY + sp.heightMm * 0.8), getSpanFont(sp, bodyFont), sp.text.c_str());
                    }

                    curX += sp.widthMm;
                }
                break;
            }

            case MdBlockType::BulletList: {
                double indent = config.listIndentMm * block.level;
                // Draw bullet dot
                ctx.set_fill_style(textColor);
                ctx.fill_circle(bx + indent + 1.5, by + 2.5, 0.85);

                double curX = bx + indent + 5.0;
                for (const auto& sp : block.spans) {
                    ctx.fill_utf8_text(BLPoint(curX, by + sp.heightMm * 0.8), getSpanFont(sp, bodyFont), sp.text.c_str());
                    curX += sp.widthMm;
                }
                break;
            }

            case MdBlockType::NumberedList: {
                double indent = config.listIndentMm * block.level;
                std::string numStr = std::to_string(block.listIndex) + ".";
                ctx.set_fill_style(textColor);
                ctx.fill_utf8_text(BLPoint(bx + indent, by + 3.0), bodyFont, numStr.c_str());

                double curX = bx + indent + 6.0;
                for (const auto& sp : block.spans) {
                    ctx.fill_utf8_text(BLPoint(curX, by + sp.heightMm * 0.8), getSpanFont(sp, bodyFont), sp.text.c_str());
                    curX += sp.widthMm;
                }
                break;
            }

            case MdBlockType::TaskList: {
                double indent = config.listIndentMm * block.level;
                // Draw checkbox
                ctx.set_stroke_style(textColor);
                ctx.set_stroke_width(0.4);
                ctx.stroke_round_rect(bx + indent, by + 1.0, 3.2, 3.2, 0.6);

                if (block.checked) {
                    ctx.set_fill_style(config.linkColor);
                    ctx.fill_round_rect(bx + indent + 0.6, by + 1.6, 2.0, 2.0, 0.4);
                }

                double curX = bx + indent + 6.0;
                ctx.set_fill_style(textColor);
                for (const auto& sp : block.spans) {
                    ctx.fill_utf8_text(BLPoint(curX, by + sp.heightMm * 0.8), getSpanFont(sp, bodyFont), sp.text.c_str());
                    curX += sp.widthMm;
                }
                break;
            }

            case MdBlockType::Blockquote: {
                double indent = config.blockquoteIndentMm;
                // Draw background tint
                ctx.set_fill_style(config.quoteBgColor);
                ctx.fill_round_rect(bx, by, block.width_mm, block.height_mm, 1.5);

                // Draw vertical quote bar
                ctx.set_fill_style(config.quoteBorderColor);
                ctx.fill_round_rect(bx, by, 1.5, block.height_mm, 0.75);

                double curX = bx + indent + 3.0;
                ctx.set_fill_style(textColor);
                for (const auto& sp : block.spans) {
                    ctx.fill_utf8_text(BLPoint(curX, by + sp.heightMm * 0.8), italicFont, sp.text.c_str());
                    curX += sp.widthMm;
                }
                break;
            }

            case MdBlockType::CodeBlock: {
                // Background card
                ctx.set_fill_style(invert ? BLRgba32(0x20, 0x24, 0x2E, 0xFF) : config.codeBgColor);
                ctx.fill_round_rect(bx, by, block.width_mm, block.height_mm, 2.5);

                ctx.set_stroke_style(config.codeBorderColor);
                ctx.set_stroke_width(0.4);
                ctx.stroke_round_rect(bx, by, block.width_mm, block.height_mm, 2.5);

                // Language tag in corner
                if (!block.language.empty()) {
                    ctx.set_fill_style(BLRgba32(0x94, 0xA3, 0xB8, 0xFF));
                    ctx.fill_utf8_text(BLPoint(bx + block.width_mm - 20.0, by + 4.0), codeFont, block.language.c_str());
                }

                // Render code text line by line
                ctx.set_fill_style(invert ? BLRgba32(0xE2, 0xE8, 0xF0, 0xFF) : config.codeTextColor);
                double codeY = by + 6.0;
                double lineH = config.codeFontSizePt * (25.4 / 72.0) * 1.40;
                std::istringstream stream(block.codeContent);
                std::string codeLine;
                while (std::getline(stream, codeLine)) {
                    ctx.fill_utf8_text(BLPoint(bx + 5.0, codeY), codeFont, codeLine.c_str());
                    codeY += lineH;
                }
                break;
            }

            case MdBlockType::Table: {
                double rowH = config.bodyFontSizePt * (25.4 / 72.0) * 1.5 + 4.0;
                double curRowY = by;

                // Header background
                ctx.set_fill_style(invert ? BLRgba32(0x24, 0x28, 0x33, 0xFF) : config.tableHeaderBgColor);
                ctx.fill_round_rect(bx, curRowY, block.width_mm, rowH, 1.0);

                // Header text
                ctx.set_fill_style(headingColor);
                double curColX = bx;
                for (size_t c = 0; c < block.table.headers.size(); ++c) {
                    double cw = (c < block.table.colWidthsMm.size()) ? block.table.colWidthsMm[c] : 30.0;
                    const auto& h = block.table.headers[c];
                    ctx.fill_utf8_text(BLPoint(curColX + 3.0, curRowY + rowH * 0.7), boldFont, h.c_str());
                    curColX += cw;
                }
                curRowY += rowH;

                // Data rows
                for (const auto& row : block.table.rows) {
                    curColX = bx;
                    ctx.set_fill_style(textColor);
                    for (size_t c = 0; c < row.size(); ++c) {
                        double cw = (c < block.table.colWidthsMm.size()) ? block.table.colWidthsMm[c] : 30.0;
                        const auto& val = row[c];
                        ctx.fill_utf8_text(BLPoint(curColX + 3.0, curRowY + rowH * 0.7), bodyFont, val.c_str());
                        curColX += cw;
                    }
                    curRowY += rowH;
                }

                // Table outer border
                ctx.set_stroke_style(config.tableBorderColor);
                ctx.set_stroke_width(0.4);
                ctx.stroke_round_rect(bx, by, block.width_mm, block.height_mm, 1.5);
                break;
            }

            case MdBlockType::HorizontalRule: {
                ctx.set_stroke_style(config.hrColor);
                ctx.set_stroke_width(0.5);
                ctx.stroke_line(bx, by + 3.0, bx + block.width_mm, by + 3.0);
                break;
            }

            case MdBlockType::MathBlock: {
                ctx.set_fill_style(invert ? BLRgba32(0x20, 0x24, 0x2E, 0xFF) : BLRgba32(0xF8, 0xFA, 0xFC, 0xFF));
                ctx.fill_round_rect(bx, by, block.width_mm, block.height_mm, 2.0);

                ctx.set_fill_style(config.textColor);
                ctx.fill_utf8_text(BLPoint(bx + 15.0, by + 10.0), italicFont, block.codeContent.c_str());
                break;
            }
        }
    }

    ctx.restore();
}

void MdEngine::RenderLiveLayer(
    BLContext& ctx,
    double originX_mm,
    double originY_mm,
    double currentSec
) {
    editor.UpdateBlink(currentSec);

    ctx.save();

    // 1. Text Selection highlight rectangles
    if (editor.selection.hasSelection) {
        auto boxes = MdLayoutEngine::GetSelectionBoxes(
            editor.selection.MinOffset(),
            editor.selection.MaxOffset(),
            blocks,
            config
        );

        ctx.set_fill_style(config.selectionColor);
        for (const auto& box : boxes) {
            ctx.fill_rect(originX_mm + box.minX, originY_mm + box.minY, box.Width(), box.Height());
        }
    }

    // 2. Blinking Caret
    if (editor.caret.visible) {
        MdCaret c = MdLayoutEngine::GetCaretForOffset(editor.selection.cursorOffset, blocks, config);
        ctx.set_stroke_style(BLRgba32(0x25, 0x63, 0xEB, 0xFF)); // Accent blue caret
        ctx.set_stroke_width(0.7);
        double cx = originX_mm + c.worldPosMm.x;
        double cy0 = originY_mm + c.worldPosMm.y;
        double cy1 = cy0 + c.heightMm;
        ctx.stroke_line(cx, cy0, cx, cy1);
    }

    ctx.restore();
}

void MdEngine::OnMouseDown(double localX_mm, double localY_mm, bool shiftSelect) {
    size_t off = MdLayoutEngine::HitTestOffset(localX_mm, localY_mm, blocks, config);
    editor.SetCursorOffset(off, shiftSelect);
}

void MdEngine::OnMouseDrag(double localX_mm, double localY_mm) {
    size_t off = MdLayoutEngine::HitTestOffset(localX_mm, localY_mm, blocks, config);
    editor.SetCursorOffset(off, true);
}

void MdEngine::OnTextInput(const std::string& text) {
    editor.InsertText(text);
    isDirty = true;
}

void MdEngine::OnKeyDown(int32_t keycode, uint16_t keymod) {
    bool shift = (keymod & SDL_KMOD_SHIFT) != 0;
    bool ctrl = (keymod & SDL_KMOD_CTRL) != 0;

    switch (keycode) {
        case SDLK_BACKSPACE:
            editor.Backspace();
            isDirty = true;
            break;
        case SDLK_DELETE:
            editor.DeleteForward();
            isDirty = true;
            break;
        case SDLK_RETURN:
            editor.InsertText("\n");
            isDirty = true;
            break;
        case SDLK_LEFT:
            editor.MoveCursor(-1, shift);
            break;
        case SDLK_RIGHT:
            editor.MoveCursor(1, shift);
            break;
        case SDLK_HOME:
            editor.MoveToLineStart(shift);
            break;
        case SDLK_END:
            editor.MoveToLineEnd(shift);
            break;
        case SDLK_A:
            if (ctrl) editor.SelectAll();
            break;
        case SDLK_Z:
            if (ctrl) {
                if (shift) editor.Redo();
                else editor.Undo();
                isDirty = true;
            }
            break;
        case SDLK_Y:
            if (ctrl) {
                editor.Redo();
                isDirty = true;
            }
            break;
        case SDLK_B:
            if (ctrl) {
                editor.ToggleBold();
                isDirty = true;
            }
            break;
        case SDLK_I:
            if (ctrl) {
                editor.ToggleItalic();
                isDirty = true;
            }
            break;
    }
}

} // namespace Folio
