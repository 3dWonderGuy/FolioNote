#pragma once
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include "core/document/canvas_page.hpp"
#include "utils/guid_generator.hpp"

/**
 * =========================================================================================
 * @file section.hpp
 * @brief Represents a Section tab within a Notebook or Section Group.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * - A Section represents an individual thematic tab (e.g., "Lectures", "Homework", "Quick Notes").
 * - In the document hierarchy:
 *     Notebook -> [Section Group ->] Section -> CanvasPage
 * - A Section can either reside directly at the root of a Notebook or be nested inside a SectionGroup.
 * - When created, a Section automatically instantiates a default blank CanvasPage so that the canvas
 *   is never in an uninitialized empty state.
 *
 * KEY ATTRIBUTES:
 * - `guid`: Persistent unique UUID v4 identifier.
 * - `groupGuid`: If non-empty, points to the parent SectionGroup UUID.
 * - `iconFile`: Color-coded SVG tab icon chosen randomly from SECTION_PRESET_ICONS or customized by the user.
 * - `sortOrder`: 0-indexed integer defining the horizontal or vertical display order in the UI navigation bar.
 * - `pages`: Ordered vector of CanvasPages.
 * - `activePageIndex`: Tracks the currently viewed page within this section.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Tab Colors: Custom hex/ARGB color tags for section tabs.
 * - Password Protection: AES-256 encrypted sections (like OneNote's locked sections).
 * - Section Archiving / Export: Export a section as a standalone `.section` package or PDF bundle.
 * - Read-Only / Locking: Prevent accidental edits on finalized sections.
 */

inline const std::vector<std::string> SECTION_PRESET_ICONS = {
    "blue-section-simple.svg",
    "green-section-simple.svg",
    "magenta-section-simple.svg",
    "orange-section-simple.svg",
    "pink-section-simple.svg",
    "red-section-simple.svg",
    "saladgreen-section-simple.svg",
    "skyblue-section-simple.svg",
    "yellow-section-simple.svg"
};

/**
 * @brief Selects a random SVG icon from the preset section icon palette.
 */
inline std::string GetRandomSectionIcon() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, SECTION_PRESET_ICONS.size() - 1);
    return SECTION_PRESET_ICONS[dis(gen)];
}

class Section {
public:
    // -------------------------------------------------------------------------
    // Identification & Hierarchy
    // -------------------------------------------------------------------------
    std::string guid;               ///< Unique persistent UUID v4 identifier
    std::string groupGuid;          ///< Foreign key pointing to parent SectionGroup (empty if root section)
    std::string name;               ///< Display name of this section (e.g. "General Notes")
    std::string iconFile;           ///< Filename of the tab icon SVG
    int32_t sortOrder = 0;          ///< Persistent 0-indexed UI display order position

    // -------------------------------------------------------------------------
    // Pages & Active State
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<CanvasPage>> pages;  ///< Ordered list of canvas pages
    size_t activePageIndex = 0;                      ///< Index of the currently selected page

    // -------------------------------------------------------------------------
    // Construction & Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Constructs a Section with an optional name, icon, and parent group GUID.
     * Guarantees at least one blank "Untitled page" exists upon construction.
     */
    explicit Section(std::string sectionName = "New Section", std::string icon = "", std::string parentGroup = "")
        : guid(GUIDGenerator::GenerateV4()), 
          groupGuid(std::move(parentGroup)), 
          name(std::move(sectionName)), 
          iconFile(std::move(icon)) {
        if (iconFile.empty()) {
            iconFile = GetRandomSectionIcon();
        }
        // Guarantee at least one blank page exists
        pages.push_back(std::make_shared<CanvasPage>("Untitled page"));
    }

    // -------------------------------------------------------------------------
    // Page Management & Navigation
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the currently active CanvasPage in this section, or nullptr if none.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> GetActivePage() const {
        if (activePageIndex < pages.size()) {
            return pages[activePageIndex];
        }
        return nullptr;
    }

    /**
     * @brief Appends a new page to this section and sets its sortOrder.
     */
    void AddPage(std::shared_ptr<CanvasPage> page) {
        if (!page) return;
        page->sortOrder = static_cast<int32_t>(pages.size());
        pages.push_back(std::move(page));
    }

    /**
     * @brief Finds a page in this section by its persistent GUID.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> FindPageByGuid(const std::string& pageGuid) const {
        for (const auto& p : pages) {
            if (p && p->guid == pageGuid) {
                return p;
            }
        }
        return nullptr;
    }

    /**
     * @brief Removes a page by its persistent GUID and adjusts activePageIndex.
     * @return True if a page was found and removed.
     */
    bool RemovePage(const std::string& pageGuid) {
        auto it = std::find_if(pages.begin(), pages.end(), [&](const std::shared_ptr<CanvasPage>& p) {
            return p && p->guid == pageGuid;
        });

        if (it != pages.end()) {
            size_t removedIndex = std::distance(pages.begin(), it);
            pages.erase(it);

            // Re-anchor active page index to a valid position
            if (pages.empty()) {
                activePageIndex = 0;
            } else if (activePageIndex >= pages.size() || activePageIndex == removedIndex) {
                activePageIndex = (pages.size() > 0) ? std::min(removedIndex, pages.size() - 1) : 0;
            }
            return true;
        }
        return false;
    }

    /**
     * @brief Returns the total number of pages in this section.
     */
    [[nodiscard]] size_t GetPageCount() const noexcept {
        return pages.size();
    }
};