#pragma once
#include <vector>
#include <memory>

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
};

class CommandManager {
public:
    std::vector<std::unique_ptr<ICommand>> undoStack;
    std::vector<std::unique_ptr<ICommand>> redoStack;

    void ExecuteCommand(std::unique_ptr<ICommand> cmd) {
        cmd->Execute();
        undoStack.push_back(std::move(cmd));
        redoStack.clear(); // Clear redo on new action
    }

    void Undo() {
        if (undoStack.empty()) return;
        auto cmd = std::move(undoStack.back());
        undoStack.pop_back();
        cmd->Undo();
        redoStack.push_back(std::move(cmd));
    }

    void Redo() {
        if (redoStack.empty()) return;
        auto cmd = std::move(redoStack.back());
        redoStack.pop_back();
        cmd->Execute();
        undoStack.push_back(std::move(cmd));
    }

    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
};