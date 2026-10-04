#include "core/md_engine/md_editor_state.hpp"
#include <algorithm>
#include <sstream>

namespace Folio {

MdEditorState::MdEditorState() {
    caret.visible = true;
}

void MdEditorState::SetText(const std::string& text) {
    markdownText = text;
    selection.Clear();
    selection.cursorOffset = 0;
    selection.anchorOffset = 0;
    caret.offset = 0;
    m_undoStack.clear();
    m_redoStack.clear();
    isModified = false;
}

void MdEditorState::PushUndoSnapshot() {
    constexpr size_t MAX_UNDO = 100;
    if (m_undoStack.size() >= MAX_UNDO) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_undoStack.push_back(markdownText);
    m_redoStack.clear();
    isModified = true;
}

void MdEditorState::InsertText(const std::string& str) {
    if (str.empty()) return;
    PushUndoSnapshot();

    if (selection.hasSelection) {
        DeleteSelection();
    }

    size_t pos = std::clamp(selection.cursorOffset, static_cast<size_t>(0), markdownText.size());
    markdownText.insert(pos, str);
    selection.cursorOffset = pos + str.size();
    selection.anchorOffset = selection.cursorOffset;
    selection.hasSelection = false;
    caret.offset = selection.cursorOffset;
    caret.visible = true;
}

void MdEditorState::DeleteSelection() {
    if (!selection.hasSelection || markdownText.empty()) return;

    size_t minO = selection.MinOffset();
    size_t maxO = selection.MaxOffset();
    if (minO >= markdownText.size()) return;
    if (maxO > markdownText.size()) maxO = markdownText.size();

    markdownText.erase(minO, maxO - minO);
    selection.cursorOffset = minO;
    selection.anchorOffset = minO;
    selection.hasSelection = false;
    caret.offset = minO;
    caret.visible = true;
}

void MdEditorState::Backspace() {
    if (selection.hasSelection) {
        PushUndoSnapshot();
        DeleteSelection();
        return;
    }

    if (selection.cursorOffset == 0 || markdownText.empty()) return;
    PushUndoSnapshot();

    size_t pos = selection.cursorOffset;
    // Basic UTF-8 multibyte backward skip
    size_t eraseCount = 1;
    if (pos >= 1 && (static_cast<uint8_t>(markdownText[pos - 1]) & 0xC0) == 0x80) {
        while (pos >= eraseCount && (static_cast<uint8_t>(markdownText[pos - eraseCount]) & 0xC0) == 0x80) {
            ++eraseCount;
        }
    }

    size_t delStart = (pos >= eraseCount) ? (pos - eraseCount) : 0;
    markdownText.erase(delStart, pos - delStart);
    selection.cursorOffset = delStart;
    selection.anchorOffset = delStart;
    selection.hasSelection = false;
    caret.offset = delStart;
    caret.visible = true;
}

void MdEditorState::DeleteForward() {
    if (selection.hasSelection) {
        PushUndoSnapshot();
        DeleteSelection();
        return;
    }

    if (selection.cursorOffset >= markdownText.size()) return;
    PushUndoSnapshot();

    size_t pos = selection.cursorOffset;
    size_t eraseCount = 1;
    uint8_t lead = static_cast<uint8_t>(markdownText[pos]);
    if ((lead & 0xE0) == 0xC0) eraseCount = 2;
    else if ((lead & 0xF0) == 0xE0) eraseCount = 3;
    else if ((lead & 0xF8) == 0xF0) eraseCount = 4;

    markdownText.erase(pos, eraseCount);
    caret.offset = selection.cursorOffset;
    caret.visible = true;
}

void MdEditorState::MoveCursor(int deltaChars, bool extendSelection) {
    if (deltaChars == 0) return;

    int newPos = static_cast<int>(selection.cursorOffset) + deltaChars;
    newPos = std::clamp(newPos, 0, static_cast<int>(markdownText.size()));

    SetCursorOffset(static_cast<size_t>(newPos), extendSelection);
}

void MdEditorState::SetCursorOffset(size_t offset, bool extendSelection) {
    offset = std::clamp(offset, static_cast<size_t>(0), markdownText.size());
    selection.cursorOffset = offset;
    if (!extendSelection) {
        selection.anchorOffset = offset;
        selection.hasSelection = false;
    } else {
        selection.hasSelection = (selection.anchorOffset != selection.cursorOffset);
    }
    caret.offset = offset;
    caret.visible = true;
}

void MdEditorState::MoveToLineStart(bool extendSelection) {
    if (markdownText.empty() || selection.cursorOffset == 0) return;
    size_t pos = selection.cursorOffset;
    size_t prevNewline = markdownText.rfind('\n', (pos > 0) ? pos - 1 : 0);
    size_t lineStart = (prevNewline == std::string::npos) ? 0 : prevNewline + 1;
    SetCursorOffset(lineStart, extendSelection);
}

void MdEditorState::MoveToLineEnd(bool extendSelection) {
    if (markdownText.empty()) return;
    size_t pos = selection.cursorOffset;
    size_t nextNewline = markdownText.find('\n', pos);
    size_t lineEnd = (nextNewline == std::string::npos) ? markdownText.size() : nextNewline;
    SetCursorOffset(lineEnd, extendSelection);
}

void MdEditorState::SelectAll() {
    selection.anchorOffset = 0;
    selection.cursorOffset = markdownText.size();
    selection.hasSelection = !markdownText.empty();
    caret.offset = selection.cursorOffset;
    caret.visible = true;
}

void MdEditorState::ClearSelection() {
    selection.Clear();
}

std::string MdEditorState::GetSelectedText() const {
    if (!selection.hasSelection || markdownText.empty()) return "";
    size_t minO = selection.MinOffset();
    size_t maxO = selection.MaxOffset();
    return markdownText.substr(minO, maxO - minO);
}

void MdEditorState::UpdateBlink(double currentSec) {
    if (currentSec - m_lastBlinkTimeSec >= 0.50) {
        caret.visible = !caret.visible;
        m_lastBlinkTimeSec = currentSec;
    }
}

bool MdEditorState::Undo() {
    if (m_undoStack.empty()) return false;
    m_redoStack.push_back(markdownText);
    markdownText = m_undoStack.back();
    m_undoStack.pop_back();

    selection.cursorOffset = std::min(selection.cursorOffset, markdownText.size());
    selection.anchorOffset = selection.cursorOffset;
    selection.hasSelection = false;
    caret.offset = selection.cursorOffset;
    isModified = true;
    return true;
}

bool MdEditorState::Redo() {
    if (m_redoStack.empty()) return false;
    m_undoStack.push_back(markdownText);
    markdownText = m_redoStack.back();
    m_redoStack.pop_back();

    selection.cursorOffset = std::min(selection.cursorOffset, markdownText.size());
    selection.anchorOffset = selection.cursorOffset;
    selection.hasSelection = false;
    caret.offset = selection.cursorOffset;
    isModified = true;
    return true;
}

void MdEditorState::WrapSelectionWith(const std::string& prefix, const std::string& suffix) {
    PushUndoSnapshot();
    if (selection.hasSelection) {
        size_t minO = selection.MinOffset();
        size_t maxO = selection.MaxOffset();
        std::string selected = markdownText.substr(minO, maxO - minO);
        std::string replacement = prefix + selected + suffix;
        markdownText.replace(minO, maxO - minO, replacement);
        selection.anchorOffset = minO;
        selection.cursorOffset = minO + replacement.size();
    } else {
        size_t pos = selection.cursorOffset;
        std::string tag = prefix + suffix;
        markdownText.insert(pos, tag);
        selection.cursorOffset = pos + prefix.size();
        selection.anchorOffset = selection.cursorOffset;
        selection.hasSelection = false;
    }
    caret.offset = selection.cursorOffset;
}

void MdEditorState::ToggleBold() {
    WrapSelectionWith("**", "**");
}

void MdEditorState::ToggleItalic() {
    WrapSelectionWith("*", "*");
}

void MdEditorState::ToggleStrikethrough() {
    WrapSelectionWith("~~", "~~");
}

void MdEditorState::ToggleCode() {
    WrapSelectionWith("`", "`");
}

void MdEditorState::InsertHeading(int level) {
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    std::string prefix(level, '#');
    prefix += " ";
    InsertText("\n" + prefix);
}

void MdEditorState::InsertTable(int rows, int cols) {
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;

    std::ostringstream ss;
    ss << "\n|";
    for (int c = 1; c <= cols; ++c) {
        ss << " Column " << c << " |";
    }
    ss << "\n|";
    for (int c = 1; c <= cols; ++c) {
        ss << " -------- |";
    }
    for (int r = 1; r <= rows; ++r) {
        ss << "\n|";
        for (int c = 1; c <= cols; ++c) {
            ss << " Cell " << r << "," << c << " |";
        }
    }
    ss << "\n\n";
    InsertText(ss.str());
}

void MdEditorState::InsertCodeBlock(const std::string& lang) {
    InsertText("\n```" + lang + "\n// code here\n```\n");
}

} // namespace Folio
