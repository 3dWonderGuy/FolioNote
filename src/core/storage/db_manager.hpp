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
    int32_t sortOrder = 0;      ///< Zero-indexed display position in the UI sidebar
    int64_t createdAt = 0;      ///< Creation timestamp
    int64_t updatedAt = 0;      ///< Last modified timestamp
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
     */
    bool SavePageMetadata(const std::string& pageGuid, const std::string& sectionGuid, 
                          const std::string& title, const std::string& createdDate, 
                          const std::string& createdTime, int32_t sortOrder,
                          bool hasBlob, const std::string& parentPageGuid = "",
                          int32_t nestingLevel = 0, bool isCollapsed = false);

    /**
     * @brief Deletes a page's metadata record from SQLite.
     */
    bool DeletePage(const std::string& pageGuid);

    /**
     * @brief Retrieves all page metadata records for a given section, ordered by sortOrder ascending.
     */
    std::vector<DBPageRecord> LoadPagesMetadata(const std::string& sectionGuid);

    /**
     * @brief Forces SQLite to execute a WAL checkpoint, merging WAL journal pages back into the main database file.
     */
    bool CheckpointWAL();

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
