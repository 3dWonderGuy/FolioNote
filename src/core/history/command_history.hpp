#pragma once
/**
 * =========================================================================================
 * @file command_history.hpp
 * @brief Per-page undo/redo stack manager enforcing bounded history and transactional execution
 * =========================================================================================
 */

#include <vector>
#include <memory>
#include <cstddef>
#include "core/history/canvas_command.hpp"

class CanvasPage;
class CanvasEngine;

namespace Folio {

class CommandHistory {
private:
    std::vector<std::unique_ptr<ICanvasCommand>> undoStack;
    std::vector<std::unique_ptr<ICanvasCommand>> redoStack;
    size_t maxHistoryDepth = 100;

public:
    explicit CommandHistory(size_t maxDepth = 100) : maxHistoryDepth(maxDepth) {}

    /**
     * @brief Records an action that was already executed live (e.g. stroke drawn, erase completed).
     * Pushes to the undo stack and clears the redo stack.
     */
    void RecordCommand(std::unique_ptr<ICanvasCommand> command) {
        if (!command) return;

        undoStack.push_back(std::move(command));
        redoStack.clear();

        // Enforce maximum memory depth bound
        if (undoStack.size() > maxHistoryDepth) {
            undoStack.erase(undoStack.begin());
        }
    }

    /**
     * @brief Executes a new command and pushes it onto the undo stack.
     */
    void ExecuteCommand(std::unique_ptr<ICanvasCommand> command, CanvasPage& page, CanvasEngine* engine = nullptr) {
        if (!command) return;

        command->Execute(page, engine);
        undoStack.push_back(std::move(command));
        redoStack.clear();

        if (undoStack.size() > maxHistoryDepth) {
            undoStack.erase(undoStack.begin());
        }
    }

    /**
     * @brief Undoes the most recent command on the page.
     * @return true if an action was undone; false if undo stack is empty.
     */
    bool Undo(CanvasPage& page, CanvasEngine* engine = nullptr) {
        if (undoStack.empty()) return false;

        auto cmd = std::move(undoStack.back());
        undoStack.pop_back();

        cmd->Undo(page, engine);
        redoStack.push_back(std::move(cmd));
        return true;
    }

    /**
     * @brief Redoes the most recently undone command on the page.
     * @return true if an action was redone; false if redo stack is empty.
     */
    bool Redo(CanvasPage& page, CanvasEngine* engine = nullptr) {
        if (redoStack.empty()) return false;

        auto cmd = std::move(redoStack.back());
        redoStack.pop_back();

        cmd->Execute(page, engine);
        undoStack.push_back(std::move(cmd));
        return true;
    }

    [[nodiscard]] bool CanUndo() const noexcept { return !undoStack.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redoStack.empty(); }

    [[nodiscard]] size_t GetUndoCount() const noexcept { return undoStack.size(); }
    [[nodiscard]] size_t GetRedoCount() const noexcept { return redoStack.size(); }

    void Clear() {
        undoStack.clear();
        redoStack.clear();
    }
};

} // namespace Folio

using Folio::CommandHistory;