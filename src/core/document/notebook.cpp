/**
 * =========================================================================================
 * @file notebook.cpp
 * @brief Implementation of Notebook Package, Hierarchy Navigation, and Section Management
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE & HIERARCHY INVARIANTS:
 * This translation unit implements the in-memory document container of FolioNote:
 * 1. Root and Nested Section Hierarchy:
 *    Manages the parent-child relationship between Notebooks, SectionGroups, Sections,
 *    and Pages.
 * 2. Active Selection Synchronization:
 *    Maintains synchronization between `activeSectionGuid` (persistent lookup across
 *    session restores) and `activeSectionIndex` (instant O(1) indexed access for UI loops).
 * 3. Atomic Movement & Reordering:
 *    Safely relocates sections between the notebook root and hierarchical section groups
 *    with automatic index clamping and order recalculation.
 * 4. Deep Hierarchy Traversal:
 *    Performs recursive search and aggregation across arbitrarily nested SectionGroups.
 */

#include "core/document/notebook.hpp"

#include <algorithm>
#include "utils/guid_generator.hpp"
#include "utils/logger.hpp"
#include "utils/usage_tracker.hpp"

// =========================================================================================
// Construction & Lifecycle
// =========================================================================================

/**
 * @brief Constructs a new Notebook with a title, UI accent color tag, and icon filename.
 *
 * GENERAL WORKING PROCESS & INVARIANTS:
 * 1. Generates a new cryptographically random UUID v4 string via `GUIDGenerator::GenerateV4()`
 *    for unique persistent identification across database storage and library queries.
 * 2. Assigns display title, UI accent color tag (defaults to warm orange), and SVG icon filename.
 * 3. Invariant Fallback: If `iconFile` is empty, defaults to `FOLIO_NOTEBOOK_DEFAULT_ICON`
 *    ("orange-notebook.svg"), guaranteeing every notebook is visually identifiable.
 * 4. Invariant Guarantee: Ensures every newly constructed notebook is immediately usable
 *    by auto-creating a default "New Section 1" root section (with its own random color) and setting it active.
 *
 * @param notebookName Display title (defaults to "New Notebook").
 * @param tag UI accent color vector (defaults to random preset color via GetRandomSectionColor()).
 * @param icon SVG icon file name (defaults to FOLIO_NOTEBOOK_DEFAULT_ICON).
 */
Notebook::Notebook(std::string notebookName, ImVec4 tag, std::string icon)
    : guid(GUIDGenerator::GenerateV4()), 
      name(std::move(notebookName)), 
      colorTag(tag), 
      iconFile(std::move(icon)) {
    if (iconFile.empty()) {
        iconFile = FOLIO_NOTEBOOK_DEFAULT_ICON;
    }
    // Guarantee at least one section exists
    auto defaultSec = std::make_shared<Section>("New Section 1");
    activeSectionGuid = defaultSec->guid;
    sections.push_back(defaultSec);
}

// =========================================================================================
// Active Item Navigation
// =========================================================================================

/**
 * @brief Sets the active section, keeping activeSectionGuid and activeSectionIndex in sync.
 *
 * WORKING PROCESS:
 * 1. Checks if the incoming section pointer is non-null.
 * 2. Compares incoming section GUID against `activeSectionGuid` to detect actual switch.
 * 3. Updates `activeSectionGuid`.
 * 4. Iterates over root sections to find matching index; if located, updates `activeSectionIndex`.
 * 5. Telemetry: If a section change occurred, records event in `UsageTracker`.
 *
 * @param sec Shared pointer to the section to activate.
 */
void Notebook::SetActiveSection(const std::shared_ptr<Section>& sec) {
    if (!sec) return;
    bool changed = (activeSectionGuid != sec->guid);
    activeSectionGuid = sec->guid;
    for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i] && sections[i]->guid == sec->guid) {
            activeSectionIndex = i;
            if (changed) ::Folio::UsageTracker::Instance().RecordSectionSwitch();
            return;
        }
    }
    if (changed) ::Folio::UsageTracker::Instance().RecordSectionSwitch();
}

/**
 * @brief Returns the currently active Section (from root or any SectionGroup), or nullptr.
 *
 * RESOLUTION PIPELINE & FALLBACK STRATEGY:
 * 1. Primary: If `activeSectionGuid` is non-empty, looks up section across both root sections
 *    and nested SectionGroups via `FindSectionByGuid()`.
 * 2. Secondary: If GUID lookup fails or is unset, attempts direct index access via `activeSectionIndex`.
 * 3. Tertiary: Falls back to the first available root section (`sections.front()`).
 * 4. Quaternary: Falls back to the first available section inside the first SectionGroup.
 * 5. Failure: Returns nullptr if the notebook contains zero sections across all containers.
 *
 * @return std::shared_ptr<Section> Active Section pointer, or nullptr if none found.
 */
std::shared_ptr<Section> Notebook::GetActiveSection() const {
    if (!activeSectionGuid.empty()) {
        if (auto found = FindSectionByGuid(activeSectionGuid)) {
            return found;
        }
    }
    if (activeSectionIndex < sections.size() && sections[activeSectionIndex]) {
        activeSectionGuid = sections[activeSectionIndex]->guid;
        return sections[activeSectionIndex];
    }
    if (!sections.empty() && sections.front()) {
        activeSectionIndex = 0;
        activeSectionGuid = sections.front()->guid;
        return sections.front();
    }
    for (const auto& grp : sectionGroups) {
        if (grp && !grp->sections.empty() && grp->sections.front()) {
            activeSectionGuid = grp->sections.front()->guid;
            return grp->sections.front();
        }
    }
    return nullptr;
}

/**
 * @brief Resolves the currently active CanvasPage through the active section.
 *
 * @return std::shared_ptr<CanvasPage> Active page pointer, or nullptr if no active section.
 */
std::shared_ptr<CanvasPage> Notebook::GetActivePage() const {
    auto sec = GetActiveSection();
    return sec ? sec->GetActivePage() : nullptr;
}

// =========================================================================================
// Section & Group Management
// =========================================================================================

/**
 * @brief Appends a new root-level section to this notebook.
 *
 * ALGORITHM:
 * 1. Assigns sequential `sortOrder` index matching `sections.size()`.
 * 2. Clears `groupGuid` since this section resides at root.
 * 3. Appends pointer to `sections` vector.
 *
 * @param section Shared pointer to the new Section to add.
 */
void Notebook::AddSection(std::shared_ptr<Section> section) {
    if (!section) return;
    section->sortOrder = static_cast<int32_t>(sections.size());
    section->groupGuid.clear();
    std::string secName = section->name;
    std::string secGuid = section->guid;
    sections.push_back(std::move(section));
    LOG_INFO(Notebook, "Added root section '" + secName + "' (" + secGuid + "). Total root sections: " + std::to_string(sections.size()));
}

/**
 * @brief Appends a new root-level section group to this notebook.
 *
 * ALGORITHM:
 * 1. Sets `notebookGuid` to link this group with this notebook.
 * 2. Assigns sequential `sortOrder` index matching `sectionGroups.size()`.
 * 3. Appends pointer to `sectionGroups` vector.
 *
 * @param group Shared pointer to the SectionGroup to add.
 */
void Notebook::AddSectionGroup(std::shared_ptr<SectionGroup> group) {
    if (!group) return;
    group->notebookGuid = this->guid;
    group->sortOrder = static_cast<int32_t>(sectionGroups.size());
    std::string grpName = group->name;
    std::string grpGuid = group->guid;
    sectionGroups.push_back(std::move(group));
    LOG_INFO(Notebook, "Added section group '" + grpName + "' (" + grpGuid + "). Total groups: " + std::to_string(sectionGroups.size()));
}

/**
 * @brief Moves a root section from one index to another, updating sortOrder and activeIndex.
 *
 * INDEX CALCULATION & ACTIVE SELECTION MATH:
 * When an element is moved from `fromIdx` to `toIdx`:
 * - If `activeSectionIndex == fromIdx`, the active index shifts directly to `toIdx`.
 * - If `fromIdx < activeSectionIndex <= toIdx`, preceding elements shift left: `activeSectionIndex--`.
 * - If `toIdx <= activeSectionIndex < fromIdx`, following elements shift right: `activeSectionIndex++`.
 *
 * @param fromIdx Source index in `sections` vector.
 * @param toIdx Destination index in `sections` vector.
 * @return bool True if indices are valid and movement succeeded; false otherwise.
 */
bool Notebook::MoveSection(size_t fromIdx, size_t toIdx) {
    if (fromIdx >= sections.size() || toIdx >= sections.size() || fromIdx == toIdx) {
        return false;
    }
    auto movedSec = sections[fromIdx];
    sections.erase(sections.begin() + fromIdx);
    sections.insert(sections.begin() + toIdx, movedSec);

    // Update active section index tracking
    if (activeSectionIndex == fromIdx) {
        activeSectionIndex = toIdx;
    } else if (fromIdx < activeSectionIndex && toIdx >= activeSectionIndex) {
        activeSectionIndex--;
    } else if (fromIdx > activeSectionIndex && toIdx <= activeSectionIndex) {
        activeSectionIndex++;
    }

    // Re-index all sort orders
    for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i]) sections[i]->sortOrder = static_cast<int32_t>(i);
    }
    LOG_INFO(Notebook, "Reordered root section '" + (movedSec ? movedSec->name : "Unknown") + "' from index " + std::to_string(fromIdx) + " to " + std::to_string(toIdx));
    return true;
}

/**
 * @brief Moves a section group from one index to another, updating sortOrder.
 *
 * @param fromIdx Source index in `sectionGroups` vector.
 * @param toIdx Destination index in `sectionGroups` vector.
 * @return bool True if movement succeeded; false on invalid bounds.
 */
bool Notebook::MoveSectionGroup(size_t fromIdx, size_t toIdx) {
    if (fromIdx >= sectionGroups.size() || toIdx >= sectionGroups.size() || fromIdx == toIdx) {
        return false;
    }
    auto movedGrp = sectionGroups[fromIdx];
    sectionGroups.erase(sectionGroups.begin() + fromIdx);
    sectionGroups.insert(sectionGroups.begin() + toIdx, movedGrp);

    for (size_t i = 0; i < sectionGroups.size(); ++i) {
        if (sectionGroups[i]) sectionGroups[i]->sortOrder = static_cast<int32_t>(i);
    }
    LOG_INFO(Notebook, "Reordered section group '" + (movedGrp ? movedGrp->name : "Unknown") + "' from index " + std::to_string(fromIdx) + " to " + std::to_string(toIdx));
    return true;
}

/**
 * @brief Moves a section (from root or another group) into a target SectionGroup.
 *
 * WORKING PROCESS:
 * 1. Locates target group via `FindSectionGroupByGuid`. Returns false if not found.
 * 2. Checks if section currently resides in root sections. If so, removes it and updates
 *    root sort orders and activeSectionIndex.
 * 3. If not in root, searches other SectionGroups, removes it, and updates that group's orders.
 * 4. Assigns `targetSec->groupGuid = targetGroupGuid`.
 * 5. Inserts into target group at `targetIdx` (or appends if out of bounds).
 * 6. Updates target group's section sort orders.
 * 7. Maintains active section state: if the moved section was active, keeps it active and
 *    auto-expands the target group (`targetGrp->isCollapsed = false`).
 *
 * @param secGuid GUID of section to move.
 * @param targetGroupGuid GUID of target SectionGroup.
 * @param targetIdx Insertion index in target group, or (size_t)-1 to append at end.
 * @return bool True if section was relocated successfully; false if target or section not found.
 */
bool Notebook::MoveSectionToGroup(const std::string& secGuid, const std::string& targetGroupGuid, size_t targetIdx) {
    auto targetGrp = FindSectionGroupByGuid(targetGroupGuid);
    if (!targetGrp) {
        LOG_ERROR(Notebook, "MoveSectionToGroup failed: Target group not found: " + targetGroupGuid);
        return false;
    }

    std::shared_ptr<Section> targetSec = nullptr;
    bool wasActive = (!activeSectionGuid.empty() && activeSectionGuid == secGuid);

    // 1. Try remove from root sections
    auto rootIt = std::find_if(sections.begin(), sections.end(), [&](const std::shared_ptr<Section>& s) {
        return s && s->guid == secGuid;
    });
    if (rootIt != sections.end()) {
        targetSec = *rootIt;
        size_t removedIdx = std::distance(sections.begin(), rootIt);
        sections.erase(rootIt);

        if (sections.empty()) {
            activeSectionIndex = 0;
        } else if (activeSectionIndex >= sections.size() || activeSectionIndex == removedIdx) {
            activeSectionIndex = (sections.size() > 0) ? std::min(removedIdx, sections.size() - 1) : 0;
        }

        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i]) sections[i]->sortOrder = static_cast<int32_t>(i);
        }
    } else {
        // 2. Try remove from other section groups
        for (auto& grp : sectionGroups) {
            if (!grp) continue;
            auto it = std::find_if(grp->sections.begin(), grp->sections.end(), [&](const std::shared_ptr<Section>& s) {
                return s && s->guid == secGuid;
            });
            if (it != grp->sections.end()) {
                targetSec = *it;
                size_t removedIdx = std::distance(grp->sections.begin(), it);
                grp->sections.erase(it);

                if (grp->sections.empty()) {
                    grp->activeSectionIndex = 0;
                } else if (grp->activeSectionIndex >= grp->sections.size() || grp->activeSectionIndex == removedIdx) {
                    grp->activeSectionIndex = (grp->sections.size() > 0) ? std::min(removedIdx, grp->sections.size() - 1) : 0;
                }

                for (size_t i = 0; i < grp->sections.size(); ++i) {
                    if (grp->sections[i]) grp->sections[i]->sortOrder = static_cast<int32_t>(i);
                }
                break;
            }
        }
    }

    if (targetSec) {
        targetSec->groupGuid = targetGroupGuid;
        size_t insertPos = 0;
        if (targetIdx < targetGrp->sections.size()) {
            targetGrp->sections.insert(targetGrp->sections.begin() + targetIdx, targetSec);
            insertPos = targetIdx;
        } else {
            insertPos = targetGrp->sections.size();
            targetGrp->sections.push_back(targetSec);
        }

        for (size_t i = 0; i < targetGrp->sections.size(); ++i) {
            if (targetGrp->sections[i]) targetGrp->sections[i]->sortOrder = static_cast<int32_t>(i);
        }

        // Maintain active section state
        if (wasActive || activeSectionGuid.empty() || GetActiveSection() == nullptr) {
            activeSectionGuid = targetSec->guid;
            targetGrp->activeSectionIndex = insertPos;
            targetGrp->isCollapsed = false;
        }

        LOG_INFO(Notebook, "Moved section '" + targetSec->name + "' (" + secGuid + ") into group '" + targetGrp->name + "' at index " + std::to_string(insertPos));
        return true;
    }

    LOG_WARN(Notebook, "MoveSectionToGroup failed: Section not found: " + secGuid);
    return false;
}

/**
 * @brief Moves a section from a SectionGroup back to the notebook root sections list.
 *
 * WORKING PROCESS:
 * 1. Searches through all SectionGroups to locate the section by GUID.
 * 2. Erases it from its source group and clamps the source group's activeSectionIndex.
 * 3. Re-indexes the source group's remaining sections.
 * 4. Clears `targetSec->groupGuid`.
 * 5. Inserts into root `sections` at `targetIdx` (or appends if out of bounds).
 * 6. Re-indexes all root section `sortOrder` values.
 * 7. Maintains active selection state, pointing `activeSectionIndex` to the inserted root location.
 *
 * @param secGuid GUID of section to move.
 * @param targetIdx Insertion index in root sections, or (size_t)-1 to append at end.
 * @return bool True if relocated; false if section not found in any group.
 */
bool Notebook::MoveSectionToRoot(const std::string& secGuid, size_t targetIdx) {
    std::shared_ptr<Section> targetSec = nullptr;
    bool wasActive = (!activeSectionGuid.empty() && activeSectionGuid == secGuid);

    for (auto& grp : sectionGroups) {
        if (!grp) continue;
        auto it = std::find_if(grp->sections.begin(), grp->sections.end(), [&](const std::shared_ptr<Section>& s) {
            return s && s->guid == secGuid;
        });
        if (it != grp->sections.end()) {
            targetSec = *it;
            size_t removedIdx = std::distance(grp->sections.begin(), it);
            grp->sections.erase(it);

            if (grp->sections.empty()) {
                grp->activeSectionIndex = 0;
            } else if (grp->activeSectionIndex >= grp->sections.size() || grp->activeSectionIndex == removedIdx) {
                grp->activeSectionIndex = (grp->sections.size() > 0) ? std::min(removedIdx, grp->sections.size() - 1) : 0;
            }

            for (size_t i = 0; i < grp->sections.size(); ++i) {
                if (grp->sections[i]) grp->sections[i]->sortOrder = static_cast<int32_t>(i);
            }
            break;
        }
    }

    if (targetSec) {
        targetSec->groupGuid.clear();
        size_t insertPos = 0;
        if (targetIdx < sections.size()) {
            sections.insert(sections.begin() + targetIdx, targetSec);
            insertPos = targetIdx;
        } else {
            insertPos = sections.size();
            sections.push_back(targetSec);
        }

        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i]) sections[i]->sortOrder = static_cast<int32_t>(i);
        }

        // Maintain active section state
        if (wasActive || activeSectionGuid.empty() || GetActiveSection() == nullptr) {
            activeSectionGuid = targetSec->guid;
            activeSectionIndex = insertPos;
        }

        LOG_INFO(Notebook, "Moved section '" + targetSec->name + "' (" + secGuid + ") to root sections at index " + std::to_string(insertPos));
        return true;
    }

    LOG_WARN(Notebook, "MoveSectionToRoot failed: Section not found in any group: " + secGuid);
    return false;
}

/**
 * @brief Removes a root section by GUID and safely clamps activeSectionIndex.
 *
 * SAFETY INVARIANTS:
 * - If the removed section was the active section, updates `activeSectionGuid` to the
 *   new active section resolved via `GetActiveSection()`.
 * - Re-indexes `sortOrder` across remaining root sections.
 *
 * @param secGuid GUID identifier of root section to delete.
 * @return bool True if found and removed; false if not found.
 */
bool Notebook::RemoveSection(const std::string& secGuid) {
    auto it = std::find_if(sections.begin(), sections.end(), [&](const std::shared_ptr<Section>& s) {
        return s && s->guid == secGuid;
    });

    if (it != sections.end()) {
        std::string secName = (*it)->name;
        size_t removedIndex = std::distance(sections.begin(), it);
        sections.erase(it);

        if (sections.empty()) {
            activeSectionIndex = 0;
        } else if (activeSectionIndex >= sections.size() || activeSectionIndex == removedIndex) {
            activeSectionIndex = (sections.size() > 0) ? std::min(removedIndex, sections.size() - 1) : 0;
        }
        if (activeSectionGuid == secGuid) {
            auto newActive = GetActiveSection();
            activeSectionGuid = newActive ? newActive->guid : "";
        }

        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i]) sections[i]->sortOrder = static_cast<int32_t>(i);
        }

        LOG_INFO(Notebook, "Removed root section '" + secName + "' (" + secGuid + "). Remaining root sections: " + std::to_string(sections.size()));
        return true;
    }

    LOG_WARN(Notebook, "RemoveSection failed: Root section not found: " + secGuid);
    return false;
}

// =========================================================================================
// Deep Search & Query Helpers
// =========================================================================================

/**
 * @brief Finds a section by GUID across root-level sections and all nested section groups.
 *
 * SEARCH ALGORITHM:
 * 1. Checks root `sections` in O(N_root) time.
 * 2. If not found, iterates through `sectionGroups` delegating to each group's recursive
 *    `FindSectionByGuid` in O(N_groups * N_group_sections) time.
 *
 * @param secGuid GUID of the section to locate.
 * @return std::shared_ptr<Section> Matching section pointer, or nullptr if not found.
 */
std::shared_ptr<Section> Notebook::FindSectionByGuid(const std::string& secGuid) const {
    for (const auto& sec : sections) {
        if (sec && sec->guid == secGuid) return sec;
    }
    for (const auto& grp : sectionGroups) {
        if (!grp) continue;
        if (auto found = grp->FindSectionByGuid(secGuid)) {
            return found;
        }
    }
    return nullptr;
}

/**
 * @brief Finds a section group by GUID across all root and nested groups.
 *
 * SEARCH ALGORITHM:
 * 1. Checks if the root group GUID matches.
 * 2. If not, delegates recursively to `FindSubGroupByGuid()` to check child sub-groups.
 *
 * @param grpGuid GUID of the SectionGroup to locate.
 * @return std::shared_ptr<SectionGroup> Matching group pointer, or nullptr if not found.
 */
std::shared_ptr<SectionGroup> Notebook::FindSectionGroupByGuid(const std::string& grpGuid) const {
    for (const auto& grp : sectionGroups) {
        if (!grp) continue;
        if (grp->guid == grpGuid) return grp;
        if (auto found = grp->FindSubGroupByGuid(grpGuid)) {
            return found;
        }
    }
    return nullptr;
}

/**
 * @brief Performs an in-memory deep copy of this notebook, including its sections and section groups.
 *
 * GENERAL WORKING PROCESS & INVARIANTS:
 * 1. Generates a fresh unique persistent UUID v4 identifier via `GUIDGenerator::GenerateV4()`
 *    so the cloned notebook has distinct identity in memory and storage.
 * 2. Determines the cloned notebook's display title: defaults to `<currentName> - Copy` if
 *    `newName` is empty.
 * 3. Preserves visual theme accent color (`colorTag`), icon filename (`iconFile`), and expansion state (`isOpen`).
 * 4. Deep-copies all root sections via `sec->Clone()`, ensuring page and stroke hierarchies are
 *    independently replicated with fresh GUIDs.
 * 5. Deep-copies all section groups and their contained sections via `grp->Clone()`.
 * 6. Guarantees at least one default section exists if source was empty.
 * 7. Clamps and mirrors `activeSectionIndex` and resolves `activeSectionGuid`.
 *
 * @param newName Optional display title for the clone (defaults to source name + " - Copy").
 * @return std::shared_ptr<Notebook> Newly allocated deep-cloned Notebook model.
 */
std::shared_ptr<Notebook> Notebook::Clone(const std::string& newName) const {
    std::string safeName = newName.empty() ? (this->name + " - Copy") : newName;
    auto copyNb = std::make_shared<Notebook>(safeName, this->colorTag, this->iconFile);
    copyNb->guid = GUIDGenerator::GenerateV4();
    copyNb->filePath = this->filePath;
    copyNb->isOpen = this->isOpen;
    copyNb->sections.clear();
    copyNb->sectionGroups.clear();

    // Deep clone root sections
    for (const auto& sec : this->sections) {
        if (sec) {
            copyNb->sections.push_back(sec->Clone());
        }
    }

    // Deep clone section groups and their child sections recursively
    for (const auto& grp : this->sectionGroups) {
        if (grp) {
            copyNb->sectionGroups.push_back(grp->Clone(copyNb->guid));
        }
    }

    // Invariant: guarantee at least one section exists
    if (copyNb->sections.empty() && copyNb->sectionGroups.empty()) {
        copyNb->sections.push_back(std::make_shared<Section>("New Section 1"));
    }

    // Synchronize active section tracking
    copyNb->activeSectionIndex = (this->activeSectionIndex < copyNb->sections.size()) ? this->activeSectionIndex : 0;
    if (!copyNb->sections.empty()) {
        copyNb->activeSectionGuid = copyNb->sections[copyNb->activeSectionIndex]->guid;
    }

    LOG_INFO(Notebook, "Cloned notebook '" + this->name + "' -> '" + safeName + "' (" + copyNb->guid + ")");
    return copyNb;
}

/**
 * @brief Collects all CanvasPages across all sections and section groups in this notebook.
 *
 * TRAVERSAL & AGGREGATION ALGORITHM:
 * 1. Iterates over all root sections. If `includeDeleted` is false, skips sections marked
 *    as soft-deleted (`sec->IsDeleted()`).
 * 2. Collects matching canvas pages. If `includeDeleted` is false, skips pages marked
 *    as soft-deleted (`page->IsDeleted()`).
 * 3. Recursively walks all `sectionGroups` and nested `subGroups` using a recursive lambda,
 *    applying identical soft-deletion filtering rules.
 *
 * @param includeDeleted If false, excludes soft-deleted items (deletedAt > 0); if true, returns all.
 * @return std::vector<std::shared_ptr<CanvasPage>> Flat vector of matching pages in the notebook.
 */
std::vector<std::shared_ptr<CanvasPage>> Notebook::GetAllPages(bool includeDeleted) const {
    std::vector<std::shared_ptr<CanvasPage>> allPages;
    
    auto collectFromSection = [&](const std::shared_ptr<Section>& sec) {
        if (!sec) return;
        if (!includeDeleted && sec->IsDeleted()) return;
        for (const auto& page : sec->pages) {
            if (page && (includeDeleted || !page->IsDeleted())) {
                allPages.push_back(page);
            }
        }
    };

    // 1. Collect from root sections
    for (const auto& sec : sections) {
        collectFromSection(sec);
    }

    // 2. Recursively collect from SectionGroups and nested subGroups
    auto collectFromGroup = [&](auto& self, const std::shared_ptr<SectionGroup>& grp) -> void {
        if (!grp) return;
        for (const auto& sec : grp->sections) {
            collectFromSection(sec);
        }
        for (const auto& sub : grp->subGroups) {
            self(self, sub);
        }
    };

    for (const auto& grp : sectionGroups) {
        collectFromGroup(collectFromGroup, grp);
    }

    return allPages;
}
