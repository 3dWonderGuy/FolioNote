#pragma once
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
 */

#include <string>
#include <vector>
#include <memory>

#include "imgui.h"
#include "core/document/section.hpp"
#include "core/document/section_group.hpp"

/// Standard default SVG icon for all notebook packages.
inline constexpr const char* FOLIO_NOTEBOOK_DEFAULT_ICON = "orange-notebook.svg";

class Notebook {
public:
    // -------------------------------------------------------------------------
    // Identification & Package Metadata
    // -------------------------------------------------------------------------
    std::string guid;                                                   ///< Unique persistent UUID v4 identifier
    std::string name;                                                   ///< Display name (e.g. "Work & Projects")
    std::string filePath;                                               ///< Absolute path to the .notebook folder on disk
    std::string iconFile = FOLIO_NOTEBOOK_DEFAULT_ICON;                 ///< SVG icon filename
    ImVec4 colorTag = GetRandomSectionColor();                          ///< UI accent color (defaults to random preset)
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
     *
     * LIFECYCLE & PERSISTENCE INVARIANT:
     * - If `tag` is omitted or defaulted, a random accent color is chosen via `GetRandomSectionColor()`.
     * - When loaded from persistent SQLite storage (`LoadNotebookHierarchy`), this value is explicitly
     *   overwritten with the saved database record (`color_r, color_g, color_b, color_a`), guaranteeing
     *   user color customization is permanently preserved without subsequent re-rolls.
     *
     * @param notebookName Display title (defaults to "New Notebook").
     * @param tag UI accent color vector (defaults to random preset).
     * @param icon SVG icon file name (defaults to orange-notebook.svg).
     */
    explicit Notebook(std::string notebookName = "New Notebook", 
                      ImVec4 tag = GetRandomSectionColor(), 
                      std::string icon = FOLIO_NOTEBOOK_DEFAULT_ICON);

    // -------------------------------------------------------------------------
    // Active Item Navigation
    // -------------------------------------------------------------------------

    /**
     * @brief Sets the active section, keeping activeSectionGuid and activeSectionIndex in sync.
     * @param sec Shared pointer to the section to activate.
     */
    void SetActiveSection(const std::shared_ptr<Section>& sec);

    /**
     * @brief Returns the currently active Section (from root or any SectionGroup), or nullptr.
     * @return std::shared_ptr<Section> Active section pointer, or nullptr if none found.
     */
    [[nodiscard]] std::shared_ptr<Section> GetActiveSection() const;

    /**
     * @brief Resolves the currently active CanvasPage through the active section.
     * @return std::shared_ptr<CanvasPage> Active page pointer, or nullptr if none.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> GetActivePage() const;

    // -------------------------------------------------------------------------
    // Section & Group Management
    // -------------------------------------------------------------------------

    /**
     * @brief Appends a new root-level section to this notebook.
     * @param section Shared pointer to the new Section to add.
     */
    void AddSection(std::shared_ptr<Section> section);

    /**
     * @brief Appends a new root-level section group to this notebook.
     * @param group Shared pointer to the SectionGroup to add.
     */
    void AddSectionGroup(std::shared_ptr<SectionGroup> group);

    /**
     * @brief Moves a root section from one index to another.
     * @param fromIdx Source index in sections vector.
     * @param toIdx Destination index in sections vector.
     * @return bool True if indices are valid and movement succeeded; false otherwise.
     */
    bool MoveSection(size_t fromIdx, size_t toIdx);

    /**
     * @brief Moves a section group from one index to another.
     * @param fromIdx Source index in sectionGroups vector.
     * @param toIdx Destination index in sectionGroups vector.
     * @return bool True if movement succeeded; false on invalid bounds.
     */
    bool MoveSectionGroup(size_t fromIdx, size_t toIdx);

    /**
     * @brief Moves a section (from root or another group) into a target SectionGroup.
     * @param secGuid GUID of section to move.
     * @param targetGroupGuid GUID of target SectionGroup.
     * @param targetIdx Insertion index in target group, or (size_t)-1 to append at end.
     * @return bool True if section was relocated successfully; false otherwise.
     */
    bool MoveSectionToGroup(const std::string& secGuid, const std::string& targetGroupGuid, size_t targetIdx = (size_t)-1);

    /**
     * @brief Moves a section from a SectionGroup back to the notebook root sections list.
     * @param secGuid GUID of section to move.
     * @param targetIdx Insertion index in root sections, or (size_t)-1 to append at end.
     * @return bool True if relocated; false if section not found in any group.
     */
    bool MoveSectionToRoot(const std::string& secGuid, size_t targetIdx = (size_t)-1);

    /**
     * @brief Removes a root section by GUID and safely clamps activeSectionIndex.
     * @param secGuid GUID identifier of root section to delete.
     * @return bool True if found and removed; false if not found.
     */
    bool RemoveSection(const std::string& secGuid);

    // -------------------------------------------------------------------------
    // Deep Search & Query Helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Finds a section by GUID across root-level sections and all nested section groups.
     * @param secGuid GUID of the section to locate.
     * @return std::shared_ptr<Section> Matching section pointer, or nullptr if not found.
     */
    [[nodiscard]] std::shared_ptr<Section> FindSectionByGuid(const std::string& secGuid) const;

    /**
     * @brief Finds a section group by GUID across all root and nested groups.
     * @param grpGuid GUID of the SectionGroup to locate.
     * @return std::shared_ptr<SectionGroup> Matching group pointer, or nullptr if not found.
     */
    [[nodiscard]] std::shared_ptr<SectionGroup> FindSectionGroupByGuid(const std::string& grpGuid) const;

    /**
     * @brief Performs an in-memory deep copy of this notebook, including its sections and section groups.
     * Generates a new GUID and preserves visual tags and active section states.
     *
     * @param newName Optional title for the cloned notebook (defaults to current name + " - Copy").
     * @return std::shared_ptr<Notebook> Newly allocated deep-cloned Notebook instance.
     */
    [[nodiscard]] std::shared_ptr<Notebook> Clone(const std::string& newName = "") const;

    /**
     * @brief Collects all CanvasPages across all sections and section groups in this notebook.
     *
     * @param includeDeleted If false (default), excludes soft-deleted sections and pages (deletedAt > 0).
     * @return std::vector<std::shared_ptr<CanvasPage>> Flat vector of all matching pages in the notebook.
     */
    [[nodiscard]] std::vector<std::shared_ptr<CanvasPage>> GetAllPages(bool includeDeleted = false) const;
};