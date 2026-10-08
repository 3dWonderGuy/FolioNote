#include "core/md_engine/md_layout_engine.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>

namespace Folio {

namespace {

constexpr double PT_TO_MM = 25.4 / 72.0;

double MeasureSpanWidth(const std::string& text, float fontSizePt, bool isCode = false) {
    if (text.empty()) return 0.0;
    double fontHeightMm = fontSizePt * PT_TO_MM;
    double avgCharWidth = isCode ? (fontHeightMm * 0.60) : (fontHeightMm * 0.52);

    double total = 0.0;
    for (size_t idx = 0; idx < text.size(); ++idx) {
        uint8_t c = static_cast<uint8_t>(text[idx]);
        // Skip UTF-8 continuation bytes to avoid counting multibyte codepoints multiple times
        if ((c & 0xC0) == 0x80) continue;

        if (c == ' ' || c == '\t') {
            total += avgCharWidth * 0.65;
        } else if (c == 'i' || c == 'l' || c == '.' || c == ',' || c == '!') {
            total += avgCharWidth * (isCode ? 1.0 : 0.45);
        } else if (c == 'm' || c == 'w' || c == 'M' || c == 'W') {
            total += avgCharWidth * (isCode ? 1.0 : 1.40);
        } else if (c >= 0x80) {
            // Non-ASCII codepoint (CJK, Cyrillic, Greek, accented latin)
            total += avgCharWidth * (isCode ? 1.0 : 1.15);
        } else {
            total += avgCharWidth;
        }
    }
    return total;
}

} // anonymous namespace

double MdLayoutEngine::LayoutDocument(std::vector<MdBlock>& blocks, const MdStyleConfig& config) {
    double currentY = config.marginTopMm;
    double contentW = config.ContentWidthMm();

    for (auto& block : blocks) {
        block.localY_mm = currentY;
        block.width_mm = contentW;

        switch (block.type) {
            case MdBlockType::Heading: {
                currentY += config.headingSpacingTopMm;
                block.localY_mm = currentY;

                float sizePt = config.h1FontSizePt;
                if (block.level == 2) sizePt = config.h2FontSizePt;
                else if (block.level == 3) sizePt = config.h3FontSizePt;
                else if (block.level >= 4) sizePt = config.h4FontSizePt;

                for (auto& sp : block.spans) {
                    sp.sizePt = sizePt;
                    sp.widthMm = MeasureSpanWidth(sp.text, sizePt, sp.type == MdSpanType::Code);
                    sp.heightMm = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                }

                double lineH = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                block.height_mm = lineH;
                currentY += lineH + config.headingSpacingBottomMm;
                break;
            }

            case MdBlockType::Paragraph: {
                double totalSpanW = 0.0;
                float sizePt = config.bodyFontSizePt;

                for (auto& sp : block.spans) {
                    sp.sizePt = sizePt;
                    sp.widthMm = MeasureSpanWidth(sp.text, sizePt, sp.type == MdSpanType::Code);
                    sp.heightMm = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                    totalSpanW += sp.widthMm;
                }

                double lineH = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                int lineCount = std::max(1, static_cast<int>(std::ceil(totalSpanW / contentW)));
                block.height_mm = lineCount * lineH;
                currentY += block.height_mm + config.paragraphSpacingMm;
                break;
            }

            case MdBlockType::BulletList:
            case MdBlockType::NumberedList:
            case MdBlockType::TaskList: {
                double indentMm = config.listIndentMm * block.level + 6.0;
                double availW = std::max(20.0, contentW - indentMm);
                float sizePt = config.bodyFontSizePt;
                double totalSpanW = 0.0;

                for (auto& sp : block.spans) {
                    sp.sizePt = sizePt;
                    sp.widthMm = MeasureSpanWidth(sp.text, sizePt, sp.type == MdSpanType::Code);
                    sp.heightMm = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                    totalSpanW += sp.widthMm;
                }

                double lineH = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                int lineCount = std::max(1, static_cast<int>(std::ceil(totalSpanW / availW)));
                block.height_mm = lineCount * lineH;
                currentY += block.height_mm + 1.5; // compact list spacing
                break;
            }

            case MdBlockType::Blockquote: {
                double indentMm = config.blockquoteIndentMm + 4.0;
                double availW = std::max(20.0, contentW - indentMm);
                float sizePt = config.bodyFontSizePt;
                double totalSpanW = 0.0;

                for (auto& sp : block.spans) {
                    sp.sizePt = sizePt;
                    sp.widthMm = MeasureSpanWidth(sp.text, sizePt, sp.type == MdSpanType::Code);
                    sp.heightMm = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                    totalSpanW += sp.widthMm;
                }

                double lineH = sizePt * PT_TO_MM * config.lineHeightMultiplier;
                int lineCount = std::max(1, static_cast<int>(std::ceil(totalSpanW / availW)));
                block.height_mm = lineCount * lineH + 4.0; // padding
                currentY += block.height_mm + config.paragraphSpacingMm;
                break;
            }

            case MdBlockType::CodeBlock: {
                float sizePt = config.codeFontSizePt;
                double lineH = sizePt * PT_TO_MM * 1.40;

                // Count lines
                int codeLines = 1;
                for (char c : block.codeContent) {
                    if (c == '\n') ++codeLines;
                }

                block.height_mm = (codeLines * lineH) + 8.0; // 4mm top/bottom padding
                currentY += block.height_mm + config.paragraphSpacingMm;
                break;
            }

            case MdBlockType::Table: {
                size_t cols = block.table.headers.size();
                if (cols == 0) cols = 1;
                double colW = contentW / static_cast<double>(cols);
                block.table.colWidthsMm.assign(cols, colW);

                float sizePt = config.bodyFontSizePt;
                double rowH = sizePt * PT_TO_MM * 1.5 + 4.0;
                size_t totalRows = 1 + block.table.rows.size(); // 1 header + data rows
                block.height_mm = totalRows * rowH;
                currentY += block.height_mm + config.paragraphSpacingMm;
                break;
            }

            case MdBlockType::MathBlock: {
                block.height_mm = 16.0;
                currentY += block.height_mm + config.paragraphSpacingMm;
                break;
            }

            case MdBlockType::HorizontalRule: {
                block.height_mm = 6.0;
                currentY += block.height_mm + 2.0;
                break;
            }
        }
    }

    return currentY + config.marginBottomMm;
}

size_t MdLayoutEngine::HitTestOffset(
    double localX_mm,
    double localY_mm,
    const std::vector<MdBlock>& blocks,
    const MdStyleConfig& config
) {
    if (blocks.empty()) return 0;

    for (const auto& block : blocks) {
        if (localY_mm >= block.localY_mm && localY_mm <= block.localY_mm + block.height_mm) {
            if (block.spans.empty()) {
                return block.sourceStartOffset;
            }

            double indentX = config.marginLeftMm;
            if (block.type == MdBlockType::BulletList || block.type == MdBlockType::NumberedList || block.type == MdBlockType::TaskList) {
                indentX += (config.listIndentMm * block.level + 6.0);
            } else if (block.type == MdBlockType::Blockquote) {
                indentX += (config.blockquoteIndentMm + 4.0);
            }

            double targetX = localX_mm - indentX;
            if (targetX <= 0.0) {
                return block.sourceStartOffset;
            }

            double curX = 0.0;
            for (const auto& sp : block.spans) {
                if (targetX <= curX + sp.widthMm) {
                    double relX = std::max(0.0, targetX - curX);
                    double ratio = (sp.widthMm > 0.0) ? (relX / sp.widthMm) : 0.0;
                    size_t charOffset = static_cast<size_t>(std::round(ratio * sp.sourceLength));
                    return sp.sourceOffset + std::min(charOffset, sp.sourceLength);
                }
                curX += sp.widthMm;
            }

            return block.spans.back().sourceOffset + block.spans.back().sourceLength;
        }
    }

    if (localY_mm < blocks.front().localY_mm) {
        return blocks.front().sourceStartOffset;
    }
    return blocks.back().sourceEndOffset;
}

MdCaret MdLayoutEngine::GetCaretForOffset(
    size_t offset,
    const std::vector<MdBlock>& blocks,
    const MdStyleConfig& config
) {
    MdCaret caret;
    caret.offset = offset;
    caret.worldPosMm = Point2D{ config.marginLeftMm, config.marginTopMm };
    caret.heightMm = config.bodyFontSizePt * PT_TO_MM;
    caret.visible = true;

    if (blocks.empty()) return caret;

    for (const auto& block : blocks) {
        if (offset >= block.sourceStartOffset && offset <= block.sourceEndOffset) {
            caret.worldPosMm.y = block.localY_mm;
            caret.worldPosMm.x = config.marginLeftMm;

            if (block.type == MdBlockType::BulletList || block.type == MdBlockType::NumberedList || block.type == MdBlockType::TaskList) {
                caret.worldPosMm.x += (config.listIndentMm * block.level + 6.0);
            } else if (block.type == MdBlockType::Blockquote) {
                caret.worldPosMm.x += config.blockquoteIndentMm + 4.0;
            }

            if (!block.spans.empty()) {
                caret.heightMm = block.spans.front().sizePt * PT_TO_MM;
            }

            double spanAccX = 0.0;
            for (const auto& sp : block.spans) {
                if (offset <= sp.sourceOffset) {
                    break;
                } else if (offset >= sp.sourceOffset + sp.sourceLength) {
                    spanAccX += sp.widthMm;
                } else {
                    size_t relOffset = offset - sp.sourceOffset;
                    double ratio = (sp.sourceLength > 0) ? (static_cast<double>(relOffset) / sp.sourceLength) : 0.0;
                    spanAccX += sp.widthMm * ratio;
                    break;
                }
            }
            caret.worldPosMm.x += spanAccX;
            break;
        }
    }

    return caret;
}

std::vector<AABB> MdLayoutEngine::GetSelectionBoxes(
    size_t startOffset,
    size_t endOffset,
    const std::vector<MdBlock>& blocks,
    const MdStyleConfig& config
) {
    std::vector<AABB> boxes;
    if (startOffset >= endOffset || blocks.empty()) return boxes;

    for (const auto& block : blocks) {
        if (block.sourceEndOffset >= startOffset && block.sourceStartOffset <= endOffset) {
            double y0 = block.localY_mm;
            double y1 = block.localY_mm + block.height_mm;
            double x0 = config.marginLeftMm;
            double x1 = config.marginLeftMm + block.width_mm;
            boxes.push_back(AABB(x0, y0, x1, y1));
        }
    }

    return boxes;
}

} // namespace Folio
