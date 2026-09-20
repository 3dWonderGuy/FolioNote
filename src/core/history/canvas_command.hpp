#pragma once
/**
 * =========================================================================================
 * @file canvas_command.hpp
 * @brief Command interfaces and concrete action records for the Undo/Redo subsystem
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN:
 * Follows the Command Pattern to encapsulate canvas mutations into discrete, reversible units.
 * Supports:
 * 1. Single Object Insertion: Pen strokes, images, text boxes, shapes.
 * 2. Multi-Object Deletion: Delete key, cut, selection removal.
 * 3. Compound Continuous Eraser Actions: Groups multiple erased strokes and sliced fragments
 *    from a single continuous pen gesture into one single undoable action.
 * 4. Multi-Object Transformation: Gizmo moving, scaling, and rotating.
 */

#include <memory>
#include <vector>
#include <string>
#include <utility>
#include "core/spatial/aabb.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container.hpp"

class CanvasPage;
class CanvasEngine;

namespace Folio {

/**
 * @class ICanvasCommand
 * @brief Abstract base class for all reversible canvas mutations.
 */
class ICanvasCommand {
public:
    virtual ~ICanvasCommand() = default;

    /// Apply or re-apply the mutation to the target page
    virtual void Execute(CanvasPage& page, CanvasEngine* engine = nullptr) = 0;

    /// Reverse the mutation on the target page
    virtual void Undo(CanvasPage& page, CanvasEngine* engine = nullptr) = 0;

    /// Bounding box in world coordinates affected by this command
    virtual AABB GetTargetBounds() const = 0;

    /// Human-readable label for debugging and history inspection
    virtual std::string GetName() const = 0;
};

/**
 * @class AddObjectCommand
 * @brief Records the creation/insertion of a canvas object (stroke, image, text box, shape).
 */
class AddObjectCommand : public ICanvasCommand {
public:
    std::shared_ptr<CanvasObject> object;

    explicit AddObjectCommand(std::shared_ptr<CanvasObject> obj);
    void Execute(CanvasPage& page, CanvasEngine* engine) override;
    void Undo(CanvasPage& page, CanvasEngine* engine) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

/**
 * @class AddObjectsCommand
 * @brief Records the creation/insertion of multiple objects simultaneously (e.g. Paste, Duplicate).
 */
class AddObjectsCommand : public ICanvasCommand {
public:
    std::vector<std::shared_ptr<CanvasObject>> objects;

    explicit AddObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs);
    explicit AddObjectsCommand(std::shared_ptr<CanvasObject> obj);
    void Execute(CanvasPage& page, CanvasEngine* engine) override;
    void Undo(CanvasPage& page, CanvasEngine* engine) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

/**
 * @class RemoveObjectsCommand
 * @brief Records the deletion of one or more objects (e.g. Delete key, cut, selection removal).
 */
class RemoveObjectsCommand : public ICanvasCommand {
public:
    std::vector<std::shared_ptr<CanvasObject>> objects;

    explicit RemoveObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs);
    explicit RemoveObjectsCommand(std::shared_ptr<CanvasObject> obj);
    void Execute(CanvasPage& page, CanvasEngine* engine) override;
    void Undo(CanvasPage& page, CanvasEngine* engine) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

/**
 * @class BatchEraseCommand
 * @brief Compound command capturing an entire continuous eraser gesture.
 *
 * If the user drags the eraser across 20 strokes in one continuous pen stroke,
 * this command groups all deleted strokes and sliced fragments into a single
 * atomic undoable action.
 */
class BatchEraseCommand : public ICanvasCommand {
public:
    struct SlicedStrokeEntry {
        std::shared_ptr<InkContainer> originalStroke;
        std::vector<std::shared_ptr<InkContainer>> generatedFragments;
    };

    std::vector<std::shared_ptr<CanvasObject>> deletedObjects;
    std::vector<SlicedStrokeEntry> slicedStrokes;

    BatchEraseCommand() = default;
    BatchEraseCommand(
        std::vector<std::shared_ptr<CanvasObject>> deleted,
        std::vector<SlicedStrokeEntry> sliced
    );

    void Execute(CanvasPage& page, CanvasEngine* engine) override;
    void Undo(CanvasPage& page, CanvasEngine* engine) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
    [[nodiscard]] bool IsEmpty() const;
};

/**
 * @class TransformObjectsCommand
 * @brief Records moving, resizing, or rotating objects via the selection gizmo.
 */
class TransformObjectsCommand : public ICanvasCommand {
public:
    struct Entry {
        uint32_t uid = 0;
        std::shared_ptr<CanvasObject> beforeState;
        std::shared_ptr<CanvasObject> afterState;
    };

    std::vector<Entry> entries;

    TransformObjectsCommand() = default;
    explicit TransformObjectsCommand(std::vector<Entry> entries);

    void Execute(CanvasPage& page, CanvasEngine* engine) override;
    void Undo(CanvasPage& page, CanvasEngine* engine) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

/**
 * @class GroupObjectsCommand
 * @brief Records grouping multiple objects into a logical unit by assigning a shared groupId.
 */
class GroupObjectsCommand : public ICanvasCommand {
public:
    struct Entry {
        std::shared_ptr<CanvasObject> object;
        std::string previousGroupId;
    };
    std::vector<Entry> entries;
    std::string newGroupId;

    GroupObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs, std::string groupId);

    void Execute(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    void Undo(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

/**
 * @class UngroupObjectsCommand
 * @brief Records breaking an existing object group by clearing their shared groupId.
 */
class UngroupObjectsCommand : public ICanvasCommand {
public:
    struct Entry {
        std::shared_ptr<CanvasObject> object;
        std::string previousGroupId;
    };
    std::vector<Entry> entries;

    explicit UngroupObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs);

    void Execute(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    void Undo(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

/**
 * @class MacroCommand
 * @brief Composite command encapsulating multiple distinct canvas mutations into one atomic undo/redo action.
 */
class MacroCommand : public ICanvasCommand {
public:
    std::string macroName;
    std::vector<std::unique_ptr<ICanvasCommand>> subCommands;

    explicit MacroCommand(std::string name = "Macro Action");

    void AddCommand(std::unique_ptr<ICanvasCommand> cmd);
    void Execute(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    void Undo(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
    [[nodiscard]] bool IsEmpty() const noexcept;
};

/**
 * @class LockObjectsCommand
 * @brief Records locking objects as background templates or unlocking them.
 */
class LockObjectsCommand : public ICanvasCommand {
public:
    struct State {
        std::shared_ptr<CanvasObject> object;
        bool wasLocked = false;
        bool wasSelectable = true;
        int32_t wasZOrder = 1;
    };
    std::vector<State> states;
    bool targetLock = true;
    bool targetSelectable = false;
    int32_t targetZOrder = 0;

    LockObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs,
                       bool lock, bool selectable, int32_t zOrder = 0);

    void Execute(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    void Undo(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

/**
 * @class ModifyTextCommand
 * @brief Records reversible text string edits and container dimension changes for TextBoxObject.
 *
 * GENERAL WORKING PROCESS & INVARIANTS:
 * - Stores previous and replacement text strings along with bounding box dimensions (width and height).
 * - On Execute(): sets target text box to newText, recalibrates bounds, refreshes spatial index, and updates active editor state.
 * - On Undo(): reverts target text box to previousText, restores previous dimensions, and refreshes R-Tree spatial index.
 * - Input: target UID, previous/new text strings, previous/new width and height in millimeters.
 * - Output: bidirectional state mutation on the target CanvasPage.
 */
class ModifyTextCommand : public ICanvasCommand {
public:
    uint32_t textBoxUid = 0;
    std::string previousText;
    std::string newText;
    double previousWidth = 0.0;
    double newWidth = 0.0;
    double previousHeight = 0.0;
    double newHeight = 0.0;

    ModifyTextCommand(uint32_t uid, std::string prevText, std::string nextText,
                      double prevW, double nextW, double prevH, double nextH);

    void Execute(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    void Undo(CanvasPage& page, CanvasEngine* engine = nullptr) override;
    AABB GetTargetBounds() const override;
    std::string GetName() const override;
};

// =============================================================================
// CONVENIENCE TYPE ALIASES (Matching Universal ICommand Design)
// =============================================================================
using ICommand = ICanvasCommand;
using DeleteCommand = RemoveObjectsCommand;
using TransformCommand = TransformObjectsCommand;

} // namespace Folio

// Expose standard aliases to global namespace for convenience
using Folio::ICommand;
using Folio::DeleteCommand;
using Folio::TransformCommand;
using Folio::ModifyTextCommand;