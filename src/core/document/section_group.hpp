#pragma once
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include "imgui.h"
#include "core/document/section.hpp"
#include "utils/guid_generator.hpp"
#include "utils/logger.hpp"

/**
 * =========================================================================================
 * @file section_group.hpp
 * @brief Represents a Section Group folder containing Sections and optional nested Sub-Groups.
 * =========================================================================================
 * 
 * ARCHITECTURAL ROLE:
 * - A SectionGroup acts as a hierarchical folder structure within a Notebook.
 * - Notebooks can hold both root-level Sections and SectionGroups.
 * - SectionGroups can be nested recursively via `parentGroupGuid` and `subGroups`.
 * - In the document tree:
 *     Notebook -> SectionGroup -> [SubGroup -> ...] Section -> CanvasPage
 *
 * KEY ATTRIBUTES:
 * - `guid`: Unique UUID v4 for persistent identification across sessions and storage.
 * - `notebookGuid`: Persistent foreign key linking back to the owning Notebook.
 * - `parentGroupGuid`: Empty string if at the notebook root level, or parent group GUID if nested.
 * - `name`: Display title of the section group folder.
 * - `colorTag`: Accent color vector (RGBA) for folder tabs and sidebar tree indicators.
 * - `sortOrder`: 0-indexed integer defining position in the navigation panel.
 * - `isCollapsed`: Controls whether child sections/sub-groups are folded in the sidebar tree view.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Drag-and-Drop Re-Parenting: Move sections between groups or promote a section to root.
 * - Batch Export: Export all sections within a group to a combined PDF or archive.
 * - Read-Only / Locking: Prevent accidental edits on finalized sections.
 */
class SectionGroup {
public:
    // -------------------------------------------------------------------------
    // Identification & Hierarchy
    // -------------------------------------------------------------------------
    std::string guid;               ///< Unique persistent UUID v4 identifier
    std::string notebookGuid;       ///< ID of owning notebook
    std::string parentGroupGuid;   ///< Empty if root-level group, or GUID of parent group if nested
    std::string name;               ///< Display title of the section group folder
    ImVec4 colorTag = GetRandomSectionColor(); ///< Folder accent color tag (defaults to random preset)
    int32_t sortOrder = 0;          ///< Persistent 0-indexed column order position
    bool isCollapsed = false;       ///< UI folding state (true = folded/hidden, false = expanded)

    // -------------------------------------------------------------------------
    // Soft Delete & Recycle Bin Lifecycle
    // -------------------------------------------------------------------------
    int64_t deletedAt = 0;          ///< Unix timestamp (seconds) when group was soft-deleted, or 0 if active

    /**
     * @brief Checks whether this section group has been moved to the recycle bin.
     * @return bool True if soft-deleted (deletedAt > 0), false if active.
     */
    [[nodiscard]] bool IsDeleted() const noexcept {
        return deletedAt > 0;
    }

    // -------------------------------------------------------------------------
    // Contained Children
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<Section>> sections;         ///< Sections contained directly in this group
    std::vector<std::shared_ptr<SectionGroup>> subGroups;  ///< Nested child sub-groups
    size_t activeSectionIndex = 0;                          ///< Currently selected section index

    // -------------------------------------------------------------------------
    // Construction & Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Constructs a new SectionGroup with an optional name, parent group GUID, and color tag.
     * Generates a fresh UUID v4 for persistent identification and logs creation.
     *
     * LIFECYCLE & PERSISTENCE INVARIANT:
     * - If `color` is omitted or defaulted, a random accent color is chosen via `GetRandomSectionColor()`.
     * - When loaded from persistent SQLite storage (`LoadNotebookHierarchy`), this value is explicitly
     *   overwritten with the saved database record (`color_r, color_g, color_b, color_a`), guaranteeing
     *   user color customization is permanently preserved without subsequent re-rolls.
     *
     * @param groupName Display title of the section group (defaults to "New Section Group").
     * @param parentGuid GUID of owning parent SectionGroup, or empty for notebook-root groups.
     * @param color Accent color vector (RGBA, defaults to random preset).
     */
    explicit SectionGroup(
        std::string groupName = "New Section Group",
        std::string parentGuid = "",
        const ImVec4& color = GetRandomSectionColor()
    )   : guid(GUIDGenerator::GenerateV4()), 
          parentGroupGuid(std::move(parentGuid)), 
          name(std::move(groupName)),
          colorTag(color) {
        LOG_INFO(SectionGroup, "Created SectionGroup '" + name + "' (" + guid + ") with parent: '" + parentGroupGuid + "'");
    }

    /**
     * @brief Creates an in-memory deep copy of this section group and all child sections and sub-groups.
     *
     * RECURSIVE DEEP CLONE ALGORITHM:
     * 1. Allocates a new SectionGroup instance with a newly minted UUID v4.
     * 2. Sets foreign keys (`notebookGuid` and `parentGroupGuid`) to link with the cloned hierarchy.
     * 3. Copies `colorTag`, `sortOrder`, and `isCollapsed`.
     * 4. Iterates child `sections`, cloning each via `sec->Clone()` (which generates new GUIDs for
     *    sections and their canvas pages), and reparents them with `clonedSec->groupGuid = clone->guid`.
     * 5. Recursively calls `sub->Clone(clone->notebookGuid, clone->guid)` on all child `subGroups`,
     *    ensuring arbitrary nesting depth is fully cloned without shared pointer aliasing.
     * 6. Emits diagnostic logging with section and sub-group counts.
     *
     * @param newNotebookGuid Foreign key linking back to the cloned owning Notebook.
     * @param newParentGroupGuid GUID of parent group if nested, or empty for root-level groups.
     * @return std::shared_ptr<SectionGroup> Newly allocated deep-cloned SectionGroup model.
     */
    [[nodiscard]] std::shared_ptr<SectionGroup> Clone(
        const std::string& newNotebookGuid = "",
        const std::string& newParentGroupGuid = ""
    ) const {
        auto clone = std::make_shared<SectionGroup>(name, newParentGroupGuid.empty() ? parentGroupGuid : newParentGroupGuid, colorTag);
        clone->guid = GUIDGenerator::GenerateV4();
        clone->notebookGuid = newNotebookGuid.empty() ? this->notebookGuid : newNotebookGuid;
        clone->sortOrder = this->sortOrder;
        clone->isCollapsed = this->isCollapsed;
        clone->deletedAt = this->deletedAt;
        clone->activeSectionIndex = this->activeSectionIndex;

        // Deep clone child sections with new GUIDs
        clone->sections.clear();
        for (const auto& sec : sections) {
            if (sec) {
                auto clonedSec = sec->Clone();
                clonedSec->groupGuid = clone->guid;
                clonedSec->sortOrder = sec->sortOrder;
                clone->sections.push_back(clonedSec);
            }
        }

        // Deep clone nested sub-groups recursively with new GUIDs
        clone->subGroups.clear();
        for (const auto& sub : subGroups) {
            if (sub) {
                auto clonedSub = sub->Clone(clone->notebookGuid, clone->guid);
                clone->subGroups.push_back(clonedSub);
            }
        }

        LOG_INFO(SectionGroup, "Cloned SectionGroup '" + name + "' (" + guid + ") -> (" + clone->guid +
                 ") with " + std::to_string(clone->sections.size()) + " sections and " +
                 std::to_string(clone->subGroups.size()) + " sub-groups");
        return clone;
    }

    // -------------------------------------------------------------------------
    // Navigation & Child Management
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the currently active Section in this group, or nullptr if empty.
     * @return std::shared_ptr<Section> Active section pointer, or nullptr if out of bounds.
     */
    [[nodiscard]] std::shared_ptr<Section> GetActiveSection() const {
        if (activeSectionIndex < sections.size()) {
            return sections[activeSectionIndex];
        }
        return nullptr;
    }

    /**
     * @brief Adds a section to this group, assigning this group's GUID as its parent.
     * Automatically assigns sequential sortOrder matching its insertion index and logs operation.
     *
     * @param section Shared pointer to Section to add.
     */
    void AddSection(std::shared_ptr<Section> section) {
        if (!section) return;
        section->groupGuid = this->guid;
        section->sortOrder = static_cast<int32_t>(sections.size());
        std::string secName = section->name;
        std::string secGuid = section->guid;
        sections.push_back(std::move(section));
        LOG_INFO(SectionGroup, "Added section '" + secName + "' (" + secGuid + ") to group '" + name + "'. Total sections: " + std::to_string(sections.size()));
    }

    /**
     * @brief Adds a nested sub-group to this group, setting parentGroupGuid accordingly.
     * Automatically assigns sequential sortOrder matching its insertion index and logs operation.
     *
     * @param subGroup Shared pointer to child SectionGroup to nest.
     */
    void AddSubGroup(std::shared_ptr<SectionGroup> subGroup) {
        if (!subGroup) return;
        subGroup->parentGroupGuid = this->guid;
        subGroup->sortOrder = static_cast<int32_t>(subGroups.size());
        std::string subName = subGroup->name;
        std::string subGuid = subGroup->guid;
        subGroups.push_back(std::move(subGroup));
        LOG_INFO(SectionGroup, "Added sub-group '" + subName + "' (" + subGuid + ") to group '" + name + "'. Total sub-groups: " + std::to_string(subGroups.size()));
    }

    /**
     * @brief Reorders a section within this group, adjusting sort orders and active selection.
     *
     * INDEX CALCULATION & ACTIVE SELECTION MATH:
     * When an element is shifted from `fromIdx` to `toIdx`:
     * - If `activeSectionIndex == fromIdx`, the active index shifts directly to `toIdx`.
     * - If `fromIdx < activeSectionIndex <= toIdx`, preceding elements shift left: `activeSectionIndex--`.
     * - If `toIdx <= activeSectionIndex < fromIdx`, following elements shift right: `activeSectionIndex++`.
     *
     * @param fromIdx Source index in `sections` vector.
     * @param toIdx Destination index in `sections` vector.
     * @return bool True if indices are valid and movement succeeded; false otherwise.
     */
    bool MoveSection(size_t fromIdx, size_t toIdx) {
        if (fromIdx >= sections.size() || toIdx >= sections.size() || fromIdx == toIdx) {
            return false;
        }
        auto movedSec = sections[fromIdx];
        sections.erase(sections.begin() + fromIdx);
        sections.insert(sections.begin() + toIdx, movedSec);

        // Update active selection index
        if (activeSectionIndex == fromIdx) {
            activeSectionIndex = toIdx;
        } else if (fromIdx < activeSectionIndex && toIdx >= activeSectionIndex) {
            activeSectionIndex--;
        } else if (fromIdx > activeSectionIndex && toIdx <= activeSectionIndex) {
            activeSectionIndex++;
        }

        // Re-index all sort orders to guarantee contiguous sequence
        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i]) sections[i]->sortOrder = static_cast<int32_t>(i);
        }
        LOG_INFO(SectionGroup, "Reordered section '" + (movedSec ? movedSec->name : "Unknown") + "' in group '" + name + "' from index " + std::to_string(fromIdx) + " to " + std::to_string(toIdx));
        return true;
    }

    /**
     * @brief Recursively searches for a Section by GUID within this group and any nested sub-groups.
     * @param secGuid GUID of target section.
     * @return std::shared_ptr<Section> Found section or nullptr.
     */
    [[nodiscard]] std::shared_ptr<Section> FindSectionByGuid(const std::string& secGuid) const {
        for (const auto& sec : sections) {
            if (sec && sec->guid == secGuid) return sec;
        }
        for (const auto& sub : subGroups) {
            if (sub) {
                if (auto found = sub->FindSectionByGuid(secGuid)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    /**
     * @brief Recursively searches for a SubGroup by GUID within this group hierarchy.
     * @param grpGuid GUID of target sub-group.
     * @return std::shared_ptr<SectionGroup> Found sub-group or nullptr.
     */
    [[nodiscard]] std::shared_ptr<SectionGroup> FindSubGroupByGuid(const std::string& grpGuid) const {
        for (const auto& sub : subGroups) {
            if (sub) {
                if (sub->guid == grpGuid) return sub;
                if (auto found = sub->FindSubGroupByGuid(grpGuid)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    /**
     * @brief Removes a section by GUID from this group, safely clamping activeSectionIndex,
     * maintaining a contiguous sortOrder sequence, and logging the outcome.
     *
     * @param secGuid GUID of section to remove.
     * @return bool True if the section was found and removed; false otherwise.
     */
    bool RemoveSection(const std::string& secGuid) {
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

            // Maintain contiguous 0-indexed sortOrder sequence
            for (size_t i = 0; i < sections.size(); ++i) {
                if (sections[i]) sections[i]->sortOrder = static_cast<int32_t>(i);
            }
            LOG_INFO(SectionGroup, "Removed section '" + secName + "' (" + secGuid + ") from group '" + name + "'. Remaining sections: " + std::to_string(sections.size()));
            return true;
        }
        LOG_WARN_CODE(SectionGroup, FolioErrorCode::DocSectionNotFound, 
                      "RemoveSection failed: Section not found in group '" + name + "': " + secGuid);
        return false;
    }

    /**
     * @brief Recursively collects all CanvasPages across all sections and sub-groups in this group.
     *
     * @param includeDeleted If false, filters out soft-deleted sections and pages (deletedAt > 0).
     * @return std::vector<std::shared_ptr<CanvasPage>> Flat vector of all matching canvas pages.
     */
    [[nodiscard]] std::vector<std::shared_ptr<CanvasPage>> GetAllPages(bool includeDeleted = false) const {
        std::vector<std::shared_ptr<CanvasPage>> pages;
        if (!includeDeleted && IsDeleted()) return pages;

        for (const auto& sec : sections) {
            if (sec && (includeDeleted || !sec->IsDeleted())) {
                for (const auto& p : sec->pages) {
                    if (p && (includeDeleted || !p->IsDeleted())) {
                        pages.push_back(p);
                    }
                }
            }
        }
        for (const auto& sub : subGroups) {
            if (sub && (includeDeleted || !sub->IsDeleted())) {
                auto subPages = sub->GetAllPages(includeDeleted);
                pages.insert(pages.end(), subPages.begin(), subPages.end());
            }
        }
        return pages;
    }
};
