#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>
#include <sqlite3.h>
#include "core/search/notebook_search_index.hpp"

namespace Folio {

/**
 * =========================================================================================
 * @file db_manager.hpp
 * @brief Thread-Safe SQLite Database Manager for FolioNote Notebooks (.fn Packages)
 * =========================================================================================
 * 
 * --- ARCHITECTURE OVERVIEW ---
 * FolioNote notebooks are organized as structured package directories containing an embedded
 * SQLite database for metadata, sections, and page catalogs, alongside isolated `.ink` binary
 * files for high-speed page graphics payloads.
 * 
 * Storage Hierarchy:
 * ┌─────────────────────────────────────────────────────────────────┐
 * │ Notebook Package Directory (.fn)                                │
 * │  ├── notebook.db     (SQLite database: schema, meta, sections)  │
 * │  ├── notebook.db-wal (High-throughput Write-Ahead Log)          │
 * │  └── pages/                                                     │
 * │       ├── [page_guid_1].ink     (Compressed vector graphics)    │
 * │       ├── [page_guid_1].ink.wal (Instant action journal)        │
 * │       └── [page_guid_2].ink                                     │
 * └─────────────────────────────────────────────────────────────────┘
 * 
 * Key Responsibilities of DBManager:
 *  - High-concurrency WAL mode configuration with zero UI thread blocking.
 *  - ACID transaction lifecycle management (Begin, Commit, Rollback).
 *  - Upsert and retrieval of Notebook metadata, hierarchical Sections, and Page metadata.
 *  - Memory-mapped I/O (MMAP) and indexed queries for sub-millisecond retrieval.
 */

/**
 * @brief Persistent notebook metadata record stored in the 'notebook_meta' SQLite table.
 */
struct DBNotebookRecord {
    std::string guid;       ///< Unique persistent UUID v4 identifier
    std::string name;       ///< User-defined display title
    float colorR = 0.2f;    ///< Visual theme color (Red channel: 0.0 - 1.0)
    float colorG = 0.48f;   ///< Visual theme color (Green channel: 0.0 - 1.0)
    float colorB = 0.92f;   ///< Visual theme color (Blue channel: 0.0 - 1.0)
    float colorA = 1.0f;    ///< Alpha opacity (1.0 = opaque)
    int64_t createdAt = 0;  ///< Unix epoch timestamp in seconds
    int64_t updatedAt = 0;  ///< Unix epoch timestamp in seconds
};

/**
 * @brief Persistent section group record stored in the 'section_groups' SQLite table.
 */
struct DBSectionGroupRecord {
    std::string guid;               ///< Unique persistent UUID v4 identifier
    std::string notebookGuid;       ///< Foreign key pointing to parent notebook
    std::string parentGroupGuid;   ///< Empty if root-level group, or GUID of parent group if nested
    std::string name;               ///< Section group folder title (e.g., "Semester 1", "Projects")
    float colorR = 0.55f;           ///< Red color component (0.0 to 1.0)
    float colorG = 0.58f;           ///< Green color component (0.0 to 1.0)
    float colorB = 0.62f;           ///< Blue color component (0.0 to 1.0)
    float colorA = 1.0f;            ///< Alpha opacity (1.0 = opaque)
    int32_t sortOrder = 0;          ///< Persistent 0-indexed column order position in sidebar
    bool isCollapsed = false;       ///< UI folding state (folded/expanded)
    int64_t createdAt = 0;          ///< Creation timestamp
    int64_t updatedAt = 0;          ///< Last modified timestamp
};

/**
 * @brief Persistent notebook section record stored in the 'sections' SQLite table.
 */
struct DBSectionRecord {
    std::string guid;           ///< Unique persistent UUID v4 identifier
    std::string notebookGuid;   ///< Foreign key pointing to parent notebook
    std::string groupGuid;      ///< Foreign key pointing to parent SectionGroup (empty if top-level)
    std::string name;           ///< Section title (e.g., "Math Notes", "Design Sketches")
    float colorR = 0.90f;       ///< Red color component (0.0 to 1.0)
    float colorG = 0.42f;       ///< Green color component (0.0 to 1.0)
    float colorB = 0.17f;       ///< Blue color component (0.0 to 1.0)
    float colorA = 1.0f;        ///< Alpha opacity (1.0 = opaque)
    int32_t sortOrder = 0;      ///< Zero-indexed display position in the UI sidebar
    int64_t createdAt = 0;      ///< Creation timestamp
    int64_t updatedAt = 0;      ///< Last modified timestamp
    int64_t deletedAt = 0;      ///< Deletion timestamp in Unix epoch seconds (0 = active, >0 = in recycle bin)
};

/**
 * @brief Persistent canvas page metadata record stored in the 'pages' SQLite table.
 */
struct DBPageRecord {
    std::string guid;           ///< Unique persistent UUID v4 identifier matching the .ink file basename
    std::string sectionGuid;    ///< Foreign key pointing to the owning section
    std::string title;          ///< User-facing page name
    std::string createdDate;    ///< Formatted ISO date (e.g. "2026-09-05")
    std::string createdTime;    ///< Formatted time (e.g. "04:30:00")
    std::string parentPageGuid; ///< Empty if root page, or GUID of parent page
    int32_t nestingLevel = 0;   ///< Hierarchy depth: 0 = Page, 1 = Sub-page, 2 = Sub-sub-page
    int32_t sortOrder = 0;      ///< Display sort order inside the section
    bool isCollapsed = false;   ///< Whether child sub-pages are folded in sidebar
    int64_t lastAccessed = 0;   ///< Telemetry timestamp for LRU memory cache eviction
    bool hasBlob = false;       ///< True if an on-disk .ink payload file exists
    bool isDedicatedPdf = false;///< True if this page is a dedicated standalone PDF reader canvas
    std::string dedicatedPdfPath;///< Persistent relative package path or absolute disk path to backing PDF
    std::string dedicatedPdfBookmarks;///< Serialized user bookmarks for this dedicated PDF page
    std::string dedicatedPdfHighlights;///< Serialized text highlight spans for this dedicated PDF page
    int64_t deletedAt = 0;      ///< Deletion timestamp in Unix epoch seconds (0 = active, >0 = in recycle bin)
};

/**
 * @brief Thread-safe SQLite database manager for FolioNote notebook packages.
 */
class DBManager {
public:
    DBManager() = default;
    ~DBManager();

    // Non-copyable to prevent duplicate database handles
    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;

    /**
     * @brief Opens a notebook SQLite file, initializes WAL mode, and builds the relational schema.
     * @param dbPath Absolute file path to the SQLite database (e.g. "C:/Notes/MyNotebook.fn/notebook.db").
     * @return true on successful connection and schema validation, false otherwise.
     */
    bool Open(const std::string& dbPath);

    /**
     * @brief Closes the active database connection safely and flushes WAL buffers.
     */
    void Close();

    /**
     * @brief Returns true if an active SQLite database connection is currently open.
     */
    [[nodiscard]] bool IsOpen() const noexcept;

    /**
     * @brief Returns the file path of the currently open database.
     */
    [[nodiscard]] std::string GetCurrentPath() const;

    // --- Atomic Transactions ---

    /**
     * @brief Begins an immediate write transaction ("BEGIN IMMEDIATE TRANSACTION;").
     */
    bool BeginTransaction();

    /**
     * @brief Commits the current active transaction ("COMMIT;").
     */
    bool CommitTransaction();

    /**
     * @brief Rolls back uncommitted changes ("ROLLBACK;").
     */
    bool RollbackTransaction();

    // --- Notebook Metadata CRUD ---

    /**
     * @brief Inserts or updates the top-level notebook metadata record (name, color theme, timestamps).
     */
    bool UpsertNotebookMeta(const DBNotebookRecord& record);

    /**
     * @brief Reads the top-level notebook metadata record.
     */
    bool LoadNotebookMeta(DBNotebookRecord& outRecord);

    // --- Section Groups CRUD ---

    /**
     * @brief Inserts or updates a section group record.
     */
    bool UpsertSectionGroup(const DBSectionGroupRecord& record);

    /**
     * @brief Deletes a section group and cascades deletion to child sections and pages.
     */
    bool DeleteSectionGroup(const std::string& groupGuid);

    /**
     * @brief Retrieves all section groups belonging to a notebook, ordered by sortOrder ascending.
     */
    std::vector<DBSectionGroupRecord> LoadSectionGroups(const std::string& notebookGuid);

    // --- Sections CRUD ---

    /**
     * @brief Inserts or updates a section record.
     */
    bool UpsertSection(const DBSectionRecord& record);

    /**
     * @brief Deletes a section and cascades deletion to all child pages via foreign keys.
     */
    bool DeleteSection(const std::string& sectionGuid);

    /**
     * @brief Retrieves all sections belonging to a notebook, ordered by sortOrder ascending.
     */
    std::vector<DBSectionRecord> LoadSections(const std::string& notebookGuid);

    // --- Page Metadata CRUD ---

    /**
     * @brief Inserts or updates a page's metadata record in SQLite.
     * @param pageGuid Unique page GUID.
     * @param sectionGuid GUID of owning section.
     * @param title User-facing title.
     * @param createdDate Creation date string.
     * @param createdTime Creation time string.
     * @param sortOrder Display position order index.
     * @param hasBlob Whether an on-disk .ink file exists.
     * @param parentPageGuid GUID of parent page for subpage trees.
     * @param nestingLevel Nesting depth (0 = root, 1 = subpage, 2 = sub-subpage).
     * @param isCollapsed Whether child subpages are folded in UI.
     * @param isDedicatedPdf Whether this page is a dedicated continuous PDF viewer page.
     * @param dedicatedPdfPath Relative package path or external disk path to the PDF document.
     * @param dedicatedPdfBookmarks Serialized user bookmarks JSON / string.
     * @param dedicatedPdfHighlights Serialized text highlights JSON / string.
     * @return true if upsert succeeded; false otherwise.
     */
    bool SavePageMetadata(const std::string& pageGuid, const std::string& sectionGuid, 
                          const std::string& title, const std::string& createdDate, 
                          const std::string& createdTime, int32_t sortOrder,
                          bool hasBlob, const std::string& parentPageGuid = "",
                          int32_t nestingLevel = 0, bool isCollapsed = false,
                          bool isDedicatedPdf = false, const std::string& dedicatedPdfPath = "",
                          const std::string& dedicatedPdfBookmarks = "",
                          const std::string& dedicatedPdfHighlights = "");

    /**
     * @brief Deletes a page's metadata record from SQLite.
     */
    bool DeletePage(const std::string& pageGuid);

    /**
     * @brief Retrieves all page metadata records for a given section, ordered by sortOrder ascending.
     */
    std::vector<DBPageRecord> LoadPagesMetadata(const std::string& sectionGuid);

    /**
     * @brief Lightweight update of a single page's sort_order column only.
     *
     * Called when a page is reordered in the nav panel but has no dirty canvas content,
     * so a full SavePageAsync (blob re-serialisation) would be wasteful.
     *
     * @param pageGuid  GUID of the page whose position changed.
     * @param sortOrder New 0-indexed position within its section.
     * @return true on success.
     */
    bool UpdatePageSortOrder(const std::string& pageGuid, int32_t sortOrder);


    /**
     * @brief Forces SQLite to execute a WAL checkpoint, merging WAL journal pages back into the main database file.
     */
    bool CheckpointWAL();

    // =========================================================================
    // RECYCLE BIN & SOFT DELETE CRUD
    // =========================================================================

    /**
     * @brief Soft-deletes a page by stamping its deleted_at timestamp in SQLite.
     *
     * Invariants:
     * - The on-disk .ink file is preserved intact for 30-day recovery.
     * - The page is excluded from regular LoadPagesMetadata queries.
     *
     * @param pageGuid Unique persistent GUID of the page.
     * @return true on successful timestamp update, false otherwise.
     */
    bool SoftDeletePage(const std::string& pageGuid);

    /**
     * @brief Restores a soft-deleted page by resetting deleted_at to NULL.
     * @param pageGuid Unique persistent GUID of the page.
     * @return true on successful restoration, false otherwise.
     */
    bool RestorePage(const std::string& pageGuid);

    /**
     * @brief Soft-deletes a section and cascades soft-deletion to all child pages.
     * @param sectionGuid Unique persistent GUID of the section.
     * @return true on successful timestamp update, false otherwise.
     */
    bool SoftDeleteSection(const std::string& sectionGuid);

    /**
     * @brief Restores a soft-deleted section and all its contained child pages.
     * @param sectionGuid Unique persistent GUID of the section.
     * @return true on successful restoration, false otherwise.
     */
    bool RestoreSection(const std::string& sectionGuid);

    /**
     * @brief Retrieves all soft-deleted sections belonging to a notebook package.
     * @param notebookGuid GUID of the parent notebook.
     * @return List of DBSectionRecord objects currently residing in the recycle bin.
     */
    std::vector<DBSectionRecord> LoadDeletedSections(const std::string& notebookGuid);

    /**
     * @brief Retrieves all soft-deleted pages belonging to a notebook package.
     * @param notebookGuid GUID of the parent notebook.
     * @return List of DBPageRecord objects currently residing in the recycle bin.
     */
    std::vector<DBPageRecord> LoadDeletedPages(const std::string& notebookGuid);

    /**
     * @brief Permanently purges a page record from SQLite and unlinks its .ink file on disk.
     * @param pageGuid Unique persistent GUID of the page.
     * @param pkgPath Absolute filesystem path to the .notebook folder.
     * @return true on successful purge, false otherwise.
     */
    bool PermanentlyDeletePage(const std::string& pageGuid, const std::string& pkgPath);

    /**
     * @brief Permanently purges a section record, all its child pages, and their .ink files.
     * @param sectionGuid Unique persistent GUID of the section.
     * @param pkgPath Absolute filesystem path to the .notebook folder.
     * @return true on successful purge, false otherwise.
     */
    bool PermanentlyDeleteSection(const std::string& sectionGuid, const std::string& pkgPath);

    /**
     * @brief Empties all soft-deleted pages and sections from this notebook package.
     * @param notebookGuid GUID of the notebook.
     * @param pkgPath Absolute filesystem path to the .notebook folder.
     * @return Count of permanently purged pages and sections.
     */
    size_t EmptyRecycleBin(const std::string& notebookGuid, const std::string& pkgPath);

    /**
     * @brief Automatically purges items in the recycle bin older than the specified retention window.
     *
     * General Working Process:
     * Calculates cutoff timestamp = now - maxAgeSeconds (default 30 days = 2,592,000s).
     * Discovers all expired pages, unlinks their .ink payloads, and purges database records.
     *
     * @param notebookGuid GUID of the notebook.
     * @param pkgPath Absolute filesystem path to the .notebook folder.
     * @param maxAgeSeconds Maximum retention window in seconds (default: 30 days).
     * @return Number of expired records purged.
     */
    size_t PurgeExpiredRecycleBinItems(const std::string& notebookGuid, const std::string& pkgPath, int64_t maxAgeSeconds = 30LL * 24 * 60 * 60);

    // --- Full-Text Search (FTS5) & Content Indexing ---

    /**
     * @brief Performs a full-text search across the notebook with snippet extraction and spatial coordinates.
     */
    std::vector<SearchResult> SearchContent(const std::string& notebookGuid, 
                                            const std::string& queryText, 
                                            const std::string& tagFilter = "", 
                                            int limit = 50);

    /**
     * @brief Automatically indexes all text and tags inside a CanvasPage into the notebook's search catalog.
     */
    bool IndexPageContent(const std::string& notebookGuid, 
                          const std::string& sectionGuid, 
                          const CanvasPage& page);

    /**
     * @brief Returns a summary of all unique hashtags and labels used in the notebook.
     */
    std::vector<TagSummary> GetNotebookTags(const std::string& notebookGuid);

private:
    sqlite3* db = nullptr;
    std::string currentDbPath;
    mutable std::mutex dbMutex;
    bool isOpen = false;

    void CloseInternal();
    void ExecutePragmas();
    bool InitSchema();
    bool ExecuteSimpleSQL(const char* sql);
    int64_t GetCurrentTimestamp() const;
};

} // namespace Folio
