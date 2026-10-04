#pragma once

#include <string>
#include <vector>
#include "core/md_engine/md_types.hpp"

namespace Folio {

/**
 * @class MdEditorState
 * @brief Headless text buffer editor controller with undo/redo, caret navigation,
 * selection tracking, and Markdown formatting shortcuts.
 */
class MdEditorState {
public:
    std::string markdownText;
    MdSelection selection;
    MdCaret caret;
    bool isModified = false;

    MdEditorState();

    void SetText(const std::string& text);
    [[nodiscard]] const std::string& GetText() const noexcept { return markdownText; }

    // =========================================================================
    // EDITING MUTATIONS
    // =========================================================================
    void InsertText(const std::string& str);
    void Backspace();
    void DeleteForward();
    void DeleteSelection();

    // =========================================================================
    // NAVIGATION & SELECTION
    // =========================================================================
    void MoveCursor(int deltaChars, bool extendSelection);
    void SetCursorOffset(size_t offset, bool extendSelection);
    void MoveToLineStart(bool extendSelection);
    void MoveToLineEnd(bool extendSelection);
    void SelectAll();
    void ClearSelection();
    [[nodiscard]] std::string GetSelectedText() const;

    // =========================================================================
    // CARET BLINK TELEMETRY
    // =========================================================================
    void UpdateBlink(double currentSec);

    // =========================================================================
    // UNDO / REDO HISTORY
    // =========================================================================
    void PushUndoSnapshot();
    bool Undo();
    bool Redo();
    [[nodiscard]] bool CanUndo() const noexcept { return !m_undoStack.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !m_redoStack.empty(); }

    // =========================================================================
    // MARKDOWN FORMATTING SHORTCUTS
    // =========================================================================
    void WrapSelectionWith(const std::string& prefix, const std::string& suffix);
    void ToggleBold();
    void ToggleItalic();
    void ToggleStrikethrough();
    void ToggleCode();
    void InsertHeading(int level);
    void InsertTable(int rows = 3, int cols = 3);
    void InsertCodeBlock(const std::string& lang = "cpp");

private:
    std::vector<std::string> m_undoStack;
    std::vector<std::string> m_redoStack;
    double m_lastBlinkTimeSec = 0.0;
};

} // namespace Folio
