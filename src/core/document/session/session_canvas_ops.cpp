/**
 * =========================================================================================
 * @file session_canvas_ops.cpp
 * @brief Implementation of DocumentSession stroke commits, laser pointer presentation ink,
 *        polymorphic objects, selection, clipboard, grouping, and template locking.
 * =========================================================================================
 *
 * ARCHITECTURAL & MATHEMATICAL PROCESS:
 * - Converts raw finished stylus stroke vectors into persistent InkContainers and R-Tree indexed entities.
 * - Handles ephemeral presentation strokes (Laser Pointer) with quadratic alpha decay:
 *     α(t) = α_0 * (1 - t/T)^2
 * - Computes centroid translations for clipboard paste:
 *     C = ((B_min + B_max)/2), Δ = Target - C
 * - Implements atomic object grouping with UUID v4 assignment and background template immutability.
 */

#include "core/document/document_session.hpp"
#include "utils/uid_generator.hpp"
#include "utils/guid_generator.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"
#include <unordered_set>

// -----------------------------------------------------------------------------
// Ephemeral Presentation Ink / Laser Pointer
// -----------------------------------------------------------------------------

/**
 * @brief Registers a delegate receiver for ephemeral presentation strokes.
 * @param sink Delegate callback accepting (outlinePath, color, durationMs).
 */
void DocumentSession::SetEphemeralStrokeSink(std::function<void(BLPath, BLRgba32, uint32_t)> sink) {
    ephemeralStrokeSink = std::move(sink);
}

/**
 * @brief Commits an ephemeral presentation stroke (e.g. Laser Pointer).
 *
 * MATHEMATICAL PROCESS:
 * - Bypasses SQLite page storage and CommandHistory (zero footprint).
 * - Routes geometry directly to visual presentation sink where it fades smoothly:
 *     α(t) = α_0 * (1 - t/T)^2
 *
 * @param data Finished stroke geometry from LiveLayerPipeline.
 * @param tool Active pen settings containing stroke color.
 * @param fadeDurationMs Total visibility duration in milliseconds (default: 2500ms).
 */
void DocumentSession::CommitEphemeralStroke(FinishedStrokeData&& data, const PenTool& tool, uint32_t fadeDurationMs) {
    if (!data.outlinePath.is_empty() && ephemeralStrokeSink) {
        ephemeralStrokeSink(std::move(data.outlinePath), tool.color, fadeDurationMs);
    }
}

// -----------------------------------------------------------------------------
// Ink Stroke Commit Workflow
// -----------------------------------------------------------------------------

/**
 * @brief Commits finished stroke data (already containing pre-computed outline geometry).
 * @param data Finished stroke outline path and live segment records.
 * @param tool Active pen tool settings (color, base size, highlighter mode).
 */
void DocumentSession::CommitStroke(FinishedStrokeData&& data, const PenTool& tool) {
    auto activePage = GetActivePage();
    if (!activePage) {
        LOG_WARN_CODE(DocumentSession, FolioErrorCode::InputTargetPageNull,
                      "CommitStroke rejected: No active CanvasPage available to receive stroke data.");
        return;
    }
    if (data.outlinePath.is_empty() && data.liveSegments.empty()) return;

    auto container = std::make_shared<InkContainer>();
    container->uid = UIDGenerator::Next();
    container->isHighlighter = (tool.penType == PenType::Highlighter);
    
    Stroke stroke;
    stroke.outlinePath = std::move(data.outlinePath);
    stroke.segments = std::move(data.liveSegments);
    stroke.color = tool.color;
    stroke.baseWidth = tool.baseSize;
    stroke.pattern = tool.strokePattern;
    container->AddStroke(stroke);

    activePage->AddObject(container);
    RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(container));
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

/**
 * @brief Commits raw 1D stroke segments by computing polygon outline hulls on the fly.
 * @param segments Vector of 1D interpolated segments with pressure/width values.
 * @param tool Active pen tool settings (color, base size, cap type).
 */
void DocumentSession::CommitStroke(std::vector<Segment1D>&& segments, const PenTool& tool) {
    auto activePage = GetActivePage();
    if (!activePage) {
        LOG_WARN_CODE(DocumentSession, FolioErrorCode::InputTargetPageNull,
                      "CommitStroke (raw segments) rejected: No active CanvasPage available.");
        return;
    }
    if (segments.empty()) return;

    auto container = std::make_shared<InkContainer>();
    container->uid = UIDGenerator::Next();
    container->isHighlighter = (tool.penType == PenType::Highlighter);
    
    Stroke stroke;
    std::vector<StrokeOutlineBuilder::InputPoint> pts;
    pts.reserve(segments.size() + 1);
    pts.push_back({ segments[0].p0.x, segments[0].p0.y, segments[0].width });
    for (const auto& s : segments) {
        pts.push_back({ s.p1.x, s.p1.y, s.width });
    }
    stroke.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, tool.capType, tool.strokePattern);
    stroke.segments = std::move(segments);
    stroke.color = tool.color;
    stroke.baseWidth = tool.baseSize;
    stroke.pattern = tool.strokePattern;
    container->AddStroke(stroke);

    activePage->AddObject(container);
    RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(container));
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

// -----------------------------------------------------------------------------
// Polymorphic Object Management
// -----------------------------------------------------------------------------

/**
 * @brief Adds any CanvasObject (Image, TextBox, PDF, Ink) to the active page.
 * Automatically assigns a runtime UID if unassigned, updates the page R-Tree,
 * and records the insertion into the page command history for undo/redo.
 */
void DocumentSession::AddObject(const std::shared_ptr<CanvasObject>& obj) {
    auto activePage = GetActivePage();
    if (!activePage) {
        LOG_WARN_CODE(DocumentSession, FolioErrorCode::InputTargetPageNull,
                      "AddObject rejected: No active CanvasPage available in session.");
        return;
    }
    if (!obj) {
        LOG_WARN(DocumentSession, "AddObject rejected: Null object pointer passed.");
        return;
    }

    if (obj->uid == 0) {
        obj->uid = UIDGenerator::Next();
    }
    LOG_INFO(DocumentSession, "DocumentSession: Added CanvasObject UID " + std::to_string(obj->uid) +
             " to active page '" + activePage->title + "' [" + activePage->guid + "]");
    activePage->AddObject(obj, true);
    RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(obj));
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

/**
 * @brief Convenience helper to add an ImageObject to the active page.
 */
void DocumentSession::AddImage(const std::shared_ptr<Folio::ImageObject>& img) {
    AddObject(img);
}

/**
 * @brief Convenience helper to add a TextBoxObject to the active page.
 */
void DocumentSession::AddTextBox(const std::shared_ptr<Folio::TextBoxObject>& textBox) {
    AddObject(textBox);
}

// -----------------------------------------------------------------------------
// Symmetric Selection Management Facade
// -----------------------------------------------------------------------------

/**
 * @brief Selects all visible, selectable, unlocked objects on the active page.
 *
 * MATHEMATICAL PROCESS:
 * - Filters: isSelected = 1 IF isVisible != 0 AND isSelectable != 0 AND isLocked == 0
 * @return Total count of objects newly selected.
 */
size_t DocumentSession::SelectAll() {
    auto activePage = GetActivePage();
    if (!activePage) return 0;

    size_t count = 0;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isVisible && obj->isSelectable && !obj->isLocked) {
            obj->isSelected = 1;
            count++;
        }
    }
    if (count > 0) {
        activePage->isModified = true;
        NotifyPageModified(activePage);
    }
    return count;
}

/**
 * @brief Deselects all objects on the active page.
 */
void DocumentSession::DeselectAll() {
    auto activePage = GetActivePage();
    if (!activePage) return;

    for (const auto& obj : activePage->objects) {
        if (obj) {
            obj->isSelected = 0;
        }
    }
}

/**
 * @brief Retrieves all currently selected objects on the active page.
 * @return Vector of shared pointers to selected CanvasObjects.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::GetSelectedObjects() const {
    auto activePage = GetActivePage();
    if (!activePage) return {};

    std::vector<std::shared_ptr<CanvasObject>> selected;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isSelected && !obj->isLocked) {
            selected.push_back(obj);
        }
    }
    return selected;
}

/**
 * @brief Deletes all currently selected objects atomically from the active page.
 *
 * ARCHITECTURAL PROCESS:
 * - Collects all objects with isSelected != 0 and isLocked == 0.
 * - Defensive safety: If empty, safely returns 0 without side effects.
 * - Commits single atomic RemoveObjectsCommand to history.
 *
 * @return Number of objects deleted.
 */
size_t DocumentSession::DeleteSelection() {
    auto activePage = GetActivePage();
    if (!activePage) return 0;

    std::vector<std::shared_ptr<CanvasObject>> toRemove;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isSelected && !obj->isLocked) {
            toRemove.push_back(obj);
        }
    }
    if (toRemove.empty()) {
        return 0; // Safe: zero deletions when nothing is selected
    }

    size_t count = toRemove.size();
    RecordHistoryCommand(activePage, std::make_unique<Folio::RemoveObjectsCommand>(toRemove));
    activePage->isModified = true;

    for (const auto& obj : toRemove) {
        activePage->RemoveObject(obj);
    }

    NotifyPageModified(activePage);
    LOG_INFO(DocumentSession, "DeleteSelection: Atomically deleted " + std::to_string(count) + " selected objects.");
    return count;
}

// -----------------------------------------------------------------------------
// Unified Clipboard & Duplication Facade
// -----------------------------------------------------------------------------

/**
 * @brief Copies the currently selected objects on the active page into the session clipboard.
 */
void DocumentSession::CopySelection() {
    CopySelection(GetSelectedObjects());
}

/**
 * @brief Copies the specified canvas objects into the session clipboard.
 * @param selected Vector of selected CanvasObjects.
 */
void DocumentSession::CopySelection(const std::vector<std::shared_ptr<CanvasObject>>& selected) {
    clipboardObjects.clear();
    for (const auto& obj : selected) {
        if (obj && obj->isSelected) {
            clipboardObjects.push_back(std::shared_ptr<CanvasObject>(obj->Clone().release()));
        }
    }
    LOG_INFO(DocumentSession, "Copied " + std::to_string(clipboardObjects.size()) + " objects to session clipboard.");
}

/**
 * @brief Cuts the currently selected objects on the active page.
 * Copies them to clipboard and removes them from the page with undo tracking.
 * @return Vector of removed objects.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::CutSelection() {
    return CutSelection(GetSelectedObjects());
}

/**
 * @brief Cuts the specified objects: copies them to clipboard and removes them from the page.
 * Records an atomic RemoveObjectsCommand into the history or active macro transaction.
 *
 * @param selected Vector of selected CanvasObjects.
 * @return Vector of removed objects.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::CutSelection(const std::vector<std::shared_ptr<CanvasObject>>& selected) {
    CopySelection(selected);
    auto activePage = GetActivePage();
    if (!activePage) return {};

    std::vector<std::shared_ptr<CanvasObject>> toRemove;
    for (const auto& obj : selected) {
        if (obj && obj->isSelected) {
            toRemove.push_back(obj);
        }
    }
    if (!toRemove.empty()) {
        RecordHistoryCommand(activePage, std::make_unique<Folio::RemoveObjectsCommand>(toRemove));
        for (const auto& obj : toRemove) {
            activePage->RemoveObject(obj);
        }
        activePage->isModified = true;
        NotifyPageModified(activePage);
        LOG_INFO(DocumentSession, "Cut " + std::to_string(toRemove.size()) + " objects from active page.");
    }
    return toRemove;
}

/**
 * @brief Pastes the contents of the session clipboard onto the active page, centered at (worldX, worldY).
 *
 * MATHEMATICAL PROCESS:
 * 1. Evaluates collective bounding box B = Union(obj.bounds) for all clipboard items.
 * 2. Computes centroid: C = ((B.minX + B.maxX)/2, (B.minY + B.maxY)/2).
 * 3. Calculates world translation vector: ΔX = worldX - C.x, ΔY = worldY - C.y.
 * 4. Clones each item, applies Δ translation, assigns fresh unique runtime UID via UIDGenerator::Next().
 * 5. Commits a single atomic AddObjectsCommand to the page history.
 *
 * @param worldX Target world X coordinate (millimeters).
 * @param worldY Target world Y coordinate (millimeters).
 * @return Vector of newly created and inserted CanvasObjects.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::PasteObjects(double worldX, double worldY) {
    if (clipboardObjects.empty()) return {};
    auto activePage = GetActivePage();
    if (!activePage) return {};

    AABB box;
    for (const auto& obj : clipboardObjects) {
        if (obj) box.Merge(obj->bounds);
    }
    Point2D center = { (box.minX + box.maxX) * 0.5, (box.minY + box.maxY) * 0.5 };
    double dx = worldX - center.x;
    double dy = worldY - center.y;
    BLMatrix2D trans = BLMatrix2D::make_translation(dx, dy);

    std::vector<std::shared_ptr<CanvasObject>> newObjects;
    newObjects.reserve(clipboardObjects.size());

    for (const auto& obj : clipboardObjects) {
        if (!obj) continue;
        auto clone = std::shared_ptr<CanvasObject>(obj->Clone().release());
        clone->uid = UIDGenerator::Next();
        clone->ApplyTransform(trans);
        clone->UpdateBounds();
        clone->isSelected = 1;
        activePage->AddObject(clone);
        newObjects.push_back(clone);
    }

    if (!newObjects.empty()) {
        RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectsCommand>(newObjects));
        activePage->isModified = true;
        NotifyPageModified(activePage);
        LOG_INFO(DocumentSession, "Pasted " + std::to_string(newObjects.size()) +
                 " objects at (" + std::to_string(worldX) + ", " + std::to_string(worldY) + ") mm");
    }
    return newObjects;
}

/**
 * @brief Duplicates the currently selected objects on the active page in-place with an offset.
 * @param offsetMm Offset distance in millimeters (default: 10.0 mm).
 * @return Vector of newly created and inserted duplicate CanvasObjects.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::DuplicateSelection(double offsetMm) {
    return DuplicateSelection(GetSelectedObjects(), offsetMm);
}

/**
 * @brief Duplicates the selected objects in-place with a slight offset (+offsetMm X and Y).
 * Automatically deselects original objects, selects the newly created duplicates,
 * and records an atomic AddObjectsCommand into history.
 *
 * @param selected Vector of selected CanvasObjects to duplicate.
 * @param offsetMm Offset distance in millimeters (default: 10.0 mm).
 * @return Vector of newly created and inserted duplicate CanvasObjects.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::DuplicateSelection(const std::vector<std::shared_ptr<CanvasObject>>& selected,
                                                                               double offsetMm) {
    auto activePage = GetActivePage();
    if (!activePage) return {};

    BLMatrix2D trans = BLMatrix2D::make_translation(offsetMm, offsetMm);
    std::vector<std::shared_ptr<CanvasObject>> newObjects;
    newObjects.reserve(selected.size());

    for (const auto& obj : selected) {
        if (!obj || !obj->isSelected) continue;
        obj->isSelected = 0; // Deselect original

        auto clone = std::shared_ptr<CanvasObject>(obj->Clone().release());
        clone->uid = UIDGenerator::Next();
        clone->ApplyTransform(trans);
        clone->UpdateBounds();
        clone->isSelected = 1; // Select duplicate
        activePage->AddObject(clone);
        newObjects.push_back(clone);
    }

    if (!newObjects.empty()) {
        RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectsCommand>(newObjects));
        activePage->isModified = true;
        NotifyPageModified(activePage);
        LOG_INFO(DocumentSession, "Duplicated " + std::to_string(newObjects.size()) + " selected objects.");
    }
    return newObjects;
}

// -----------------------------------------------------------------------------
// Object Grouping Facade
// -----------------------------------------------------------------------------

/**
 * @brief Groups the currently selected objects into a cohesive logical group.
 * Assigns a shared RFC 4122 UUID v4 groupId to all selected, unlocked objects.
 *
 * @return Generated groupId string if successful, or empty string if < 2 objects selected.
 */
std::string DocumentSession::GroupSelection() {
    auto activePage = GetActivePage();
    if (!activePage) return "";

    std::vector<std::shared_ptr<CanvasObject>> selected;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isSelected && !obj->isLocked) {
            selected.push_back(obj);
        }
    }
    if (selected.size() < 2) {
        LOG_WARN(DocumentSession, "GroupSelection rejected: At least 2 objects required to form a group.");
        return "";
    }

    std::string newGroupId = GUIDGenerator::GenerateV4();
    auto cmd = std::make_unique<Folio::GroupObjectsCommand>(selected, newGroupId);
    cmd->Execute(*activePage);
    RecordHistoryCommand(activePage, std::move(cmd));
    activePage->isModified = true;
    NotifyPageModified(activePage);
    LOG_INFO(DocumentSession, "GroupSelection: Grouped " + std::to_string(selected.size()) +
             " objects under groupId " + newGroupId);
    return newGroupId;
}

/**
 * @brief Ungroups any selected objects, restoring them to individual unlinked objects.
 * @return Number of objects ungrouped.
 */
size_t DocumentSession::UngroupSelection() {
    auto activePage = GetActivePage();
    if (!activePage) return 0;

    std::vector<std::shared_ptr<CanvasObject>> selected;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isSelected && obj->IsGrouped() && !obj->isLocked) {
            selected.push_back(obj);
        }
    }
    if (selected.empty()) return 0;

    size_t count = selected.size();
    auto cmd = std::make_unique<Folio::UngroupObjectsCommand>(selected);
    cmd->Execute(*activePage);
    RecordHistoryCommand(activePage, std::move(cmd));
    activePage->isModified = true;
    NotifyPageModified(activePage);
    LOG_INFO(DocumentSession, "UngroupSelection: Ungrouped " + std::to_string(count) + " objects.");
    return count;
}

// -----------------------------------------------------------------------------
// Page Background Template Lock Facade
// -----------------------------------------------------------------------------

/**
 * @brief Locks the currently selected objects as immutable background page templates.
 *
 * ARCHITECTURAL PROCESS:
 * 1. Sets zIndex = 0 (lowest layer beneath user inking).
 * 2. Sets isLocked = 1 (rejects transformations and deletions).
 * 3. Sets isSelectable = 0 (immune to selection tools).
 * 4. Sets isSelected = 0 (detaches gizmo immediately).
 * 5. Commits reversible LockObjectsCommand in history.
 *
 * @return Number of objects locked as background templates.
 */
size_t DocumentSession::LockSelectionAsBackground() {
    auto activePage = GetActivePage();
    if (!activePage) return 0;

    std::vector<std::shared_ptr<CanvasObject>> selected;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isSelected) {
            selected.push_back(obj);
        }
    }
    if (selected.empty()) return 0;

    size_t count = selected.size();
    auto cmd = std::make_unique<Folio::LockObjectsCommand>(selected, true, false, 0);
    cmd->Execute(*activePage);
    RecordHistoryCommand(activePage, std::move(cmd));
    activePage->isModified = true;
    NotifyPageModified(activePage);
    LOG_INFO(DocumentSession, "LockSelectionAsBackground: Locked " + std::to_string(count) +
             " objects as background templates.");
    return count;
}

/**
 * @brief Unlocks specific objects by UID, restoring their selectability.
 * @param objectUids Vector of CanvasObject UIDs to unlock.
 * @return Number of objects unlocked.
 */
size_t DocumentSession::UnlockObjects(const std::vector<uint32_t>& objectUids) {
    auto activePage = GetActivePage();
    if (!activePage || objectUids.empty()) return 0;

    std::unordered_set<uint32_t> uidSet(objectUids.begin(), objectUids.end());
    std::vector<std::shared_ptr<CanvasObject>> targets;
    for (const auto& obj : activePage->objects) {
        if (obj && uidSet.count(obj->uid)) {
            targets.push_back(obj);
        }
    }
    if (targets.empty()) return 0;

    size_t count = targets.size();
    auto cmd = std::make_unique<Folio::LockObjectsCommand>(targets, false, true, 1);
    cmd->Execute(*activePage);
    RecordHistoryCommand(activePage, std::move(cmd));
    activePage->isModified = true;
    NotifyPageModified(activePage);
    LOG_INFO(DocumentSession, "UnlockObjects: Unlocked " + std::to_string(count) + " objects.");
    return count;
}

/**
 * @brief Unlocks all background templates and locked objects on the active page.
 * @return Number of objects unlocked.
 */
size_t DocumentSession::UnlockAllBackgroundTemplates() {
    auto activePage = GetActivePage();
    if (!activePage) return 0;

    std::vector<std::shared_ptr<CanvasObject>> locked;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isLocked) {
            locked.push_back(obj);
        }
    }
    if (locked.empty()) return 0;

    size_t count = locked.size();
    auto cmd = std::make_unique<Folio::LockObjectsCommand>(locked, false, true, 1);
    cmd->Execute(*activePage);
    RecordHistoryCommand(activePage, std::move(cmd));
    activePage->isModified = true;
    NotifyPageModified(activePage);
    LOG_INFO(DocumentSession, "UnlockAllBackgroundTemplates: Unlocked all " + std::to_string(count) +
             " locked background template objects.");
    return count;
}

/**
 * @brief Retrieves all locked background template objects on the active page.
 * @return Vector of locked CanvasObjects.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::GetBackgroundTemplateObjects() const {
    auto activePage = GetActivePage();
    if (!activePage) return {};

    std::vector<std::shared_ptr<CanvasObject>> locked;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isLocked) {
            locked.push_back(obj);
        }
    }
    return locked;
}
