#include "core/search/notebook_search_index.hpp"
#include "core/objects/text_box.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <regex>
#include <unordered_map>
#include <chrono>

namespace Folio {

/**
 * @brief Initializes the FTS5 full-text search table and spatial metadata coordinates table.
 */
bool NotebookSearchIndex::InitSchema(sqlite3* db) {
    if (!db) return false;

    // 1. Relational content index table (stores spatial coordinates, timestamps, and foreign keys)
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS search_catalog (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            notebook_guid TEXT NOT NULL,
            section_guid TEXT NOT NULL,
            page_guid TEXT NOT NULL,
            object_guid TEXT NOT NULL,
            content_type INTEGER NOT NULL,
            raw_content TEXT NOT NULL,
            tags TEXT NOT NULL,
            min_x REAL NOT NULL DEFAULT 0.0,
            min_y REAL NOT NULL DEFAULT 0.0,
            max_x REAL NOT NULL DEFAULT 0.0,
            max_y REAL NOT NULL DEFAULT 0.0,
            created_at INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_search_page ON search_catalog(page_guid);
        CREATE INDEX IF NOT EXISTS idx_search_section ON search_catalog(section_guid);
        CREATE INDEX IF NOT EXISTS idx_search_notebook ON search_catalog(notebook_guid);
        CREATE INDEX IF NOT EXISTS idx_search_tags ON search_catalog(tags);

        -- FTS5 Full-Text Search Virtual Table
        CREATE VIRTUAL TABLE IF NOT EXISTS search_fts USING fts5(
            raw_content,
            tags,
            content='search_catalog',
            content_rowid='id',
            tokenize='porter unicode61'
        );

        -- Sync Triggers to keep FTS5 synchronized with search_catalog automatically
        CREATE TRIGGER IF NOT EXISTS trg_search_catalog_ai AFTER INSERT ON search_catalog BEGIN
            INSERT INTO search_fts(rowid, raw_content, tags) VALUES (new.id, new.raw_content, new.tags);
        END;

        CREATE TRIGGER IF NOT EXISTS trg_search_catalog_ad AFTER DELETE ON search_catalog BEGIN
            INSERT INTO search_fts(search_fts, rowid, raw_content, tags) VALUES('delete', old.id, old.raw_content, old.tags);
        END;

        CREATE TRIGGER IF NOT EXISTS trg_search_catalog_au AFTER UPDATE ON search_catalog BEGIN
            INSERT INTO search_fts(search_fts, rowid, raw_content, tags) VALUES('delete', old.id, old.raw_content, old.tags);
            INSERT INTO search_fts(rowid, raw_content, tags) VALUES (new.id, new.raw_content, new.tags);
        END;
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db, schema, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        LOG_ERROR(SearchIndex, "Failed to initialize search index schema: " + std::string(err ? err : "Unknown"));
        if (err) sqlite3_free(err);
        return false;
    }

    LOG_INFO(SearchIndex, "Search FTS5 and catalog schema initialized successfully.");
    return true;
}

/**
 * @brief Extracts hashtags matching pattern '#word' or '#todo' from text.
 */
std::vector<std::string> NotebookSearchIndex::ExtractHashtags(const std::string& text) {
    std::vector<std::string> tags;
    static const std::regex tagRegex(R"(#([a-zA-Z0-9_-]+))");
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), tagRegex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        tags.push_back(match.str());
    }
    return tags;
}

/**
 * @brief Inserts a single searchable entry into the search catalog.
 */
bool NotebookSearchIndex::IndexEntry(sqlite3* db, const SearchIndexEntry& entry) {
    if (!db || entry.rawContent.empty()) return false;

    const char* sql = R"(
        INSERT INTO search_catalog (
            notebook_guid, section_guid, page_guid, object_guid, 
            content_type, raw_content, tags, min_x, min_y, max_x, max_y, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(SearchIndex, "Failed to prepare IndexEntry statement: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    sqlite3_bind_text(stmt, 1, entry.notebookGuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.pageGuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.objectGuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, static_cast<int>(entry.contentType));
    sqlite3_bind_text(stmt, 6, entry.rawContent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, entry.tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, entry.spatialBounds.minX);
    sqlite3_bind_double(stmt, 9, entry.spatialBounds.minY);
    sqlite3_bind_double(stmt, 10, entry.spatialBounds.maxX);
    sqlite3_bind_double(stmt, 11, entry.spatialBounds.maxY);
    sqlite3_bind_int64(stmt, 12, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Scans a full CanvasPage and rebuilds its search index entries.
 */
bool NotebookSearchIndex::IndexPage(sqlite3* db, const std::string& notebookGuid, 
                                    const std::string& sectionGuid, const CanvasPage& page) {
    if (!db) return false;

    // 1. Remove prior entries for this page to prevent duplicate search results
    RemovePage(db, page.guid);

    // 2. Index Page Title
    if (!page.title.empty()) {
        SearchIndexEntry titleEntry;
        titleEntry.notebookGuid = notebookGuid;
        titleEntry.sectionGuid = sectionGuid;
        titleEntry.pageGuid = page.guid;
        titleEntry.objectGuid = "";
        titleEntry.contentType = ContentType::PageTitle;
        titleEntry.rawContent = page.title;

        auto titleTags = ExtractHashtags(page.title);
        std::ostringstream tagStream;
        for (const auto& t : titleTags) tagStream << t << " ";
        titleEntry.tags = tagStream.str();
        titleEntry.spatialBounds = AABB(0, 0, 1000, 100);

        IndexEntry(db, titleEntry);
    }

    // 3. Scan Canvas Objects (Text Boxes, Tags, future OCR)
    for (const auto& obj : page.objects) {
        if (!obj) continue;

        if (obj->type == ObjectType::Text) {
            auto txt = std::static_pointer_cast<TextBoxObject>(obj);
            if (!txt->text.empty()) {
                SearchIndexEntry textEntry;
                textEntry.notebookGuid = notebookGuid;
                textEntry.sectionGuid = sectionGuid;
                textEntry.pageGuid = page.guid;
                textEntry.objectGuid = txt->guuid;
                textEntry.contentType = ContentType::TextBox;
                textEntry.rawContent = txt->text;

                auto extracted = ExtractHashtags(txt->text);
                std::ostringstream tagStream;
                for (const auto& t : extracted) tagStream << t << " ";
                textEntry.tags = tagStream.str();
                textEntry.spatialBounds = txt->bounds;

                IndexEntry(db, textEntry);
            }
        }
    }

    LOG_INFO(SearchIndex, "Indexed page content for: '" + page.title + "' (" + page.guid + ")");
    return true;
}

/**
 * @brief Removes all indexed entries associated with a deleted page.
 */
bool NotebookSearchIndex::RemovePage(sqlite3* db, const std::string& pageGuid) {
    if (!db || pageGuid.empty()) return false;

    const char* sql = "DELETE FROM search_catalog WHERE page_guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, pageGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Removes all indexed entries associated with a deleted section.
 */
bool NotebookSearchIndex::RemoveSection(sqlite3* db, const std::string& sectionGuid) {
    if (!db || sectionGuid.empty()) return false;

    const char* sql = "DELETE FROM search_catalog WHERE section_guid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sectionGuid.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

/**
 * @brief Performs ranked full-text search with spatial coordinates and snippet extraction.
 */
std::vector<SearchResult> NotebookSearchIndex::Search(sqlite3* db, 
                                                      const std::string& notebookGuid, 
                                                      const std::string& queryText, 
                                                      const std::string& tagFilter, 
                                                      int limit) {
    std::vector<SearchResult> results;
    if (!db || (queryText.empty() && tagFilter.empty())) return results;

    std::string sql;
    sqlite3_stmt* stmt = nullptr;

    if (!queryText.empty()) {
        // FTS5 MATCH query with BM25 ranking and snippet generation
        sql = R"(
            SELECT 
                c.notebook_guid, c.section_guid, s.name as section_name,
                c.page_guid, p.title as page_title, c.object_guid,
                c.content_type, snippet(search_fts, 0, '<b>', '</b>', '...', 12) as snippet_text,
                c.raw_content, c.tags,
                c.min_x, c.min_y, c.max_x, c.max_y,
                bm25(search_fts) as rank
            FROM search_fts
            JOIN search_catalog c ON c.id = search_fts.rowid
            LEFT JOIN sections s ON s.guid = c.section_guid
            LEFT JOIN pages p ON p.guid = c.page_guid
            WHERE search_fts MATCH ?
              AND c.notebook_guid = ?
            ORDER BY rank
            LIMIT ?;
        )";

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_ERROR(SearchIndex, "Failed to prepare FTS search query: " + std::string(sqlite3_errmsg(db)));
            return results;
        }

        // Format query for prefix search (e.g. "calc" -> "calc*")
        std::string formattedQuery = queryText;
        if (!formattedQuery.empty() && formattedQuery.back() != '*' && formattedQuery.find(' ') == std::string::npos) {
            formattedQuery += "*";
        }

        sqlite3_bind_text(stmt, 1, formattedQuery.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, notebookGuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);

    } else {
        // Tag-only lookup
        sql = R"(
            SELECT 
                c.notebook_guid, c.section_guid, s.name as section_name,
                c.page_guid, p.title as page_title, c.object_guid,
                c.content_type, c.raw_content as snippet_text,
                c.raw_content, c.tags,
                c.min_x, c.min_y, c.max_x, c.max_y,
                0.0 as rank
            FROM search_catalog c
            LEFT JOIN sections s ON s.guid = c.section_guid
            LEFT JOIN pages p ON p.guid = c.page_guid
            WHERE c.tags LIKE ?
              AND c.notebook_guid = ?
            LIMIT ?;
        )";

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_ERROR(SearchIndex, "Failed to prepare tag search query: " + std::string(sqlite3_errmsg(db)));
            return results;
        }

        std::string likeTag = "%" + tagFilter + "%";
        sqlite3_bind_text(stmt, 1, likeTag.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, notebookGuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SearchResult res;
        res.notebookGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        res.sectionGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* secName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        res.sectionName = secName ? secName : "Untitled Section";
        res.pageGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* pTitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        res.pageTitle = pTitle ? pTitle : "Untitled Page";
        res.objectGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        res.contentType = static_cast<ContentType>(sqlite3_column_int(stmt, 6));
        
        const char* snip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        res.snippet = snip ? snip : "";
        const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        res.rawContent = raw ? raw : "";
        const char* tg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        res.tags = tg ? tg : "";

        res.spatialBounds.minX = sqlite3_column_double(stmt, 10);
        res.spatialBounds.minY = sqlite3_column_double(stmt, 11);
        res.spatialBounds.maxX = sqlite3_column_double(stmt, 12);
        res.spatialBounds.maxY = sqlite3_column_double(stmt, 13);
        res.rankScore = sqlite3_column_double(stmt, 14);

        results.push_back(std::move(res));
    }

    sqlite3_finalize(stmt);
    LOG_INFO(SearchIndex, "Search query '" + queryText + "' returned " + std::to_string(results.size()) + " matches.");
    return results;
}

/**
 * @brief Returns tag summary aggregation for tag clouds in the sidebar.
 */
std::vector<TagSummary> NotebookSearchIndex::GetNotebookTags(sqlite3* db, const std::string& notebookGuid) {
    std::vector<TagSummary> summaries;
    if (!db) return summaries;

    const char* sql = R"(
        SELECT tags, page_guid FROM search_catalog 
        WHERE notebook_guid = ? AND tags != '';
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return summaries;

    sqlite3_bind_text(stmt, 1, notebookGuid.c_str(), -1, SQLITE_TRANSIENT);

    std::unordered_map<std::string, TagSummary> tagMap;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string tagsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string pageGuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        std::istringstream iss(tagsStr);
        std::string tag;
        while (iss >> tag) {
            if (tag.empty()) continue;
            auto& summary = tagMap[tag];
            summary.tag = tag;
            summary.count++;
            if (std::find(summary.pageGuids.begin(), summary.pageGuids.end(), pageGuid) == summary.pageGuids.end()) {
                summary.pageGuids.push_back(pageGuid);
            }
        }
    }

    sqlite3_finalize(stmt);

    summaries.reserve(tagMap.size());
    for (auto& pair : tagMap) {
        summaries.push_back(std::move(pair.second));
    }
    return summaries;
}

} // namespace Folio
