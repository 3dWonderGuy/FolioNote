#pragma once
#include <vector>
#include <memory>
#include "core/history/canvas_command.hpp"

class CanvasEngine;

class CommandHistory {
private:
    std::vector<std::unique_ptr<ICanvasCommand>> undoStack;
    std::vector<std::unique_ptr<ICanvasCommand>> redoStack;
    size_t maxHistoryDepth = 100;

public:
    explicit CommandHistory(size_t maxDepth = 100) : maxHistoryDepth(maxDepth) {}

    // Executes a new command and pushes it onto the undo stack (clearing redo)
    void ExecuteCommand(std::unique_ptr<ICanvasCommand> command, CanvasEngine& engine) {
        if (!command) return;

        command->Execute(engine);
        undoStack.push_back(std::move(command));
        redoStack.clear();

        // Enforce history limit
        if (undoStack.size() > maxHistoryDepth) {
            undoStack.erase(undoStack.begin());
        }
    }

    // Undoes the top command
    bool Undo(CanvasEngine& engine) {
        if (undoStack.empty()) return false;

        auto cmd = std::move(undoStack.back());
        undoStack.pop_back();

        cmd->Undo(engine);
        redoStack.push_back(std::move(cmd));
        return true;
    }

    // Redoes the top command
    bool Redo(CanvasEngine& engine) {
        if (redoStack.empty()) return false;

        auto cmd = std::move(redoStack.back());
        redoStack.pop_back();

        cmd->Execute(engine);
        undoStack.push_back(std::move(cmd));
        return true;
    }

    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }

    void Clear() {
        undoStack.clear();
        redoStack.clear();
    }
};