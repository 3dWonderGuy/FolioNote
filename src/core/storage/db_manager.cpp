#include "core/storage/db_manager.hpp"
#include "utils/logger.hpp"
#include <chrono>

namespace Folio {

DBManager::~DBManager() {
    Close();
}

/**
 * @brief Opens a notebook database, sets high-performance SQLite PRAGMAs, and initializes schema.
 */
bool DBManager::Open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(dbMutex);
    CloseInternal();

    // 1. Establish SQLite database connection
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::string err = db ? sqlite3_errmsg(db) : "Unable to allocate SQLite memory";
        LOG_ERROR(DBManager, "Failed to open SQLite database: " + dbPath + " | Error: " + err);
        CloseInternal();
        return false;
    }

    currentDbPath = dbPath;

    // 2. Configure concurrency pragmas (WAL mode, Normal sync, Memory-mapped I/O)
    ExecutePragmas();

    // 3. Initialize relational tables and search indexes
    if (!InitSchema()) {
        LOG_ERROR(DBManager, "Failed to initialize database schema in: " + dbPath);
        CloseInternal();
        return false;
    }

    isOpen = true;
    LOG_INFO(DBManager, "Successfully opened notebook database (WAL mode): " + dbPath);
    return true;
}

/**
 * @brief Closes the database handle and clears state.
 */
void DBManager::Close() {
    std::lock_guard<std::mutex> lock(dbMutex);
    CloseInternal();
}

void DBManager::CloseInternal() {
    if (db) {
        // Attempt clean close
        int rc = sqlite3_close(db);
        if (rc != SQLITE_OK) {
            LOG_WARN(DBManager, "sqlite3_close reported busy handles; executing sqlite3_close_v2");
            sqlite3_close_v2(db);
        }
        db = nullptr;
    }
    isOpen = false;
    currentDbPath.clear();
}

bool DBManager::IsOpen() const noexcept {
    return isOpen && (db != nullptr);
}

std::string DBManager::GetCurrentPath() const {
    std::lock_guard<std::mutex> lock(dbMutex);
    return currentDbPath;
}

/**
 * @brief Executes performance, locking, and durability PRAGMAs.
 * 
 * - journal_mode = WAL: Enables Write-Ahead Logging for high concurrency without blocking readers.
 * - synchronous = NORMAL: In WAL mode, provides full ACID crash safety while skipping heavy fsync on each commit.
 * - busy_timeout = 5000: Auto-retries busy locks for up to 5 seconds before returning SQLITE_BUSY.
 * - foreign_keys = ON: Enforces relational cascade deletions (deleting section cascades to pages).
 * - mmap_size = 256MB: Uses OS zero-copy memory mapping for fast sequential reads.
 */
void DBManager::ExecutePragmas() {
    char* err = nullptr;
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);

    sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);

    sqlite3_exec(db, "PRAGMA busy_timeout = 5000;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);

    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);

    sqlite3_exec(db, "PRAGMA mmap_size = 268435456;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

/**
 * @brief Creates the tables and indexes if they do not already exist.
 */
bool DBManager::InitSchema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS notebook_meta (
            guid TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            color_r REAL NOT NULL,
            color_g REAL NOT NULL,
            color_b REAL NOT NULL,
            color_a REAL NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS section_groups (
            guid TEXT PRIMARY KEY,
            notebook_guid TEXT NOT NULL,
            parent_group_guid TEXT,
            name TEXT NOT NULL,
            sort_order INTEGER NOT NULL DEFAULT 0,
            is_collapsed INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            FOREIGN KEY (notebook_guid) REFERENCES notebook_meta(guid) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS sections (
            guid TEXT PRIMARY KEY,
            notebook_guid TEXT NOT NULL,
            group_guid TEXT,
            name TEXT NOT NULL,
            sort_order INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            FOREIGN KEY (notebook_guid) REFERENCES notebook_meta(guid) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS pages (
            guid TEXT PRIMARY KEY,
            section_guid TEXT NOT NULL,
            title TEXT NOT NULL,
            created_date TEXT NOT NULL,
            created_time TEXT NOT NULL,
            parent_page_guid TEXT,
            nesting_level INTEGER NOT NULL DEFAULT 0,
            sort_order INTEGER NOT NULL DEFAULT 0,
            is_collapsed INTEGER NOT NULL DEFAULT 0,
            has_blob INTEGER NOT NULL DEFAULT 0,
            last_accessed INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (section_guid) REFERENCES sections(guid) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_section_groups ON section_groups(notebook_guid, sort_order);
        CREATE INDEX IF NOT EXISTS idx_sections_notebook ON sections(notebook_guid, sort_order);
        CREATE INDEX IF NOT EXISTS idx_pages_section ON pages(section_guid, sort_order);
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db, schema, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        LOG_ERROR(DBManager, "Schema creation error: " + std::string(err ? err : "Unknown"));
        if (err) sqlite3_free(err);
        return false;
    }

    // Non-destructive migrations for existing database files
    sqlite3_exec(db, "ALTER TABLE sections ADD COLUMN group_guid TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN parent_page_guid TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN nesting_level INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN is_collapsed INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);

    // Initialize FTS5 and spatial search catalog
    if (!NotebookSearchIndex::InitSchema(db)) {
        LOG_WARN(DBManager, "Failed to initialize FTS5 search schema; falling back to relational queries.");
    }
    return true;
}

bool DBManager::ExecuteSimpleSQL(const char* sql) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err) {
            LOG_ERROR(DBManager, "SQL execution error: " + std::string(err) + " in query: " + sql);
            sqlite3_free(err);
        }
        return false;
    }
    return true;
}

int64_t DBManager::GetCurrentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool DBManager::BeginTransaction() {
    return ExecuteSimpleSQL("BEGIN IMMEDIATE TRANSACTION;");
}

bool DBManager::CommitTransaction() {
    return ExecuteSimpleSQL("COMMIT;");
}

bool DBManager::RollbackTransaction() {
    return ExecuteSimpleSQL("ROLLBACK;");
}

/**
 * @brief Upserts top-level notebook metadata (name, color tags, timestamp).
 */
bool DBManager::UpsertNotebookMeta(const DBNotebookRecord& record) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = R"(
        INSERT INTO notebook_meta (guid, name, color_r, color_g, color_b, color_a, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(guid) DO UPDATE SET
            name = excluded.name,
            color_r = excluded.color_r,
            color_g = excluded.color_g,
            color_b = excluded.color_b,
            color_a = excluded.color_a,
            updated_at = excluded.updated_at;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare UpsertNotebookMeta: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    int64_t now = GetCurrentTimestamp();
    sqlite3_bind_text(stmt, 1, record.guid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, record.colorR);
    sqlite3_bind_double(stmt, 4, record.colorG);
    sqlite3_bind_double(stmt, 5, record.colorB);
    sqlite3_bind_double(stmt, 6, record.colorA);
    sqlite3_bind_int64(stmt, 7, record.createdAt > 0 ? record.createdAt : now);
    sqlite3_bind_int64(stmt, 8, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Loads the notebook's top-level metadata record.
 */
bool DBManager::LoadNotebookMeta(DBNotebookRecord& outRecord) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = "SELECT guid, name, color_r, color_g, color_b, color_a, created_at, updated_at FROM notebook_meta LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare LoadNotebookMeta: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        outRecord.guid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        outRecord.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        outRecord.colorR = static_cast<float>(sqlite3_column_double(stmt, 2));
        outRecord.colorG = static_cast<float>(sqlite3_column_double(stmt, 3));
        outRecord.colorB = static_cast<float>(sqlite3_column_double(stmt, 4));
        outRecord.colorA = static_cast<float>(sqlite3_column_double(stmt, 5));
        outRecord.createdAt = sqlite3_column_int64(stmt, 6);
        outRecord.updatedAt = sqlite3_column_int64(stmt, 7);
        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);
    return false;
}

/**
 * @brief Upserts a section group record.
 */
bool DBManager::UpsertSectionGroup(const DBSectionGroupRecord& record) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = R"(
        INSERT INTO section_groups (guid, notebook_guid, parent_group_guid, name, sort_order, is_collapsed, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(guid) DO UPDATE SET
            notebook_guid = excluded.notebook_guid,
            parent_group_guid = excluded.parent_group_guid,
            name = excluded.name,
            sort_order = excluded.sort_order,
            is_collapsed = excluded.is_collapsed,
            updated_at = excluded.updated_at;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare UpsertSectionGroup: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    int64_t now = GetCurrentTimestamp();
    sqlite3_bind_text(stmt, 1, record.guid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.notebookGuid.c_str(), -1, SQLITE_TRANSIENT);
    if (!record.parentGroupGuid.empty()) {
        sqlite3_bind_text(stmt, 3, record.parentGroupGuid.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, record.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, record.sortOrder);
    sqlite3_bind_int(stmt, 6, record.isCollapsed ? 1 : 0);
    sqlite3_bind_int64(stmt, 7, record.createdAt > 0 ? record.createdAt : now);
    sqlite3_bind_int64(stmt, 8, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Deletes a section group.
 */
bool DBManager::DeleteSectionGroup(const std::string& groupGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = "DELETE FROM section_groups WHERE guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare DeleteSectionGroup: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, groupGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Loads all section groups belonging to a notebook, ordered by sortOrder ascending.
 */
std::vector<DBSectionGroupRecord> DBManager::LoadSectionGroups(const std::string& notebookGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<DBSectionGroupRecord> results;
    if (!db) return results;

    const char* sql = "SELECT guid, notebook_guid, parent_group_guid, name, sort_order, is_collapsed, created_at, updated_at FROM section_groups WHERE notebook_guid = ? ORDER BY sort_order ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare LoadSectionGroups: " + std::string(sqlite3_errmsg(db)));
        return results;
    }

    sqlite3_bind_text(stmt, 1, notebookGuid.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DBSectionGroupRecord grp;
        grp.guid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        grp.notebookGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* parentText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        grp.parentGroupGuid = parentText ? parentText : "";
        grp.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        grp.sortOrder = sqlite3_column_int(stmt, 4);
        grp.isCollapsed = (sqlite3_column_int(stmt, 5) != 0);
        grp.createdAt = sqlite3_column_int64(stmt, 6);
        grp.updatedAt = sqlite3_column_int64(stmt, 7);
        results.push_back(std::move(grp));
    }

    sqlite3_finalize(stmt);
    return results;
}

/**
 * @brief Upserts a notebook section.
 */
bool DBManager::UpsertSection(const DBSectionRecord& record) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = R"(
        INSERT INTO sections (guid, notebook_guid, group_guid, name, sort_order, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(guid) DO UPDATE SET
            notebook_guid = excluded.notebook_guid,
            group_guid = excluded.group_guid,
            name = excluded.name,
            sort_order = excluded.sort_order,
            updated_at = excluded.updated_at;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare UpsertSection: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    int64_t now = GetCurrentTimestamp();
    sqlite3_bind_text(stmt, 1, record.guid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.notebookGuid.c_str(), -1, SQLITE_TRANSIENT);
    if (!record.groupGuid.empty()) {
        sqlite3_bind_text(stmt, 3, record.groupGuid.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, record.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, record.sortOrder);
    sqlite3_bind_int64(stmt, 6, record.createdAt > 0 ? record.createdAt : now);
    sqlite3_bind_int64(stmt, 7, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Deletes a section (and its child pages via foreign keys).
 */
bool DBManager::DeleteSection(const std::string& sectionGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = "DELETE FROM sections WHERE guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare DeleteSection: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Retrieves all sections belonging to a notebook, ordered by sort_order.
 */
std::vector<DBSectionRecord> DBManager::LoadSections(const std::string& notebookGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<DBSectionRecord> results;
    if (!db) return results;

    const char* sql = "SELECT guid, notebook_guid, group_guid, name, sort_order, created_at, updated_at FROM sections WHERE notebook_guid = ? ORDER BY sort_order ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare LoadSections: " + std::string(sqlite3_errmsg(db)));
        return results;
    }

    sqlite3_bind_text(stmt, 1, notebookGuid.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DBSectionRecord sec;
        sec.guid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sec.notebookGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* grpText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        sec.groupGuid = grpText ? grpText : "";
        sec.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        sec.sortOrder = sqlite3_column_int(stmt, 4);
        sec.createdAt = sqlite3_column_int64(stmt, 5);
        sec.updatedAt = sqlite3_column_int64(stmt, 6);
        results.push_back(std::move(sec));
    }

    sqlite3_finalize(stmt);
    return results;
}

/**
 * @brief Saves page metadata to SQLite.
 */
bool DBManager::SavePageMetadata(const std::string& pageGuid, const std::string& sectionGuid, 
                                 const std::string& title, const std::string& createdDate, 
                                 const std::string& createdTime, int32_t sortOrder,
                                 bool hasBlob, const std::string& parentPageGuid,
                                 int32_t nestingLevel, bool isCollapsed) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = R"(
        INSERT INTO pages (guid, section_guid, title, created_date, created_time, parent_page_guid, nesting_level, sort_order, is_collapsed, has_blob, last_accessed)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(guid) DO UPDATE SET
            section_guid = excluded.section_guid,
            title = excluded.title,
            created_date = excluded.created_date,
            created_time = excluded.created_time,
            parent_page_guid = excluded.parent_page_guid,
            nesting_level = excluded.nesting_level,
            sort_order = excluded.sort_order,
            is_collapsed = excluded.is_collapsed,
            has_blob = excluded.has_blob,
            last_accessed = excluded.last_accessed;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare SavePageMetadata: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    int64_t now = GetCurrentTimestamp();
    sqlite3_bind_text(stmt, 1, pageGuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, createdDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, createdTime.c_str(), -1, SQLITE_TRANSIENT);
    if (!parentPageGuid.empty()) {
        sqlite3_bind_text(stmt, 6, parentPageGuid.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 6);
    }
    sqlite3_bind_int(stmt, 7, nestingLevel);
    sqlite3_bind_int(stmt, 8, sortOrder);
    sqlite3_bind_int(stmt, 9, isCollapsed ? 1 : 0);
    sqlite3_bind_int(stmt, 10, hasBlob ? 1 : 0);
    sqlite3_bind_int64(stmt, 11, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Deletes a page record from SQLite.
 */
bool DBManager::DeletePage(const std::string& pageGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = "DELETE FROM pages WHERE guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare DeletePage: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, pageGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Loads all page metadata records belonging to a section, ordered by sort_order.
 */
std::vector<DBPageRecord> DBManager::LoadPagesMetadata(const std::string& sectionGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<DBPageRecord> results;
    if (!db) return results;

    const char* sql = "SELECT guid, section_guid, title, created_date, created_time, parent_page_guid, nesting_level, sort_order, is_collapsed, has_blob FROM pages WHERE section_guid = ? ORDER BY sort_order ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare LoadPagesMetadata: " + std::string(sqlite3_errmsg(db)));
        return results;
    }

    sqlite3_bind_text(stmt, 1, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DBPageRecord page;
        page.guid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        page.sectionGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        page.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        page.createdDate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        page.createdTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* parentText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        page.parentPageGuid = parentText ? parentText : "";
        page.nestingLevel = sqlite3_column_int(stmt, 6);
        page.sortOrder = sqlite3_column_int(stmt, 7);
        page.isCollapsed = (sqlite3_column_int(stmt, 8) != 0);
        page.hasBlob = (sqlite3_column_int(stmt, 9) != 0);
        results.push_back(std::move(page));
    }

    sqlite3_finalize(stmt);
    return results;
}

/**
 * @brief Checkpoints WAL journal back to disk.
 */
bool DBManager::CheckpointWAL() {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;
    int rc = sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        LOG_WARN(DBManager, "WAL checkpoint returned status: " + std::to_string(rc));
        return false;
    }
    LOG_INFO(DBManager, "WAL checkpoint executed successfully.");
    return true;
}

std::vector<SearchResult> DBManager::SearchContent(const std::string& notebookGuid, 
                                                   const std::string& queryText, 
                                                   const std::string& tagFilter, 
                                                   int limit) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return {};
    return NotebookSearchIndex::Search(db, notebookGuid, queryText, tagFilter, limit);
}

bool DBManager::IndexPageContent(const std::string& notebookGuid, 
                                 const std::string& sectionGuid, 
                                 const CanvasPage& page) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;
    return NotebookSearchIndex::IndexPage(db, notebookGuid, sectionGuid, page);
}

std::vector<TagSummary> DBManager::GetNotebookTags(const std::string& notebookGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return {};
    return NotebookSearchIndex::GetNotebookTags(db, notebookGuid);
}

} // namespace Folio
