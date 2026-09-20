/**
 * @file text_editor_state.cpp
 * @brief Implementation of the headless, Slint-proof TextEditorState controller.
 *
 * GENERAL WORKING PROCESS:
 * ------------------------
 * 1. Attaches to a TextBoxObject when the user clicks to type or edits an existing note.
 * 2. On text input / key presses, updates the text buffer, handles UTF-8 byte sequences,
 *    and manages the cursor and selection range.
 * 3. Calculates typographical text layout using FontManager metrics (ascent, descent,
 *    advance width) and computes word wrapping.
 * 4. Yields world-space cursor positions and selection highlight bounding boxes for
 *    Blend2D to render without any UI-toolkit dependency.
 *
 * MATHEMATICAL FOUNDATIONS:
 * -------------------------
 * - Line Layout:
 *     Local Line Top:  y_{line} = \sum_{k=0}^{i-1} height_k
 *     Baseline:        baselineY = y_{line} + ascent
 * - Word Wrapping:
 *     Greedy wrapping: advance = MeasureTextWidth(font, currentWord)
 *     If (lineWidth + advance > maxWidth) -> wrap to next line.
 * - Point-to-Index Projection:
 *     Line is selected by: line.localY <= (queryY - worldY) < line.localY + line.height.
 *     Char offset is found by minimal horizontal distance: |x_{char} - queryX|.
 */

#include "core/objects/text/text_editor_state.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/document/document_session.hpp"
#include "core/history/canvas_command.hpp"
#include "core/text/font_manager.hpp"
#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Folio {

TextEditorState::TextEditorState() = default;

void TextEditorState::Attach(TextBoxObject* target, DocumentSession* session) {
    if (m_target && m_target != target) {
        Detach(session);
    }
    m_target = target;
    if (m_target) {
        m_target->isEditing = true;
        m_cursorIndex = m_target->text.size();
        m_selectionAnchor = m_cursorIndex;
        m_caretVisible = true;
        m_lastBlinkTimeSec = 0.0;
        m_baselineText = m_target->text;
        m_baselineWidth = m_target->worldWidth;
        m_baselineHeight = m_target->worldHeight;
        ReflowLayout();
    }
}

void TextEditorState::Detach(DocumentSession* session) {
    if (m_target) {
        if (session) {
            CommitTextEdit(session);
        }
        m_target->isEditing = false;
        m_target->isDirty = true;
        m_target = nullptr;
    }
    m_baselineText.clear();
    m_baselineWidth = 0.0;
    m_baselineHeight = 0.0;
    m_cursorIndex = 0;
    m_selectionAnchor = 0;
    m_lines.clear();
}

/**
 * @brief Commits modified text strings and dimension expansions into the session's undo stack.
 */
void TextEditorState::CommitTextEdit(DocumentSession* session) {
    if (!session || !m_target) return;
    if (m_target->text != m_baselineText ||
        std::abs(m_target->worldWidth - m_baselineWidth) > 0.01 ||
        std::abs(m_target->worldHeight - m_baselineHeight) > 0.01) {
        auto activePage = session->GetActivePage();
        if (activePage) {
            session->RecordHistoryCommand(activePage, std::make_unique<Folio::ModifyTextCommand>(
                m_target->uid,
                m_baselineText,
                m_target->text,
                m_baselineWidth,
                m_target->worldWidth,
                m_baselineHeight,
                m_target->worldHeight
            ));
            activePage->isModified = true;
            session->NotifyPageModified(activePage);
        }
        m_baselineText = m_target->text;
        m_baselineWidth = m_target->worldWidth;
        m_baselineHeight = m_target->worldHeight;
    }
}

/**
 * @brief Recomputes lines, word wrap, and total container dimensions.
 */
void TextEditorState::ReflowLayout() {
    m_lines.clear();
    if (!m_target) return;

    BLFont font = FontManager::Instance().GetFont(m_target->fontFamily, m_target->fontSize, m_target->isBold, m_target->isItalic);
    BLFontMetrics fm = FontManager::Instance().GetMetrics(font);

    // Compute line height with typographical padding
    double lineHeight = fm.ascent + fm.descent + fm.line_gap;
    if (lineHeight <= 0.001) {
        lineHeight = m_target->fontSize * 1.25;
    }
    double ascent = (fm.ascent > 0.001) ? fm.ascent : (m_target->fontSize * 0.9);

    const std::string& fullText = m_target->text;
    const double maxWrapWidth = m_target->worldWidth - 4.0; // 2mm internal margin on each side

    double curY = 2.0; // 2mm top margin
    size_t lineStart = 0;
    double maxLineWidth = 0.0;

    // Helper lambda to commit a line slice
    auto CommitLine = [&](size_t start, size_t count, const std::string& lineStr, double advanceW) {
        TextLineLayout l;
        l.startCharIndex = start;
        l.charCount = count;
        l.localY = curY;
        l.height = lineHeight;
        l.baselineY = curY + ascent;
        l.width = advanceW;
        l.text = lineStr;
        m_lines.push_back(l);

        curY += lineHeight;
        if (advanceW > maxLineWidth) {
            maxLineWidth = advanceW;
        }
    };

    if (fullText.empty()) {
        CommitLine(0, 0, "", 0.0);
    } else {
        size_t i = 0;
        while (i < fullText.size()) {
            size_t newlinePos = fullText.find('\n', i);
            std::string paragraph = (newlinePos != std::string::npos) 
                ? fullText.substr(i, newlinePos - i)
                : fullText.substr(i);
            size_t paraLen = paragraph.size();
            size_t paraStart = i;

            if (!m_target->isWrap || maxWrapWidth <= 10.0) {
                // No word wrapping — measure entire paragraph
                double w = FontManager::Instance().MeasureTextWidth(font, paragraph);
                CommitLine(paraStart, paraLen, paragraph, w);
            } else {
                // Greedy word wrapping
                std::istringstream iss(paragraph);
                std::string word;
                std::string currentLine;
                size_t currentLineStart = paraStart;

                while (iss >> word) {
                    std::string testLine = currentLine.empty() ? word : (currentLine + " " + word);
                    double testW = FontManager::Instance().MeasureTextWidth(font, testLine);

                    if (testW <= maxWrapWidth || currentLine.empty()) {
                        currentLine = testLine;
                    } else {
                        // Commit current line
                        double curW = FontManager::Instance().MeasureTextWidth(font, currentLine);
                        CommitLine(currentLineStart, currentLine.size(), currentLine, curW);
                        currentLineStart += currentLine.size() + 1; // account for space
                        currentLine = word;
                    }
                }

                if (!currentLine.empty() || paragraph.empty()) {
                    double curW = FontManager::Instance().MeasureTextWidth(font, currentLine);
                    CommitLine(currentLineStart, currentLine.size(), currentLine, curW);
                }
            }

            if (newlinePos != std::string::npos) {
                i = newlinePos + 1;
            } else {
                break;
            }
        }
    }

    // Dynamic vertical expansion: ensure text box contains all lines
    double neededHeight = curY + 4.0; // 4mm bottom margin
    if (neededHeight > m_target->worldHeight) {
        m_target->worldHeight = neededHeight;
        m_target->UpdateBounds();
    }

    // Dynamic horizontal expansion if not wrapping
    if (!m_target->isWrap) {
        double neededWidth = maxLineWidth + 8.0;
        if (neededWidth > m_target->worldWidth) {
            m_target->worldWidth = neededWidth;
            m_target->UpdateBounds();
        }
    }
}

void TextEditorState::DeleteSelection() {
    if (!HasSelection() || !m_target) return;
    size_t start = SelectionStart();
    size_t end = SelectionEnd();

    if (start < m_target->text.size()) {
        m_target->text.erase(start, end - start);
    }
    m_cursorIndex = start;
    m_selectionAnchor = start;
    m_target->SyncTextToRuns();
    m_target->isDirty = true;
    ReflowLayout();
}

void TextEditorState::OnTextInput(const std::string& utf8Text) {
    if (!m_target || utf8Text.empty()) return;

    if (HasSelection()) {
        DeleteSelection();
    }

    if (m_cursorIndex > m_target->text.size()) {
        m_cursorIndex = m_target->text.size();
    }

    m_target->text.insert(m_cursorIndex, utf8Text);
    m_cursorIndex += utf8Text.size();
    m_selectionAnchor = m_cursorIndex;

    m_target->SyncTextToRuns();
    m_target->isDirty = true;
    m_caretVisible = true;
    m_lastBlinkTimeSec = 0.0;

    ReflowLayout();
}

void TextEditorState::OnKeyDown(int32_t keycode, uint16_t keymod) {
    if (!m_target) return;

    bool shiftHeld = (keymod & SDL_KMOD_SHIFT) != 0;

    switch (keycode) {
        case SDLK_BACKSPACE: {
            if (HasSelection()) {
                DeleteSelection();
            } else if (m_cursorIndex > 0) {
                // Remove previous UTF-8 character
                size_t prevIndex = m_cursorIndex - 1;
                while (prevIndex > 0 && (m_target->text[prevIndex] & 0xC0) == 0x80) {
                    --prevIndex;
                }
                m_target->text.erase(prevIndex, m_cursorIndex - prevIndex);
                m_cursorIndex = prevIndex;
                m_selectionAnchor = m_cursorIndex;
                m_target->SyncTextToRuns();
                m_target->isDirty = true;
                ReflowLayout();
            }
            break;
        }

        case SDLK_DELETE: {
            if (HasSelection()) {
                DeleteSelection();
            } else if (m_cursorIndex < m_target->text.size()) {
                size_t nextIndex = m_cursorIndex + 1;
                while (nextIndex < m_target->text.size() && (m_target->text[nextIndex] & 0xC0) == 0x80) {
                    ++nextIndex;
                }
                m_target->text.erase(m_cursorIndex, nextIndex - m_cursorIndex);
                m_target->SyncTextToRuns();
                m_target->isDirty = true;
                ReflowLayout();
            }
            break;
        }

        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            OnTextInput("\n");
            break;
        }

        case SDLK_LEFT: {
            if (!shiftHeld && HasSelection()) {
                m_cursorIndex = SelectionStart();
                ClearSelection();
            } else if (m_cursorIndex > 0) {
                size_t prevIndex = m_cursorIndex - 1;
                while (prevIndex > 0 && (m_target->text[prevIndex] & 0xC0) == 0x80) {
                    --prevIndex;
                }
                MoveCursorTo(prevIndex, shiftHeld);
            }
            break;
        }

        case SDLK_RIGHT: {
            if (!shiftHeld && HasSelection()) {
                m_cursorIndex = SelectionEnd();
                ClearSelection();
            } else if (m_cursorIndex < m_target->text.size()) {
                size_t nextIndex = m_cursorIndex + 1;
                while (nextIndex < m_target->text.size() && (m_target->text[nextIndex] & 0xC0) == 0x80) {
                    ++nextIndex;
                }
                MoveCursorTo(nextIndex, shiftHeld);
            }
            break;
        }

        case SDLK_HOME: {
            // Find start of current line
            for (const auto& l : m_lines) {
                if (m_cursorIndex >= l.startCharIndex && m_cursorIndex <= l.startCharIndex + l.charCount) {
                    MoveCursorTo(l.startCharIndex, shiftHeld);
                    break;
                }
            }
            break;
        }

        case SDLK_END: {
            // Find end of current line
            for (const auto& l : m_lines) {
                if (m_cursorIndex >= l.startCharIndex && m_cursorIndex <= l.startCharIndex + l.charCount) {
                    MoveCursorTo(l.startCharIndex + l.charCount, shiftHeld);
                    break;
                }
            }
            break;
        }

        case SDLK_UP: {
            Point2D pt = GetCursorWorldPos();
            double lineHeight = GetCaretHeight();
            OnMouseDown(pt.x, pt.y - lineHeight * 0.5, shiftHeld);
            break;
        }

        case SDLK_DOWN: {
            Point2D pt = GetCursorWorldPos();
            double lineHeight = GetCaretHeight();
            OnMouseDown(pt.x, pt.y + lineHeight * 1.5, shiftHeld);
            break;
        }

        default:
            break;
    }

    m_caretVisible = true;
    m_lastBlinkTimeSec = 0.0;
}

void TextEditorState::MoveCursorTo(size_t newIndex, bool extendSelection) {
    m_cursorIndex = (std::min)(newIndex, m_target ? m_target->text.size() : size_t(0));
    if (!extendSelection) {
        m_selectionAnchor = m_cursorIndex;
    }
}

size_t TextEditorState::FindClosestCharIndex(double worldX, double worldY) const {
    if (!m_target || m_lines.empty()) return 0;

    double localX = worldX - m_target->worldX - 2.0; // subtract left margin
    double localY = worldY - m_target->worldY;

    // 1. Locate line by localY
    const TextLineLayout* bestLine = &m_lines[0];
    for (const auto& l : m_lines) {
        if (localY >= l.localY && localY < l.localY + l.height) {
            bestLine = &l;
            break;
        }
        if (localY >= l.localY + l.height) {
            bestLine = &l; // furthest down so far
        }
    }

    // 2. Locate character within bestLine using font measurements
    BLFont font = FontManager::Instance().GetFont(m_target->fontFamily, m_target->fontSize, m_target->isBold, m_target->isItalic);
    size_t closestOffset = 0;
    double minDiff = 1e9;

    for (size_t c = 0; c <= bestLine->text.size(); ++c) {
        std::string sub = bestLine->text.substr(0, c);
        double advance = FontManager::Instance().MeasureTextWidth(font, sub);
        double diff = std::abs(advance - localX);
        if (diff < minDiff) {
            minDiff = diff;
            closestOffset = c;
        }
    }

    return bestLine->startCharIndex + closestOffset;
}

void TextEditorState::OnMouseDown(double worldX, double worldY, bool shiftSelect) {
    size_t idx = FindClosestCharIndex(worldX, worldY);
    MoveCursorTo(idx, shiftSelect);
    m_caretVisible = true;
    m_lastBlinkTimeSec = 0.0;
}

void TextEditorState::OnMouseDrag(double worldX, double worldY) {
    size_t idx = FindClosestCharIndex(worldX, worldY);
    m_cursorIndex = idx; // extends selection while leaving anchor unchanged
}

void TextEditorState::UpdateBlink(double currentSec) {
    // 530ms blink interval matching standard OS caret blink
    double phase = std::fmod(currentSec, 1.06);
    m_caretVisible = (phase < 0.53);
}

Point2D TextEditorState::GetCursorWorldPos() const {
    if (!m_target || m_lines.empty()) {
        return Point2D{m_target ? m_target->worldX + 2.0 : 0.0, m_target ? m_target->worldY + 2.0 : 0.0};
    }

    BLFont font = FontManager::Instance().GetFont(m_target->fontFamily, m_target->fontSize, m_target->isBold, m_target->isItalic);

    for (const auto& l : m_lines) {
        if (m_cursorIndex >= l.startCharIndex && m_cursorIndex <= l.startCharIndex + l.charCount) {
            size_t localOffset = m_cursorIndex - l.startCharIndex;
            std::string sub = l.text.substr(0, localOffset);
            double adv = FontManager::Instance().MeasureTextWidth(font, sub);
            return Point2D{m_target->worldX + 2.0 + adv, m_target->worldY + l.localY};
        }
    }

    // Fallback: end of last line
    const auto& lastLine = m_lines.back();
    double adv = FontManager::Instance().MeasureTextWidth(font, lastLine.text);
    return Point2D{m_target->worldX + 2.0 + adv, m_target->worldY + lastLine.localY};
}

double TextEditorState::GetCaretHeight() const {
    if (m_lines.empty() || !m_target) {
        return m_target ? m_target->fontSize : 16.0;
    }
    return m_lines[0].height;
}

std::vector<AABB> TextEditorState::GetSelectionBoxes() const {
    std::vector<AABB> boxes;
    if (!HasSelection() || !m_target || m_lines.empty()) return boxes;

    size_t selStart = SelectionStart();
    size_t selEnd = SelectionEnd();

    BLFont font = FontManager::Instance().GetFont(m_target->fontFamily, m_target->fontSize, m_target->isBold, m_target->isItalic);

    for (const auto& l : m_lines) {
        size_t lineEnd = l.startCharIndex + l.charCount;
        if (selEnd <= l.startCharIndex || selStart >= lineEnd) {
            continue; // line not in selection
        }

        size_t boxStart = (std::max)(selStart, l.startCharIndex) - l.startCharIndex;
        size_t boxEnd = (std::min)(selEnd, lineEnd) - l.startCharIndex;

        double x0 = FontManager::Instance().MeasureTextWidth(font, l.text.substr(0, boxStart));
        double x1 = FontManager::Instance().MeasureTextWidth(font, l.text.substr(0, boxEnd));

        double wx0 = m_target->worldX + 2.0 + x0;
        double wx1 = m_target->worldX + 2.0 + x1;
        double wy0 = m_target->worldY + l.localY;
        double wy1 = wy0 + l.height;

        boxes.push_back(AABB(wx0, wy0, wx1, wy1));
    }

    return boxes;
}

} // namespace Folio
