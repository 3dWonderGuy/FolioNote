#pragma once
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include "core/document/section.hpp"
#include "utils/guid_generator.hpp"

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
 * - `sortOrder`: 0-indexed integer defining position in the navigation panel.
 * - `isCollapsed`: Controls whether child sections/sub-groups are folded in the sidebar tree view.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Drag-and-Drop Re-Parenting: Move sections between groups or promote a section to root.
 * - Group Color Accents: Folder color themes matching notebook style.
 * - Batch Export: Export all sections within a group to a combined PDF or archive.
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
    int32_t sortOrder = 0;          ///< Persistent 0-indexed column order position
    bool isCollapsed = false;       ///< UI folding state (true = folded/hidden, false = expanded)

    // -------------------------------------------------------------------------
    // Contained Children
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<Section>> sections;         ///< Sections contained directly in this group
    std::vector<std::shared_ptr<SectionGroup>> subGroups;  ///< Nested child sub-groups
    size_t activeSectionIndex = 0;                          ///< Currently selected section index

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Constructs a new SectionGroup with an optional name and parent group GUID.
     */
    explicit SectionGroup(std::string groupName = "New Section Group", std::string parentGuid = "")
        : guid(GUIDGenerator::GenerateV4()), 
          parentGroupGuid(std::move(parentGuid)), 
          name(std::move(groupName)) {}

    // -------------------------------------------------------------------------
    // Navigation & Child Management
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the currently active Section in this group, or nullptr if empty.
     */
    [[nodiscard]] std::shared_ptr<Section> GetActiveSection() const {
        if (activeSectionIndex < sections.size()) {
            return sections[activeSectionIndex];
        }
        return nullptr;
    }

    /**
     * @brief Adds a section to this group, assigning this group's GUID as its parent.
     */
    void AddSection(std::shared_ptr<Section> section) {
        if (!section) return;
        section->groupGuid = this->guid;
        section->sortOrder = static_cast<int32_t>(sections.size());
        sections.push_back(std::move(section));
    }

    /**
     * @brief Adds a nested sub-group to this group, setting parentGroupGuid accordingly.
     */
    void AddSubGroup(std::shared_ptr<SectionGroup> subGroup) {
        if (!subGroup) return;
        subGroup->parentGroupGuid = this->guid;
        subGroup->sortOrder = static_cast<int32_t>(subGroups.size());
        subGroups.push_back(std::move(subGroup));
    }

    /**
     * @brief Recursively searches for a Section by GUID within this group and any nested sub-groups.
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
     * @brief Removes a section by GUID from this group.
     * @return True if the section was found and removed.
     */
    bool RemoveSection(const std::string& secGuid) {
        auto it = std::find_if(sections.begin(), sections.end(), [&](const std::shared_ptr<Section>& s) {
            return s && s->guid == secGuid;
        });

        if (it != sections.end()) {
            size_t removedIndex = std::distance(sections.begin(), it);
            sections.erase(it);

            if (sections.empty()) {
                activeSectionIndex = 0;
            } else if (activeSectionIndex >= sections.size() || activeSectionIndex == removedIndex) {
                activeSectionIndex = (sections.size() > 0) ? std::min(removedIndex, sections.size() - 1) : 0;
            }
            return true;
        }
        return false;
    }
};
