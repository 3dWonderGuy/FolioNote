/**
 * =========================================================================================
 * @file canvas_command.cpp
 * @brief Implementation of concrete canvas mutation commands for Undo/Redo
 * =========================================================================================
 */

#include "core/history/canvas_command.hpp"
#include "core/document/canvas_page.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/objects/attachment_container.hpp"
#include "utils/logger.hpp"

namespace Folio {

// =========================================================================================
// AddObjectCommand Implementation
// =========================================================================================

AddObjectCommand::AddObjectCommand(std::shared_ptr<CanvasObject> obj)
    : object(std::move(obj)) {}

void AddObjectCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    if (object) {
        object->isVisible = 1;
        object->isSelectable = 1;
        object->isSelected = 0;
        page.AddObject(object);
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void AddObjectCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    if (object) {
        object->isVisible = 0;
        object->isSelectable = 0;
        object->isSelected = 0;
        page.RemoveObject(object);
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB AddObjectCommand::GetTargetBounds() const {
    return object ? object->bounds : AABB();
}

std::string AddObjectCommand::GetName() const {
    return "Add Object";
}

// =========================================================================================
// AddObjectsCommand Implementation
// =========================================================================================

AddObjectsCommand::AddObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs)
    : objects(std::move(objs)) {}

AddObjectsCommand::AddObjectsCommand(std::shared_ptr<CanvasObject> obj) {
    if (obj) objects.push_back(std::move(obj));
}

void AddObjectsCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (const auto& obj : objects) {
        if (obj) {
            obj->isVisible = 1;
            obj->isSelectable = 1;
            obj->isSelected = 0;
            page.AddObject(obj);
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void AddObjectsCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    for (const auto& obj : objects) {
        if (obj) {
            obj->isVisible = 0;
            obj->isSelectable = 0;
            obj->isSelected = 0;
            page.RemoveObject(obj);
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB AddObjectsCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& obj : objects) {
        if (obj) box.Merge(obj->bounds);
    }
    return box;
}

std::string AddObjectsCommand::GetName() const {
    return "Add Objects";
}

// =========================================================================================
// RemoveObjectsCommand Implementation
// =========================================================================================

RemoveObjectsCommand::RemoveObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs)
    : objects(std::move(objs)) {}

RemoveObjectsCommand::RemoveObjectsCommand(std::shared_ptr<CanvasObject> obj) {
    if (obj) objects.push_back(std::move(obj));
}

void RemoveObjectsCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (const auto& obj : objects) {
        if (obj) {
            obj->isVisible = 0;
            obj->isSelectable = 0;
            obj->isSelected = 0;
            page.RemoveObject(obj);
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void RemoveObjectsCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    for (const auto& obj : objects) {
        if (obj) {
            obj->isVisible = 1;
            obj->isSelectable = 1;
            obj->isSelected = 0;
            page.AddObject(obj);
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB RemoveObjectsCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& obj : objects) {
        if (obj) box.Merge(obj->bounds);
    }
    return box;
}

std::string RemoveObjectsCommand::GetName() const {
    return "Delete Objects";
}

// =========================================================================================
// BatchEraseCommand Implementation
// =========================================================================================

BatchEraseCommand::BatchEraseCommand(
    std::vector<std::shared_ptr<CanvasObject>> deleted,
    std::vector<SlicedStrokeEntry> sliced
) : deletedObjects(std::move(deleted)), slicedStrokes(std::move(sliced)) {}

bool BatchEraseCommand::IsEmpty() const {
    return deletedObjects.empty() && slicedStrokes.empty();
}

void BatchEraseCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (const auto& del : deletedObjects) {
        if (del) {
            del->isVisible = 0;
            del->isSelectable = 0;
            del->isSelected = 0;
            page.RemoveObject(del);
        }
    }
    for (const auto& s : slicedStrokes) {
        if (s.originalStroke) {
            s.originalStroke->isVisible = 0;
            s.originalStroke->isSelectable = 0;
            s.originalStroke->isSelected = 0;
            page.RemoveObject(s.originalStroke);
        }
        for (const auto& frag : s.generatedFragments) {
            if (frag) {
                frag->isVisible = 1;
                frag->isSelectable = 1;
                frag->isSelected = 0;
                page.AddObject(frag);
            }
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void BatchEraseCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    // Reverse slices: Remove generated fragments, restore original stroke
    for (const auto& s : slicedStrokes) {
        for (const auto& frag : s.generatedFragments) {
            if (frag) {
                frag->isVisible = 0;
                frag->isSelectable = 0;
                frag->isSelected = 0;
                page.RemoveObject(frag);
            }
        }
        if (s.originalStroke) {
            s.originalStroke->isVisible = 1;
            s.originalStroke->isSelectable = 1;
            s.originalStroke->isSelected = 0;
            page.AddObject(s.originalStroke);
        }
    }
    // Restore deleted objects
    for (const auto& del : deletedObjects) {
        if (del) {
            del->isVisible = 1;
            del->isSelectable = 1;
            del->isSelected = 0;
            page.AddObject(del);
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB BatchEraseCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& del : deletedObjects) {
        if (del) box.Merge(del->bounds);
    }
    for (const auto& s : slicedStrokes) {
        if (s.originalStroke) box.Merge(s.originalStroke->bounds);
    }
    return box;
}

std::string BatchEraseCommand::GetName() const {
    return "Erase Content";
}

// =========================================================================================
// TransformObjectsCommand Implementation
// =========================================================================================

TransformObjectsCommand::TransformObjectsCommand(std::vector<Entry> e)
    : entries(std::move(e)) {}

void TransformObjectsCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (const auto& e : entries) {
        if (e.afterState) {
            std::shared_ptr<CanvasObject> clone(e.afterState->Clone().release());
            clone->uid = e.uid;
            clone->isVisible = 1;
            clone->isSelectable = 1;
            clone->isSelected = 0;
            page.ReplaceObject(e.uid, clone);
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void TransformObjectsCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    for (const auto& e : entries) {
        if (e.beforeState) {
            std::shared_ptr<CanvasObject> clone(e.beforeState->Clone().release());
            clone->uid = e.uid;
            clone->isVisible = 1;
            clone->isSelectable = 1;
            clone->isSelected = 0;
            page.ReplaceObject(e.uid, clone);
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB TransformObjectsCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& e : entries) {
        if (e.beforeState) box.Merge(e.beforeState->bounds);
        if (e.afterState) box.Merge(e.afterState->bounds);
    }
    return box;
}

std::string TransformObjectsCommand::GetName() const {
    return "Transform Objects";
}

// =========================================================================================
// GroupObjectsCommand Implementation
// =========================================================================================

GroupObjectsCommand::GroupObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs, std::string groupId)
    : newGroupId(std::move(groupId)) {
    entries.reserve(objs.size());
    for (auto& obj : objs) {
        if (obj) {
            entries.push_back({ obj, obj->groupId });
        }
    }
}

void GroupObjectsCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (auto& e : entries) {
        if (e.object) {
            e.object->groupId = newGroupId;
        }
    }
    if (engine) {
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void GroupObjectsCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    for (auto& e : entries) {
        if (e.object) {
            e.object->groupId = e.previousGroupId;
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB GroupObjectsCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& e : entries) {
        if (e.object) box.Merge(e.object->bounds);
    }
    return box;
}

std::string GroupObjectsCommand::GetName() const {
    return "Group Objects";
}

// =========================================================================================
// UngroupObjectsCommand Implementation
// =========================================================================================

UngroupObjectsCommand::UngroupObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs) {
    entries.reserve(objs.size());
    for (auto& obj : objs) {
        if (obj) {
            entries.push_back({ obj, obj->groupId });
        }
    }
}

void UngroupObjectsCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (auto& e : entries) {
        if (e.object) {
            e.object->groupId.clear();
        }
    }
    if (engine) {
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void UngroupObjectsCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    for (auto& e : entries) {
        if (e.object) {
            e.object->groupId = e.previousGroupId;
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB UngroupObjectsCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& e : entries) {
        if (e.object) box.Merge(e.object->bounds);
    }
    return box;
}

std::string UngroupObjectsCommand::GetName() const {
    return "Ungroup Objects";
}

// =========================================================================================
// MacroCommand Implementation
// =========================================================================================

MacroCommand::MacroCommand(std::string name)
    : macroName(std::move(name)) {}

void MacroCommand::AddCommand(std::unique_ptr<ICanvasCommand> cmd) {
    if (cmd) {
        subCommands.push_back(std::move(cmd));
    }
}

bool MacroCommand::IsEmpty() const noexcept {
    return subCommands.empty();
}

void MacroCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (auto& cmd : subCommands) {
        if (cmd) cmd->Execute(page, engine);
    }
    if (engine) {
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void MacroCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    // Reverse order undo for nested actions
    for (auto it = subCommands.rbegin(); it != subCommands.rend(); ++it) {
        if (*it) (*it)->Undo(page, engine);
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB MacroCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& cmd : subCommands) {
        if (cmd) box.Merge(cmd->GetTargetBounds());
    }
    return box;
}

std::string MacroCommand::GetName() const {
    return macroName.empty() ? "Macro Action" : macroName;
}

// =========================================================================================
// LockObjectsCommand Implementation
// =========================================================================================

LockObjectsCommand::LockObjectsCommand(std::vector<std::shared_ptr<CanvasObject>> objs,
                                       bool lock, bool selectable, int32_t zOrder)
    : targetLock(lock), targetSelectable(selectable), targetZOrder(zOrder) {
    states.reserve(objs.size());
    for (auto& obj : objs) {
        if (obj) {
            states.push_back({ obj, static_cast<bool>(obj->isLocked),
                                    static_cast<bool>(obj->isSelectable),
                                    obj->zOrder });
        }
    }
}

void LockObjectsCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    for (auto& s : states) {
        if (s.object) {
            s.object->isLocked = targetLock ? 1 : 0;
            s.object->isSelectable = targetSelectable ? 1 : 0;
            s.object->isSelected = 0;
            s.object->zOrder = targetZOrder;
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void LockObjectsCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    for (auto& s : states) {
        if (s.object) {
            s.object->isLocked = s.wasLocked ? 1 : 0;
            s.object->isSelectable = s.wasSelectable ? 1 : 0;
            s.object->isSelected = 0;
            s.object->zOrder = s.wasZOrder;
        }
    }
    if (engine) {
        engine->ClearSelection();
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB LockObjectsCommand::GetTargetBounds() const {
    AABB box;
    for (const auto& s : states) {
        if (s.object) box.Merge(s.object->bounds);
    }
    return box;
}

std::string LockObjectsCommand::GetName() const {
    return targetLock ? "Lock Background Template" : "Unlock Objects";
}

// =========================================================================================
// ModifyTextCommand Implementation
// =========================================================================================

/**
 * @brief Constructs a reversible text modification command.
 * @param uid Target TextBoxObject runtime unique identifier.
 * @param prevText Pre-mutation text string.
 * @param nextText Post-mutation text string.
 * @param prevW Previous world width in mm.
 * @param nextW New world width in mm.
 * @param prevH Previous world height in mm.
 * @param nextH New world height in mm.
 */
ModifyTextCommand::ModifyTextCommand(uint32_t uid, std::string prevText, std::string nextText,
                                     double prevW, double nextW, double prevH, double nextH)
    : textBoxUid(uid),
      previousText(std::move(prevText)),
      newText(std::move(nextText)),
      previousWidth(prevW),
      newWidth(nextW),
      previousHeight(prevH),
      newHeight(nextH) {}

/**
 * @brief Re-applies the text mutation and synchronizes typographical bounds into the R-Tree.
 */
void ModifyTextCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    auto obj = page.FindObjectByUid(textBoxUid);
    if (obj && obj->type == ObjectType::Text) {
        auto tb = std::static_pointer_cast<TextBoxObject>(obj);
        tb->text = newText;
        tb->worldWidth = newWidth;
        tb->worldHeight = newHeight;
        tb->SyncTextToRuns();
        tb->UpdateBounds();
        page.spatialIndex.Remove(tb->uid);
        page.spatialIndex.Insert(tb->uid, tb->bounds);
        page.isModified = true;
        if (engine && engine->textEditor.GetTarget() == tb.get()) {
            engine->textEditor.ReflowLayout();
        }
    }
    if (engine) {
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

/**
 * @brief Reverses the text mutation, restoring prior text and bounds into the R-Tree.
 */
void ModifyTextCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    auto obj = page.FindObjectByUid(textBoxUid);
    if (obj && obj->type == ObjectType::Text) {
        auto tb = std::static_pointer_cast<TextBoxObject>(obj);
        tb->text = previousText;
        tb->worldWidth = previousWidth;
        tb->worldHeight = previousHeight;
        tb->SyncTextToRuns();
        tb->UpdateBounds();
        page.spatialIndex.Remove(tb->uid);
        page.spatialIndex.Insert(tb->uid, tb->bounds);
        page.isModified = true;
        if (engine && engine->textEditor.GetTarget() == tb.get()) {
            engine->textEditor.ReflowLayout();
        }
    }
    if (engine) {
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB ModifyTextCommand::GetTargetBounds() const {
    return AABB(0.0, 0.0, newWidth, newHeight);
}

std::string ModifyTextCommand::GetName() const {
    return "Edit Text";
}

// =========================================================================================
// RelinkAttachmentCommand Implementation
// =========================================================================================

RelinkAttachmentCommand::RelinkAttachmentCommand(
    uint32_t uid,
    std::string prevPath, std::string nextPath,
    std::string prevName, std::string nextName,
    bool prevEmbed, bool nextEmbed
) : attachmentUid(uid),
    previousPath(std::move(prevPath)),
    newPath(std::move(nextPath)),
    previousDisplayName(std::move(prevName)),
    newDisplayName(std::move(nextName)),
    previousEmbedded(prevEmbed),
    newEmbedded(nextEmbed) {}

void RelinkAttachmentCommand::Execute(CanvasPage& page, CanvasEngine* engine) {
    auto obj = page.FindObjectByUid(attachmentUid);
    if (obj && obj->type == ObjectType::AttachmentFile) {
        auto attach = std::static_pointer_cast<AttachmentObject>(obj);
        attach->filePath = newPath;
        if (!newDisplayName.empty()) {
            attach->displayName = newDisplayName;
        }
        attach->isEmbedded = newEmbedded;
        attach->IsFileValid("", true);
        page.isModified = true;
    }
    if (engine) {
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

void RelinkAttachmentCommand::Undo(CanvasPage& page, CanvasEngine* engine) {
    auto obj = page.FindObjectByUid(attachmentUid);
    if (obj && obj->type == ObjectType::AttachmentFile) {
        auto attach = std::static_pointer_cast<AttachmentObject>(obj);
        attach->filePath = previousPath;
        if (!previousDisplayName.empty()) {
            attach->displayName = previousDisplayName;
        }
        attach->isEmbedded = previousEmbedded;
        attach->IsFileValid("", true);
        page.isModified = true;
    }
    if (engine) {
        engine->isDirty = true;
        engine->needsFullRebake = true;
    }
}

AABB RelinkAttachmentCommand::GetTargetBounds() const {
    return AABB(0.0, 0.0, AttachmentObject::chipW, AttachmentObject::chipH);
}

std::string RelinkAttachmentCommand::GetName() const {
    return "Re-link Attachment";
}

} // namespace Folio
