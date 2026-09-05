#pragma once
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include "imgui.h"
#include "core/document/section.hpp"
#include "core/document/section_group.hpp"
#include "utils/guid_generator.hpp"

/**
 * =========================================================================================
 * @file notebook.hpp
 * @brief Represents a Notebook package containing Sections and Section Groups.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * - A Notebook is the top-level document package in the user's workspace.
 * - On the physical filesystem, a Notebook corresponds to a `.notebook` package folder
 *   (e.g., `Notes.notebook/`) containing an embedded SQLite database storing metadata,
 *   hierarchy tables, and binary compressed page objects.
 * - A Notebook manages:
 *     1. Top-level Sections (`sections`)
 *     2. Hierarchical Section Groups (`sectionGroups`)
 *     3. UI Theme / Tag color (`colorTag`)
 *     4. Visual icon (`iconFile`) chosen randomly or customized by the user.
 *
 * NAVIGATION TRAVERSAL:
 * - Direct lookup of sections across both top-level and nested groups via `FindSectionByGuid()`.
 * - Deep group discovery via `FindSectionGroupByGuid()`.
 * - Active page resolution down the hierarchy via `GetActivePage()`.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Global Notebook Search: SQLite FTS5 index across all pages, handwriting, and text boxes.
 * - Cloud Sync: Multi-device sync (delta changes via SQLite WAL or conflict-free sync).
 * - Full Archive Export: Export entire notebook to PDF bundle, ZIP package, or HTML site.
 * - Password Locking: Master notebook password or biometric unlock.
 */

inline const std::vector<std::string> NOTEBOOK_PRESET_ICONS = {
    "blue-notebook.svg",
    "brownyellow-notebook.svg",
    "green-notebook.svg",
    "orange-notebook.svg",
    "pink-notebook.svg",
    "red-notebook.svg",
    "saladgreen-notebook.svg",
    "violet-notebook.svg"
};

/**
 * @brief Selects a random SVG icon from the preset notebook icon palette.
 */
inline std::string GetRandomNotebookIcon() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, NOTEBOOK_PRESET_ICONS.size() - 1);
    return NOTEBOOK_PRESET_ICONS[dis(gen)];
}

class Notebook {
public:
    // -------------------------------------------------------------------------
    // Identification & Package Metadata
    // -------------------------------------------------------------------------
    std::string guid;                                                   ///< Unique persistent UUID v4 identifier
    std::string name;                                                   ///< Display name (e.g. "Work & Projects")
    std::string filePath;                                               ///< Absolute path to the .notebook folder on disk
    std::string iconFile;                                               ///< SVG icon filename
    ImVec4 colorTag = ImVec4(0.20f, 0.48f, 0.92f, 1.0f);               ///< UI accent color
    bool isOpen = true;                                                 ///< Whether notebook is expanded/open in the library

    // -------------------------------------------------------------------------
    // Sections & Groups
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<Section>> sections;                     ///< Root-level sections
    std::vector<std::shared_ptr<SectionGroup>> sectionGroups;           ///< Root-level section groups
    size_t activeSectionIndex = 0;                                      ///< Index of active root section

    // -------------------------------------------------------------------------
    // Construction & Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Constructs a new Notebook with a name, accent color tag, and icon.
     * Guarantees at least one default "New Section 1" exists.
     */
    explicit Notebook(std::string notebookName = "New Notebook", 
                      ImVec4 tag = ImVec4(0.20f, 0.48f, 0.92f, 1.0f), 
                      std::string icon = "")
        : guid(GUIDGenerator::GenerateV4()), 
          name(std::move(notebookName)), 
          colorTag(tag), 
          iconFile(std::move(icon)) {
        if (iconFile.empty()) {
            iconFile = GetRandomNotebookIcon();
        }
        // Guarantee at least one section exists
        sections.push_back(std::make_shared<Section>("New Section 1"));
    }

    // -------------------------------------------------------------------------
    // Active Item Navigation
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the currently active Section at the root level, or nullptr.
     */
    [[nodiscard]] std::shared_ptr<Section> GetActiveSection() const {
        if (activeSectionIndex < sections.size()) {
            return sections[activeSectionIndex];
        }
        return nullptr;
    }

    /**
     * @brief Resolves the currently active CanvasPage through the active section.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> GetActivePage() const {
        auto sec = GetActiveSection();
        return sec ? sec->GetActivePage() : nullptr;
    }

    // -------------------------------------------------------------------------
    // Section & Group Management
    // -------------------------------------------------------------------------

    /**
     * @brief Appends a new root-level section to this notebook.
     */
    void AddSection(std::shared_ptr<Section> section) {
        if (!section) return;
        section->sortOrder = static_cast<int32_t>(sections.size());
        sections.push_back(std::move(section));
    }

    /**
     * @brief Appends a new root-level section group to this notebook.
     */
    void AddSectionGroup(std::shared_ptr<SectionGroup> group) {
        if (!group) return;
        group->notebookGuid = this->guid;
        group->sortOrder = static_cast<int32_t>(sectionGroups.size());
        sectionGroups.push_back(std::move(group));
    }

    /**
     * @brief Removes a root section by GUID and safely clamps activeSectionIndex.
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

    // -------------------------------------------------------------------------
    // Deep Search & Query Helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Finds a section by GUID across root-level sections and all nested section groups.
     */
    [[nodiscard]] std::shared_ptr<Section> FindSectionByGuid(const std::string& secGuid) const {
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
     */
    [[nodiscard]] std::shared_ptr<SectionGroup> FindSectionGroupByGuid(const std::string& grpGuid) const {
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
     * @brief Collects all CanvasPages across all sections and section groups in this notebook.
     * Useful for global search indexing, export, and statistics.
     */
    [[nodiscard]] std::vector<std::shared_ptr<CanvasPage>> GetAllPages() const {
        std::vector<std::shared_ptr<CanvasPage>> allPages;
        
        auto collectFromSection = [&](const std::shared_ptr<Section>& sec) {
            if (!sec) return;
            for (const auto& page : sec->pages) {
                if (page) allPages.push_back(page);
            }
        };

        // Root sections
        for (const auto& sec : sections) {
            collectFromSection(sec);
        }

        // Section groups (and nested groups)
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
};