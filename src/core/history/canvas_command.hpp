#pragma once
#include <memory>
#include <string>
#include "core/spatial/aabb.hpp"

class CanvasEngine; // Forward declaration

class ICanvasCommand {
public:
    virtual ~ICanvasCommand() = default;

    // Apply or re-apply the action
    virtual void Execute(CanvasEngine& engine) = 0;

    // Reverse the action
    virtual void Undo(CanvasEngine& engine) = 0;

    // Bounding region in world coordinates for automatic viewport focus
    virtual AABB GetTargetBounds() const = 0;

    // Optional debug/UI label (e.g., "Move Object", "Change Color")
    virtual std::string GetName() const = 0;
};