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
            color_r REAL NOT NULL DEFAULT 0.55,
            color_g REAL NOT NULL DEFAULT 0.58,
            color_b REAL NOT NULL DEFAULT 0.62,
            color_a REAL NOT NULL DEFAULT 1.0,
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
            color_r REAL NOT NULL DEFAULT 0.90,
            color_g REAL NOT NULL DEFAULT 0.42,
            color_b REAL NOT NULL DEFAULT 0.17,
            color_a REAL NOT NULL DEFAULT 1.0,
            sort_order INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            deleted_at INTEGER DEFAULT NULL,
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
            is_dedicated_pdf INTEGER NOT NULL DEFAULT 0,
            dedicated_pdf_path TEXT,
            dedicated_pdf_bookmarks TEXT,
            dedicated_pdf_highlights TEXT,
            deleted_at INTEGER DEFAULT NULL,
            FOREIGN KEY (section_guid) REFERENCES sections(guid) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_section_groups ON section_groups(notebook_guid, sort_order);
        CREATE INDEX IF NOT EXISTS idx_sections_notebook ON sections(notebook_guid, sort_order);
        CREATE INDEX IF NOT EXISTS idx_pages_section ON pages(section_guid, sort_order);
        CREATE INDEX IF NOT EXISTS idx_sections_deleted ON sections(notebook_guid, deleted_at);
        CREATE INDEX IF NOT EXISTS idx_pages_deleted ON pages(section_guid, deleted_at);
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db, schema, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        LOG_ERROR(DBManager, "Schema creation error: " + std::string(err ? err : "Unknown"));
        if (err) sqlite3_free(err);
        return false;
    }

    // Non-destructive migrations for existing database files
    sqlite3_exec(db, "ALTER TABLE section_groups ADD COLUMN color_r REAL NOT NULL DEFAULT 0.55;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE section_groups ADD COLUMN color_g REAL NOT NULL DEFAULT 0.58;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE section_groups ADD COLUMN color_b REAL NOT NULL DEFAULT 0.62;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE section_groups ADD COLUMN color_a REAL NOT NULL DEFAULT 1.0;", nullptr, nullptr, nullptr);

    sqlite3_exec(db, "ALTER TABLE sections ADD COLUMN color_r REAL NOT NULL DEFAULT 0.90;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE sections ADD COLUMN color_g REAL NOT NULL DEFAULT 0.42;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE sections ADD COLUMN color_b REAL NOT NULL DEFAULT 0.17;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE sections ADD COLUMN color_a REAL NOT NULL DEFAULT 1.0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE sections ADD COLUMN group_guid TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE sections ADD COLUMN deleted_at INTEGER DEFAULT NULL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN parent_page_guid TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN nesting_level INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN is_collapsed INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN is_dedicated_pdf INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN dedicated_pdf_path TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN dedicated_pdf_bookmarks TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN dedicated_pdf_highlights TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE pages ADD COLUMN deleted_at INTEGER DEFAULT NULL;", nullptr, nullptr, nullptr);

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
        INSERT INTO section_groups (guid, notebook_guid, parent_group_guid, name, color_r, color_g, color_b, color_a, sort_order, is_collapsed, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(guid) DO UPDATE SET
            notebook_guid = excluded.notebook_guid,
            parent_group_guid = excluded.parent_group_guid,
            name = excluded.name,
            color_r = excluded.color_r,
            color_g = excluded.color_g,
            color_b = excluded.color_b,
            color_a = excluded.color_a,
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
    sqlite3_bind_double(stmt, 5, record.colorR);
    sqlite3_bind_double(stmt, 6, record.colorG);
    sqlite3_bind_double(stmt, 7, record.colorB);
    sqlite3_bind_double(stmt, 8, record.colorA);
    sqlite3_bind_int(stmt, 9, record.sortOrder);
    sqlite3_bind_int(stmt, 10, record.isCollapsed ? 1 : 0);
    sqlite3_bind_int64(stmt, 11, record.createdAt > 0 ? record.createdAt : now);
    sqlite3_bind_int64(stmt, 12, now);

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

    const char* sql = "SELECT guid, notebook_guid, parent_group_guid, name, color_r, color_g, color_b, color_a, sort_order, is_collapsed, created_at, updated_at FROM section_groups WHERE notebook_guid = ? ORDER BY sort_order ASC;";
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
        grp.colorR = static_cast<float>(sqlite3_column_double(stmt, 4));
        grp.colorG = static_cast<float>(sqlite3_column_double(stmt, 5));
        grp.colorB = static_cast<float>(sqlite3_column_double(stmt, 6));
        grp.colorA = static_cast<float>(sqlite3_column_double(stmt, 7));
        grp.sortOrder = sqlite3_column_int(stmt, 8);
        grp.isCollapsed = (sqlite3_column_int(stmt, 9) != 0);
        grp.createdAt = sqlite3_column_int64(stmt, 10);
        grp.updatedAt = sqlite3_column_int64(stmt, 11);
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
        INSERT INTO sections (guid, notebook_guid, group_guid, name, color_r, color_g, color_b, color_a, sort_order, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(guid) DO UPDATE SET
            notebook_guid = excluded.notebook_guid,
            group_guid = excluded.group_guid,
            name = excluded.name,
            color_r = excluded.color_r,
            color_g = excluded.color_g,
            color_b = excluded.color_b,
            color_a = excluded.color_a,
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
    sqlite3_bind_double(stmt, 5, record.colorR);
    sqlite3_bind_double(stmt, 6, record.colorG);
    sqlite3_bind_double(stmt, 7, record.colorB);
    sqlite3_bind_double(stmt, 8, record.colorA);
    sqlite3_bind_int(stmt, 9, record.sortOrder);
    sqlite3_bind_int64(stmt, 10, record.createdAt > 0 ? record.createdAt : now);
    sqlite3_bind_int64(stmt, 11, now);

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

    const char* sql = "SELECT guid, notebook_guid, group_guid, name, color_r, color_g, color_b, color_a, sort_order, created_at, updated_at, deleted_at FROM sections WHERE notebook_guid = ? AND (deleted_at IS NULL OR deleted_at = 0) ORDER BY sort_order ASC;";
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
        sec.colorR = static_cast<float>(sqlite3_column_double(stmt, 4));
        sec.colorG = static_cast<float>(sqlite3_column_double(stmt, 5));
        sec.colorB = static_cast<float>(sqlite3_column_double(stmt, 6));
        sec.colorA = static_cast<float>(sqlite3_column_double(stmt, 7));
        sec.sortOrder = sqlite3_column_int(stmt, 8);
        sec.createdAt = sqlite3_column_int64(stmt, 9);
        sec.updatedAt = sqlite3_column_int64(stmt, 10);
        sec.deletedAt = sqlite3_column_int64(stmt, 11);
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
                                 int32_t nestingLevel, bool isCollapsed,
                                 bool isDedicatedPdf, const std::string& dedicatedPdfPath,
                                 const std::string& dedicatedPdfBookmarks,
                                 const std::string& dedicatedPdfHighlights) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = R"(
        INSERT INTO pages (guid, section_guid, title, created_date, created_time, parent_page_guid, nesting_level, sort_order, is_collapsed, has_blob, last_accessed, is_dedicated_pdf, dedicated_pdf_path, dedicated_pdf_bookmarks, dedicated_pdf_highlights)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
            last_accessed = excluded.last_accessed,
            is_dedicated_pdf = excluded.is_dedicated_pdf,
            dedicated_pdf_path = excluded.dedicated_pdf_path,
            dedicated_pdf_bookmarks = excluded.dedicated_pdf_bookmarks,
            dedicated_pdf_highlights = excluded.dedicated_pdf_highlights;
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
    sqlite3_bind_int(stmt, 12, isDedicatedPdf ? 1 : 0);
    if (!dedicatedPdfPath.empty()) {
        sqlite3_bind_text(stmt, 13, dedicatedPdfPath.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 13);
    }
    if (!dedicatedPdfBookmarks.empty()) {
        sqlite3_bind_text(stmt, 14, dedicatedPdfBookmarks.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 14);
    }
    if (!dedicatedPdfHighlights.empty()) {
        sqlite3_bind_text(stmt, 15, dedicatedPdfHighlights.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 15);
    }

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

    const char* sql = "SELECT guid, section_guid, title, created_date, created_time, parent_page_guid, nesting_level, sort_order, is_collapsed, has_blob, is_dedicated_pdf, dedicated_pdf_path, dedicated_pdf_bookmarks, dedicated_pdf_highlights, deleted_at FROM pages WHERE section_guid = ? AND (deleted_at IS NULL OR deleted_at = 0) ORDER BY sort_order ASC;";
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
        page.isDedicatedPdf = (sqlite3_column_int(stmt, 10) != 0);
        const auto* pdfPathText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        page.dedicatedPdfPath = pdfPathText ? pdfPathText : "";
        const auto* pdfBkText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
        page.dedicatedPdfBookmarks = pdfBkText ? pdfBkText : "";
        const auto* pdfHlText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
        page.dedicatedPdfHighlights = pdfHlText ? pdfHlText : "";
        page.deletedAt = sqlite3_column_int64(stmt, 14);
        results.push_back(std::move(page));
    }

    sqlite3_finalize(stmt);
    return results;
}

/**
 * @brief Updates only the sort_order column for a page identified by its GUID.
 *
 * This is a lightweight alternative to a full SavePageAsync when the only thing
 * that changed is the page's position in its section (drag-and-drop reorder or
 * Move Up/Down in the nav panel context menu).
 *
 * SQL:  UPDATE pages SET sort_order = ?, updated_at = ? WHERE guid = ?
 *
 * @param pageGuid  Unique identifier of the page to update.
 * @param sortOrder New 0-indexed sort position within the section.
 * @return true if the UPDATE executed without error.
 */
bool DBManager::UpdatePageSortOrder(const std::string& pageGuid, int32_t sortOrder) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    // pages table uses last_accessed (INTEGER epoch ms), not updated_at
    const char* sql = "UPDATE pages SET sort_order = ?, last_accessed = ? WHERE guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare UpdatePageSortOrder: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    sqlite3_bind_int(stmt,   1, sortOrder);
    sqlite3_bind_int64(stmt, 2, GetCurrentTimestamp());
    sqlite3_bind_text(stmt,  3, pageGuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
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

// =========================================================================
// RECYCLE BIN & SOFT DELETE IMPLEMENTATIONS
// =========================================================================

/**
 * @brief Soft-deletes a page by stamping its deleted_at timestamp in SQLite.
 * @param pageGuid Unique persistent GUID of the page.
 * @return true on success, false on error.
 */
bool DBManager::SoftDeletePage(const std::string& pageGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = "UPDATE pages SET deleted_at = ? WHERE guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare SoftDeletePage: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, GetCurrentTimestamp());
    sqlite3_bind_text(stmt, 2, pageGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Restores a soft-deleted page by resetting deleted_at to NULL.
 * @param pageGuid Unique persistent GUID of the page.
 * @return true on success, false on error.
 */
bool DBManager::RestorePage(const std::string& pageGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    const char* sql = "UPDATE pages SET deleted_at = NULL WHERE guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare RestorePage: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, pageGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Soft-deletes a section and cascades soft-deletion to all its child pages.
 * @param sectionGuid Unique persistent GUID of the section.
 * @return true on success, false on error.
 */
bool DBManager::SoftDeleteSection(const std::string& sectionGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    int64_t now = GetCurrentTimestamp();

    // 1. Soft-delete section record
    const char* sqlSec = "UPDATE sections SET deleted_at = ? WHERE guid = ?;";
    sqlite3_stmt* stmtSec = nullptr;
    if (sqlite3_prepare_v2(db, sqlSec, -1, &stmtSec, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare SoftDeleteSection (sec): " + std::string(sqlite3_errmsg(db)));
        return false;
    }
    sqlite3_bind_int64(stmtSec, 1, now);
    sqlite3_bind_text(stmtSec, 2, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rcSec = sqlite3_step(stmtSec);
    sqlite3_finalize(stmtSec);

    // 2. Cascade soft-deletion to all child pages in this section
    const char* sqlPages = "UPDATE pages SET deleted_at = ? WHERE section_guid = ?;";
    sqlite3_stmt* stmtPages = nullptr;
    if (sqlite3_prepare_v2(db, sqlPages, -1, &stmtPages, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmtPages, 1, now);
        sqlite3_bind_text(stmtPages, 2, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmtPages);
        sqlite3_finalize(stmtPages);
    }

    return (rcSec == SQLITE_DONE);
}

/**
 * @brief Restores a soft-deleted section and all its contained child pages.
 * @param sectionGuid Unique persistent GUID of the section.
 * @return true on success, false on error.
 */
bool DBManager::RestoreSection(const std::string& sectionGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return false;

    // 1. Restore section record
    const char* sqlSec = "UPDATE sections SET deleted_at = NULL WHERE guid = ?;";
    sqlite3_stmt* stmtSec = nullptr;
    if (sqlite3_prepare_v2(db, sqlSec, -1, &stmtSec, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare RestoreSection: " + std::string(sqlite3_errmsg(db)));
        return false;
    }
    sqlite3_bind_text(stmtSec, 1, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rcSec = sqlite3_step(stmtSec);
    sqlite3_finalize(stmtSec);

    // 2. Cascade restoration to all child pages
    const char* sqlPages = "UPDATE pages SET deleted_at = NULL WHERE section_guid = ?;";
    sqlite3_stmt* stmtPages = nullptr;
    if (sqlite3_prepare_v2(db, sqlPages, -1, &stmtPages, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmtPages, 1, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmtPages);
        sqlite3_finalize(stmtPages);
    }

    return (rcSec == SQLITE_DONE);
}

/**
 * @brief Retrieves all soft-deleted sections belonging to a notebook.
 */
std::vector<DBSectionRecord> DBManager::LoadDeletedSections(const std::string& notebookGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<DBSectionRecord> results;
    if (!db) return results;

    const char* sql = "SELECT guid, notebook_guid, group_guid, name, color_r, color_g, color_b, color_a, sort_order, created_at, updated_at, deleted_at FROM sections WHERE notebook_guid = ? AND deleted_at > 0 ORDER BY deleted_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare LoadDeletedSections: " + std::string(sqlite3_errmsg(db)));
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
        sec.colorR = static_cast<float>(sqlite3_column_double(stmt, 4));
        sec.colorG = static_cast<float>(sqlite3_column_double(stmt, 5));
        sec.colorB = static_cast<float>(sqlite3_column_double(stmt, 6));
        sec.colorA = static_cast<float>(sqlite3_column_double(stmt, 7));
        sec.sortOrder = sqlite3_column_int(stmt, 8);
        sec.createdAt = sqlite3_column_int64(stmt, 9);
        sec.updatedAt = sqlite3_column_int64(stmt, 10);
        sec.deletedAt = sqlite3_column_int64(stmt, 11);
        results.push_back(std::move(sec));
    }

    sqlite3_finalize(stmt);
    return results;
}

/**
 * @brief Retrieves all soft-deleted pages belonging to a notebook.
 */
std::vector<DBPageRecord> DBManager::LoadDeletedPages(const std::string& notebookGuid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<DBPageRecord> results;
    if (!db) return results;

    const char* sql = R"(
        SELECT p.guid, p.section_guid, p.title, p.created_date, p.created_time, 
               p.parent_page_guid, p.nesting_level, p.sort_order, p.is_collapsed, 
               p.has_blob, p.is_dedicated_pdf, p.dedicated_pdf_path, 
               p.dedicated_pdf_bookmarks, p.dedicated_pdf_highlights, p.deleted_at 
        FROM pages p 
        JOIN sections s ON p.section_guid = s.guid 
        WHERE s.notebook_guid = ? AND p.deleted_at > 0 
        ORDER BY p.deleted_at DESC;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(DBManager, "Failed to prepare LoadDeletedPages: " + std::string(sqlite3_errmsg(db)));
        return results;
    }

    sqlite3_bind_text(stmt, 1, notebookGuid.c_str(), -1, SQLITE_TRANSIENT);

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
        page.isDedicatedPdf = (sqlite3_column_int(stmt, 10) != 0);
        const auto* pdfPathText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        page.dedicatedPdfPath = pdfPathText ? pdfPathText : "";
        const auto* pdfBkText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
        page.dedicatedPdfBookmarks = pdfBkText ? pdfBkText : "";
        const auto* pdfHlText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
        page.dedicatedPdfHighlights = pdfHlText ? pdfHlText : "";
        page.deletedAt = sqlite3_column_int64(stmt, 14);
        results.push_back(std::move(page));
    }

    sqlite3_finalize(stmt);
    return results;
}

/**
 * @brief Permanently purges a page record from SQLite and unlinks its .ink file on disk.
 */
bool DBManager::PermanentlyDeletePage(const std::string& pageGuid, const std::string& pkgPath) {
    std::string rootPkg = pkgPath.empty() ? std::filesystem::path(currentDbPath).parent_path().string() : pkgPath;
    if (!rootPkg.empty()) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(rootPkg) / "pages" / (pageGuid + ".ink"), ec);
    }
    return DeletePage(pageGuid);
}

/**
 * @brief Permanently purges a section record, all child pages, and their .ink files.
 */
bool DBManager::PermanentlyDeleteSection(const std::string& sectionGuid, const std::string& pkgPath) {
    std::string rootPkg = pkgPath.empty() ? std::filesystem::path(currentDbPath).parent_path().string() : pkgPath;
    
    // Gather all child pages to unlink .ink files from disk
    auto pages = LoadPagesMetadata(sectionGuid);
    for (const auto& pg : pages) {
        if (!rootPkg.empty()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(rootPkg) / "pages" / (pg.guid + ".ink"), ec);
        }
    }

    // Cascade delete pages in SQLite
    {
        std::lock_guard<std::mutex> lock(dbMutex);
        if (!db) return false;
        const char* sqlPages = "DELETE FROM pages WHERE section_guid = ?;";
        sqlite3_stmt* stmtP = nullptr;
        if (sqlite3_prepare_v2(db, sqlPages, -1, &stmtP, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmtP, 1, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmtP);
            sqlite3_finalize(stmtP);
        }
    }
    return DeleteSection(sectionGuid);
}

/**
 * @brief Empties all soft-deleted pages and sections from this notebook package.
 */
size_t DBManager::EmptyRecycleBin(const std::string& notebookGuid, const std::string& pkgPath) {
    size_t count = 0;
    auto delPages = LoadDeletedPages(notebookGuid);
    for (const auto& pg : delPages) {
        if (PermanentlyDeletePage(pg.guid, pkgPath)) {
            count++;
        }
    }
    auto delSecs = LoadDeletedSections(notebookGuid);
    for (const auto& sec : delSecs) {
        if (PermanentlyDeleteSection(sec.guid, pkgPath)) {
            count++;
        }
    }
    LOG_INFO(DBManager, "Emptied recycle bin: purged " + std::to_string(count) + " items.");
    return count;
}

/**
 * @brief Automatically purges items in the recycle bin older than the retention window (e.g. 30 days).
 */
size_t DBManager::PurgeExpiredRecycleBinItems(const std::string& notebookGuid, const std::string& pkgPath, int64_t maxAgeSeconds) {
    int64_t cutoff = GetCurrentTimestamp() - maxAgeSeconds;
    size_t count = 0;
    auto delPages = LoadDeletedPages(notebookGuid);
    for (const auto& pg : delPages) {
        if (pg.deletedAt > 0 && pg.deletedAt < cutoff) {
            if (PermanentlyDeletePage(pg.guid, pkgPath)) {
                count++;
            }
        }
    }
    auto delSecs = LoadDeletedSections(notebookGuid);
    for (const auto& sec : delSecs) {
        if (sec.deletedAt > 0 && sec.deletedAt < cutoff) {
            if (PermanentlyDeleteSection(sec.guid, pkgPath)) {
                count++;
            }
        }
    }
    if (count > 0) {
        LOG_INFO(DBManager, "Purged " + std::to_string(count) + " expired recycle bin items older than " + std::to_string(maxAgeSeconds / 86400) + " days.");
    }
    return count;
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
