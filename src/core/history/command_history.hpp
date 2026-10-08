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

/**
 * @class CommandHistory
 * @brief Manages bounded undo and redo stacks for a canvas page.
 */
class CommandHistory {
public:
    explicit CommandHistory(size_t maxDepth = 100);
    ~CommandHistory() = default;

    CommandHistory(const CommandHistory&) = delete;
    CommandHistory& operator=(const CommandHistory&) = delete;
    CommandHistory(CommandHistory&&) noexcept = default;
    CommandHistory& operator=(CommandHistory&&) noexcept = default;

    /**
     * @brief Records an action that was already executed live (e.g. stroke drawn, erase completed).
     * Pushes to the undo stack, bounds depth, and clears the redo stack.
     */
    void RecordCommand(std::unique_ptr<ICanvasCommand> command);

    /**
     * @brief Executes a new command and pushes it onto the undo stack.
     */
    void ExecuteCommand(std::unique_ptr<ICanvasCommand> command, CanvasPage& page, CanvasEngine* engine = nullptr);

    /**
     * @brief Undoes the most recent command on the page.
     * @return true if an action was undone; false if undo stack was empty.
     */
    bool Undo(CanvasPage& page, CanvasEngine* engine = nullptr);

    /**
     * @brief Redoes the most recently undone command on the page.
     * @return true if an action was redone; false if redo stack was empty.
     */
    bool Redo(CanvasPage& page, CanvasEngine* engine = nullptr);

    [[nodiscard]] bool CanUndo() const noexcept { return !undoStack.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redoStack.empty(); }
    [[nodiscard]] size_t GetUndoCount() const noexcept { return undoStack.size(); }
    [[nodiscard]] size_t GetRedoCount() const noexcept { return redoStack.size(); }
    [[nodiscard]] size_t GetMaxHistoryDepth() const noexcept { return maxHistoryDepth; }
    void SetMaxHistoryDepth(size_t maxDepth) noexcept { maxHistoryDepth = maxDepth; }

    void Clear();

private:
    std::vector<std::unique_ptr<ICanvasCommand>> undoStack;
    std::vector<std::unique_ptr<ICanvasCommand>> redoStack;
    size_t maxHistoryDepth{100};
};

using CommandManager = CommandHistory;

} // namespace Folio

using Folio::CommandHistory;
using Folio::CommandManager;