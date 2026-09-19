#pragma once
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include "imgui.h"
#include "core/document/canvas_page.hpp"
#include "utils/guid_generator.hpp"
#include "utils/logger.hpp"

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
 * COLOR & ICON UNIFICATION:
 * - Employs a single template vector icon (`FOLIO_SECTION_DEFAULT_ICON` = "orange-section-simple.svg").
 * - Dynamic color styling is governed by `colorTag` (ImVec4 RGBA), matching the design pattern
 *   established for Library and Notebook entities.
 * - Tab icons are rendered as monochrome white masks and dynamically tinted at runtime, avoiding
 *   redundant disk assets for color variations.
 *
 * KEY ATTRIBUTES:
 * - `guid`: Persistent unique UUID v4 identifier.
 * - `groupGuid`: If non-empty, points to the parent SectionGroup UUID.
 * - `name`: Display name of this section.
 * - `colorTag`: Accent color vector (RGBA) used to tint the tab icon, headers, and UI indicators.
 * - `iconFile`: Base template SVG icon filename.
 * - `sortOrder`: 0-indexed integer defining the display order in the UI navigation bar.
 * - `pages`: Ordered vector of CanvasPages.
 * - `activePageIndex`: Tracks the currently viewed page within this section.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Password Protection: AES-256 encrypted sections (like OneNote's locked sections).
 * - Section Archiving / Export: Export a section as a standalone `.section` package or PDF bundle.
 * - Read-Only / Locking: Prevent accidental edits on finalized sections.
 */

/// Base template SVG icon for section tabs
inline const std::string FOLIO_SECTION_DEFAULT_ICON = "orange-section-simple.svg";

/// Curated palette of preset accent colors for section tabs
inline const std::vector<ImVec4> SECTION_PRESET_COLORS = {
    ImVec4(0.90f, 0.42f, 0.17f, 1.0f), ///< Orange (default primary)
    ImVec4(0.17f, 0.45f, 0.73f, 1.0f), ///< Blue
    ImVec4(0.18f, 0.55f, 0.34f, 1.0f), ///< Green
    ImVec4(0.73f, 0.17f, 0.55f, 1.0f), ///< Magenta
    ImVec4(0.88f, 0.41f, 0.63f, 1.0f), ///< Pink
    ImVec4(0.85f, 0.21f, 0.21f, 1.0f), ///< Red
    ImVec4(0.43f, 0.71f, 0.24f, 1.0f), ///< Salad Green
    ImVec4(0.20f, 0.63f, 0.86f, 1.0f), ///< Sky Blue
    ImVec4(0.90f, 0.71f, 0.12f, 1.0f)  ///< Yellow
};

/**
 * @brief Selects a random accent color from the curated preset color palette.
 * 
 * MATHEMATICAL WORKING PROCESS:
 * - Entropy Source: Hardware entropy via `std::random_device rd` seeds a 32-bit Mersenne
 *   Twister engine (`std::mt19937 gen`).
 * - Discrete Uniform Distribution:
 *     P(X = k) = 1 / N,  where N = SECTION_PRESET_COLORS.size()
 *   Guarantees equal probabilistic likelihood across all curated accent tones.
 * - Invariant: Thread-safe local static PRNG generator ensures efficient O(1) sampling
 *   without heap allocations or re-initialization overhead.
 * 
 * @return ImVec4 Random RGBA color vector from SECTION_PRESET_COLORS.
 */
inline ImVec4 GetRandomSectionColor() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, SECTION_PRESET_COLORS.size() - 1);
    return SECTION_PRESET_COLORS[dis(gen)];
}

class Section {
public:
    // -------------------------------------------------------------------------
    // Identification & Hierarchy
    // -------------------------------------------------------------------------
    std::string guid;               ///< Unique persistent UUID v4 identifier
    std::string notebookGuid;       ///< Foreign key pointing to parent notebook (empty if resolved dynamically)
    std::string groupGuid;          ///< Foreign key pointing to parent SectionGroup (empty if root section)
    std::string name;               ///< Display name of this section (e.g. "General Notes")
    ImVec4 colorTag = GetRandomSectionColor(); ///< UI accent color tag for tab and header (defaults to random preset)
    std::string iconFile = FOLIO_SECTION_DEFAULT_ICON;   ///< Base template tab icon SVG
    int32_t sortOrder = 0;          ///< Persistent 0-indexed UI display order position

    // -------------------------------------------------------------------------
    // Pages & Active State
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<CanvasPage>> pages;  ///< Ordered list of canvas pages
    size_t activePageIndex = 0;                      ///< Index of the currently selected page

    // -------------------------------------------------------------------------
    // Security & Protection
    // -------------------------------------------------------------------------
    bool isPasswordProtected = false;                ///< Whether section is password protected
    bool isLocked = false;                           ///< Whether section is currently locked
    std::string password;                            ///< Password hash or passcode

    // -------------------------------------------------------------------------
    // Soft Delete & Recycle Bin Lifecycle
    // -------------------------------------------------------------------------
    int64_t deletedAt = 0;                          ///< Unix timestamp (seconds) when section was soft-deleted, or 0 if active

    /**
     * @brief Checks whether this section has been moved to the recycle bin.
     * @return true if soft-deleted (deletedAt > 0), false if active.
     */
    [[nodiscard]] bool IsDeleted() const noexcept {
        return deletedAt > 0;
    }

    // -------------------------------------------------------------------------
    // Construction & Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Constructs a Section with display title, accent color, parent group GUID, and base icon.
     * Guarantees at least one blank "Untitled page" exists upon construction.
     *
     * LIFECYCLE & PERSISTENCE INVARIANT:
     * - If `color` is omitted or defaulted, a random accent color is chosen via `GetRandomSectionColor()`.
     * - When loaded from persistent SQLite storage (`LoadNotebookHierarchy`), this value is explicitly
     *   overwritten with the saved database record (`color_r, color_g, color_b, color_a`), guaranteeing
     *   user color customization is permanently preserved without subsequent re-rolls.
     *
     * @param sectionName Display title for the section (defaults to "New Section").
     * @param color UI accent color vector (RGBA, defaults to random preset).
     * @param parentGroup GUID of owning SectionGroup, or empty for root sections.
     * @param icon Base template icon filename (defaults to orange-section-simple.svg).
     */
    explicit Section(
        std::string sectionName = "New Section",
        const ImVec4& color = GetRandomSectionColor(),
        std::string parentGroup = "",
        std::string icon = FOLIO_SECTION_DEFAULT_ICON
    ) : guid(GUIDGenerator::GenerateV4()), 
        groupGuid(std::move(parentGroup)), 
        name(std::move(sectionName)), 
        colorTag(color),
        iconFile(icon.empty() ? FOLIO_SECTION_DEFAULT_ICON : std::move(icon)) {
        // Guarantee at least one blank page exists
        pages.push_back(std::make_shared<CanvasPage>("Untitled page"));
        LOG_INFO(Section, "Created Section '" + name + "' (" + guid + ") in group '" + groupGuid + "'");
    }

    /**
     * @brief Overloaded constructor for legacy calls specifying (name, icon, parentGroup).
     * Automatically assigns a random color from the preset palette.
     */
    explicit Section(std::string sectionName, std::string icon, std::string parentGroup)
        : Section(std::move(sectionName), GetRandomSectionColor(), std::move(parentGroup), std::move(icon)) {}

    /**
     * @brief Creates a duplicate clone of this section with a new GUID, cloned pages, and matching color tag.
     * Generates a fresh UUID v4 for the section and deep-clones all child pages with new GUIDs.
     *
     * @return std::shared_ptr<Section> Newly allocated cloned Section instance.
     */
    [[nodiscard]] std::shared_ptr<Section> Clone() const {
        auto clone = std::make_shared<Section>(name + " (Copy)", colorTag, groupGuid, iconFile);
        clone->guid = GUIDGenerator::GenerateV4();
        clone->pages.clear();
        for (const auto& p : pages) {
            if (p) {
                auto clonedPage = p->Clone();
                clonedPage->sortOrder = p->sortOrder;
                clone->pages.push_back(clonedPage);
            }
        }
        clone->sortOrder = sortOrder;
        clone->activePageIndex = (activePageIndex < clone->pages.size()) ? activePageIndex : 0;
        clone->isPasswordProtected = isPasswordProtected;
        clone->password = password;
        clone->isLocked = false;
        clone->deletedAt = deletedAt;
        LOG_INFO(Section, "Cloned Section '" + name + "' (" + guid + ") -> (" + clone->guid +
                 ") with " + std::to_string(clone->pages.size()) + " pages.");
        return clone;
    }

    // -------------------------------------------------------------------------
    // Page Management & Navigation
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the currently active CanvasPage in this section, or nullptr if none.
     * @return std::shared_ptr<CanvasPage> Active page pointer, or nullptr if empty.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> GetActivePage() const {
        if (activePageIndex < pages.size()) {
            return pages[activePageIndex];
        }
        return nullptr;
    }

    /**
     * @brief Appends a new page to this section, sets its sortOrder, and logs addition.
     * @param page Shared pointer to CanvasPage to add.
     */
    void AddPage(std::shared_ptr<CanvasPage> page) {
        if (!page) return;
        page->sortOrder = static_cast<int32_t>(pages.size());
        std::string pageTitle = page->title;
        std::string pageGuid = page->guid;
        pages.push_back(std::move(page));
        LOG_INFO(Section, "Added page '" + pageTitle + "' (" + pageGuid + ") to section '" + name + "'. Total pages: " + std::to_string(pages.size()));
    }

    /**
     * @brief Finds a page in this section by its persistent GUID.
     * @param pageGuid GUID identifier of page to find.
     * @return std::shared_ptr<CanvasPage> Found page pointer, or nullptr.
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
     * @brief Removes a page by its persistent GUID, adjusts activePageIndex,
     * maintains contiguous sortOrder numbering, and logs the operation.
     *
     * @param pageGuid GUID identifier of page to delete.
     * @return bool True if a page was found and removed; false otherwise.
     */
    bool RemovePage(const std::string& pageGuid) {
        auto it = std::find_if(pages.begin(), pages.end(), [&](const std::shared_ptr<CanvasPage>& p) {
            return p && p->guid == pageGuid;
        });

        if (it != pages.end()) {
            std::string pageTitle = (*it)->title;
            size_t removedIndex = std::distance(pages.begin(), it);
            pages.erase(it);

            // Re-anchor active page index to a valid position
            if (pages.empty()) {
                activePageIndex = 0;
            } else if (activePageIndex >= pages.size() || activePageIndex == removedIndex) {
                activePageIndex = (pages.size() > 0) ? std::min(removedIndex, pages.size() - 1) : 0;
            }

            // Maintain contiguous 0-indexed sortOrder sequence
            for (size_t i = 0; i < pages.size(); ++i) {
                if (pages[i]) pages[i]->sortOrder = static_cast<int32_t>(i);
            }
            LOG_INFO(Section, "Removed page '" + pageTitle + "' (" + pageGuid + ") from section '" + name + "'. Remaining pages: " + std::to_string(pages.size()));
            return true;
        }
        LOG_WARN(Section, "RemovePage failed: Page not found in section '" + name + "': " + pageGuid);
        return false;
    }

    /**
     * @brief Collects CanvasPages in this section with optional soft-delete filtering.
     *
     * @param includeDeleted If false, filters out soft-deleted pages (deletedAt > 0).
     * @return std::vector<std::shared_ptr<CanvasPage>> Flat vector of matching pages.
     */
    [[nodiscard]] std::vector<std::shared_ptr<CanvasPage>> GetAllPages(bool includeDeleted = false) const {
        std::vector<std::shared_ptr<CanvasPage>> result;
        if (!includeDeleted && IsDeleted()) return result;
        for (const auto& p : pages) {
            if (p && (includeDeleted || !p->IsDeleted())) {
                result.push_back(p);
            }
        }
        return result;
    }

    /**
     * @brief Moves a page from one index to another, keeping activePageIndex accurate and logging movement.
     *
     * INDEX CALCULATION & ACTIVE SELECTION MATH:
     * When an element is moved from `fromIdx` to `toIdx`:
     * - If `activePageIndex == fromIdx`, the active index shifts directly to `toIdx`.
     * - If `fromIdx < activePageIndex <= toIdx`, preceding elements shift left: `activePageIndex--`.
     * - If `toIdx <= activePageIndex < fromIdx`, following elements shift right: `activePageIndex++`.
     *
     * @param fromIdx Source index in `pages` vector.
     * @param toIdx Destination index in `pages` vector.
     * @return bool True if indices are valid and movement succeeded; false otherwise.
     */
    bool MovePage(size_t fromIdx, size_t toIdx) {
        if (fromIdx >= pages.size() || toIdx >= pages.size() || fromIdx == toIdx) {
            return false;
        }
        auto movedPage = pages[fromIdx];
        pages.erase(pages.begin() + fromIdx);
        pages.insert(pages.begin() + toIdx, movedPage);

        // Update active page index if affected
        if (activePageIndex == fromIdx) {
            activePageIndex = toIdx;
        } else if (fromIdx < activePageIndex && toIdx >= activePageIndex) {
            activePageIndex--;
        } else if (fromIdx > activePageIndex && toIdx <= activePageIndex) {
            activePageIndex++;
        }

        // Re-index sortOrder
        for (size_t i = 0; i < pages.size(); ++i) {
            if (pages[i]) pages[i]->sortOrder = static_cast<int32_t>(i);
        }
        LOG_INFO(Section, "Reordered page '" + (movedPage ? movedPage->title : "Unknown") + "' in section '" + name + "' from " + std::to_string(fromIdx) + " to " + std::to_string(toIdx));
        return true;
    }

    /**
     * @brief Promotes a page up the hierarchy (decreases nestingLevel, min 0).
     * @param pageIdx Index of page in `pages` vector.
     */
    void PromotePage(size_t pageIdx) {
        if (pageIdx < pages.size() && pages[pageIdx]) {
            pages[pageIdx]->nestingLevel = std::max(0, pages[pageIdx]->nestingLevel - 1);
            LOG_INFO(Section, "Promoted page '" + pages[pageIdx]->title + "' to level " + std::to_string(pages[pageIdx]->nestingLevel));
        }
    }

    /**
     * @brief Demotes a page down the hierarchy into a sub-page (increases nestingLevel, max 2).
     * @param pageIdx Index of page in `pages` vector.
     */
    void DemotePage(size_t pageIdx) {
        if (pageIdx < pages.size() && pages[pageIdx]) {
            pages[pageIdx]->nestingLevel = std::min(2, pages[pageIdx]->nestingLevel + 1);
            LOG_INFO(Section, "Demoted page '" + pages[pageIdx]->title + "' to level " + std::to_string(pages[pageIdx]->nestingLevel));
        }
    }

    /**
     * @brief Returns the total number of pages in this section.
     * @return size_t Page count.
     */
    [[nodiscard]] size_t GetPageCount() const noexcept {
        return pages.size();
    }
};