/**
 * =========================================================================================
 * @file command_history.cpp
 * @brief Implementation of CommandHistory undo/redo stack manager
 * =========================================================================================
 */

#include "core/history/command_history.hpp"

namespace Folio {

CommandHistory::CommandHistory(size_t maxDepth)
    : maxHistoryDepth(maxDepth > 0 ? maxDepth : 1) {}

void CommandHistory::RecordCommand(std::unique_ptr<ICanvasCommand> command) {
    if (!command) return;

    undoStack.push_back(std::move(command));
    redoStack.clear();

    // Enforce maximum memory depth bound
    if (undoStack.size() > maxHistoryDepth) {
        undoStack.erase(undoStack.begin());
    }
}

void CommandHistory::ExecuteCommand(std::unique_ptr<ICanvasCommand> command, CanvasPage& page, CanvasEngine* engine) {
    if (!command) return;

    command->Execute(page, engine);
    undoStack.push_back(std::move(command));
    redoStack.clear();

    if (undoStack.size() > maxHistoryDepth) {
        undoStack.erase(undoStack.begin());
    }
}

bool CommandHistory::Undo(CanvasPage& page, CanvasEngine* engine) {
    if (undoStack.empty()) return false;

    auto cmd = std::move(undoStack.back());
    undoStack.pop_back();

    cmd->Undo(page, engine);
    redoStack.push_back(std::move(cmd));
    return true;
}

bool CommandHistory::Redo(CanvasPage& page, CanvasEngine* engine) {
    if (redoStack.empty()) return false;

    auto cmd = std::move(redoStack.back());
    redoStack.pop_back();

    cmd->Execute(page, engine);
    undoStack.push_back(std::move(cmd));
    return true;
}

void CommandHistory::Clear() {
    undoStack.clear();
    redoStack.clear();
}

} // namespace Folio
