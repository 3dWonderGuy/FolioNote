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
#include "utils/logger.hpp"
#include "utils/usage_tracker.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>

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
    mutable size_t activeSectionIndex = 0;                              ///< Index of active root section
    mutable std::string activeSectionGuid;                              ///< Persistent GUID of active section (root or group)

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
        auto defaultSec = std::make_shared<Section>("New Section 1");
        activeSectionGuid = defaultSec->guid;
        sections.push_back(defaultSec);
        sessionStartTimestamp = SDL_GetTicks();
    }

    // -------------------------------------------------------------------------
    // Time Tracking Telemetry (Session & Lifetime)
    // -------------------------------------------------------------------------
    uint64_t lifetimeTimeSpentSeconds = 0;                              ///< Cumulative all-time editing time in seconds
    uint64_t sessionStartTimestamp = 0;                                 ///< SDL_GetTicks() timestamp when notebook was mounted

    void InitSessionTimer() {
        sessionStartTimestamp = SDL_GetTicks();
        LoadTimeMetadata();
    }

    void LoadTimeMetadata() {
        if (filePath.empty()) return;
        std::error_code ec;
        std::filesystem::path timeFile = std::filesystem::path(filePath) / "time.meta";
        if (std::filesystem::exists(timeFile, ec)) {
            std::ifstream in(timeFile);
            if (in >> lifetimeTimeSpentSeconds) {
                // loaded successfully
            }
        }
    }

    void SaveTimeMetadata() {
        if (filePath.empty()) return;
        std::error_code ec;
        std::filesystem::path timeFile = std::filesystem::path(filePath) / "time.meta";
        std::ofstream out(timeFile);
        if (out) {
            out << GetTotalLifetimeSeconds();
        }
    }

    [[nodiscard]] uint64_t GetSessionSeconds() const {
        if (sessionStartTimestamp == 0) return 0;
        uint64_t now = SDL_GetTicks();
        return (now >= sessionStartTimestamp) ? ((now - sessionStartTimestamp) / 1000) : 0;
    }

    [[nodiscard]] uint64_t GetTotalLifetimeSeconds() const {
        return lifetimeTimeSpentSeconds + GetSessionSeconds();
    }

    // -------------------------------------------------------------------------
    // Active Item Navigation
    // -------------------------------------------------------------------------

    /**
     * @brief Sets the active section, keeping activeSectionGuid and activeSectionIndex in sync.
     */
    void SetActiveSection(const std::shared_ptr<Section>& sec) {
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
     */
    [[nodiscard]] std::shared_ptr<Section> GetActiveSection() const {
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
        section->groupGuid.clear();
        std::string secName = section->name;
        std::string secGuid = section->guid;
        sections.push_back(std::move(section));
        LOG_INFO(Notebook, "Added root section '" + secName + "' (" + secGuid + "). Total root sections: " + std::to_string(sections.size()));
    }

    /**
     * @brief Appends a new root-level section group to this notebook.
     */
    void AddSectionGroup(std::shared_ptr<SectionGroup> group) {
        if (!group) return;
        group->notebookGuid = this->guid;
        group->sortOrder = static_cast<int32_t>(sectionGroups.size());
        std::string grpName = group->name;
        std::string grpGuid = group->guid;
        sectionGroups.push_back(std::move(group));
        LOG_INFO(Notebook, "Added section group '" + grpName + "' (" + grpGuid + "). Total groups: " + std::to_string(sectionGroups.size()));
    }

    /**
     * @brief Moves a root section from one index to another.
     */
    bool MoveSection(size_t fromIdx, size_t toIdx) {
        if (fromIdx >= sections.size() || toIdx >= sections.size() || fromIdx == toIdx) {
            return false;
        }
        auto movedSec = sections[fromIdx];
        sections.erase(sections.begin() + fromIdx);
        sections.insert(sections.begin() + toIdx, movedSec);

        if (activeSectionIndex == fromIdx) {
            activeSectionIndex = toIdx;
        } else if (fromIdx < activeSectionIndex && toIdx >= activeSectionIndex) {
            activeSectionIndex--;
        } else if (fromIdx > activeSectionIndex && toIdx <= activeSectionIndex) {
            activeSectionIndex++;
        }

        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i]) sections[i]->sortOrder = static_cast<int32_t>(i);
        }
        LOG_INFO(Notebook, "Reordered root section '" + (movedSec ? movedSec->name : "Unknown") + "' from index " + std::to_string(fromIdx) + " to " + std::to_string(toIdx));
        return true;
    }

    /**
     * @brief Moves a section group from one index to another.
     */
    bool MoveSectionGroup(size_t fromIdx, size_t toIdx) {
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
     * @param secGuid GUID of section to move
     * @param targetGroupGuid GUID of target SectionGroup
     * @param targetIdx Insertion index in target group, or (size_t)-1 to append at end
     */
    bool MoveSectionToGroup(const std::string& secGuid, const std::string& targetGroupGuid, size_t targetIdx = (size_t)-1) {
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
     * @param secGuid GUID of section to move
     * @param targetIdx Insertion index in root sections, or (size_t)-1 to append at end
     */
    bool MoveSectionToRoot(const std::string& secGuid, size_t targetIdx = (size_t)-1) {
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