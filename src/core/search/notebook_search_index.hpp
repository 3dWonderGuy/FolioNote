#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <sqlite3.h>
#include "core/spatial/aabb.hpp"
#include "core/document/canvas_page.hpp"

namespace Folio {

/**
 * =========================================================================================
 * @file notebook_search_index.hpp
 * @brief High-Performance Full-Text Search (FTS5) & Content Indexing Engine for FolioNote
 * =========================================================================================
 * 
 * --- ARCHITECTURE OVERVIEW ---
 * Rather than decompressing and scanning hundreds of separate `.ink` files across disk when
 * searching, the NotebookSearchIndex maintains an inverted FTS5 index and metadata catalog
 * inside `notebook.db`.
 * 
 * Key Capabilities:
 *  1. Unified Universal Indexing:
 *     - Page titles and section names
 *     - Text boxes and sticky notes
 *     - Tags, hashtags (#todo, #important), bookmarks, and color categories
 *     - Future AI extensions: OCR handwriting transcripts & LaTeX math equations
 * 
 *  2. Precise Spatial Locality:
 *     - Every indexed entry stores its World-Space bounding box (AABB).
 *     - Clicking a search result in the UI allows the viewport to immediately pan, zoom,
 *       and highlight the exact word, stroke cluster, or tag on the canvas.
 * 
 *  3. Sub-Millisecond SQLite FTS5 Queries:
 *     - Full BM25 relevance ranking.
 *     - Prefix matching (e.g., "calc*" matches "calculus" and "calculator").
 *     - Automatic snippet generation with highlighted match terms (`<b>term</b>`).
 */

/**
 * @brief Categorization of indexed canvas data types.
 */
enum class ContentType : uint8_t {
    PageTitle        = 1,  ///< Title of a canvas page
    SectionTitle     = 2,  ///< Name of a notebook section
    TextBox          = 3,  ///< User-entered rich text box
    Tag              = 4,  ///< Explicit hashtag or category marker (#todo, #math, #exam)
    OCRHandwriting   = 5,  ///< Future recognized handwriting from ink strokes
    MathFormula      = 6,  ///< Future recognized LaTeX / MathML expression
    AudioTranscript  = 7   ///< Future audio note timestamp transcript
};

/**
 * @brief Converts ContentType to human-readable string.
 */
inline const char* ContentTypeToString(ContentType type) {
    switch (type) {
        case ContentType::PageTitle:       return "Page";
        case ContentType::SectionTitle:    return "Section";
        case ContentType::TextBox:         return "Text";
        case ContentType::Tag:             return "Tag";
        case ContentType::OCRHandwriting:  return "Handwriting";
        case ContentType::MathFormula:     return "Math";
        case ContentType::AudioTranscript: return "Audio";
        default:                           return "Generic";
    }
}

/**
 * @brief Single searchable content item to be inserted into the search index.
 */
struct SearchIndexEntry {
    std::string notebookGuid;       ///< Owning notebook GUID
    std::string sectionGuid;        ///< Owning section GUID
    std::string pageGuid;           ///< Owning page GUID
    std::string objectGuid;         ///< Unique CanvasObject GUID (or empty for page/section titles)
    ContentType contentType = ContentType::TextBox;
    std::string rawContent;         ///< Searchable plain text content
    std::string tags;               ///< Space-separated tags (e.g. "#todo #urgent")
    AABB spatialBounds;             ///< World-space coordinates for direct viewport targeting
};

/**
 * @brief Search result returned by the query engine with snippet and spatial location.
 */
struct SearchResult {
    std::string notebookGuid;       ///< Target notebook GUID
    std::string sectionGuid;        ///< Target section GUID
    std::string sectionName;        ///< Section display title
    std::string pageGuid;           ///< Target page GUID
    std::string pageTitle;          ///< Page display title
    std::string objectGuid;         ///< Target object GUID
    ContentType contentType = ContentType::TextBox;
    std::string snippet;            ///< Formatted snippet with match highlights (<b>match</b>)
    std::string rawContent;         ///< Full matched content string
    std::string tags;               ///< Associated tags
    AABB spatialBounds;             ///< World-space bounding box for viewport centering
    double rankScore = 0.0;         ///< FTS5 BM25 relevance score
};

/**
 * @brief Aggregated tag summary for fast tag cloud navigation.
 */
struct TagSummary {
    std::string tag;                ///< Tag name (e.g. "#todo")
    uint32_t count = 0;             ///< Number of occurrences in the notebook
    std::vector<std::string> pageGuids; ///< Unique pages containing this tag
};

/**
 * @brief Search and content index manager operating on SQLite database tables.
 */
class NotebookSearchIndex {
public:
    /**
     * @brief Creates the FTS5 virtual tables and relational coordinate indexes.
     */
    static bool InitSchema(sqlite3* db);

    /**
     * @brief Inserts or replaces a searchable item in the index.
     */
    static bool IndexEntry(sqlite3* db, const SearchIndexEntry& entry);

    /**
     * @brief Automatically scans a CanvasPage (titles, text boxes, and hashtags) and batch-indexes it.
     */
    static bool IndexPage(sqlite3* db, const std::string& notebookGuid, 
                          const std::string& sectionGuid, const CanvasPage& page);

    /**
     * @brief Removes all indexed entries associated with a deleted page.
     */
    static bool RemovePage(sqlite3* db, const std::string& pageGuid);

    /**
     * @brief Removes all indexed entries associated with a deleted section.
     */
    static bool RemoveSection(sqlite3* db, const std::string& sectionGuid);

    /**
     * @brief Performs a full-text search across the notebook with optional tag filtering and BM25 ranking.
     * @param db Active SQLite connection handle.
     * @param notebookGuid Owning notebook GUID to scope the search.
     * @param queryText Search phrase or prefix (e.g. "meeting", "calculus*", "Euler").
     * @param tagFilter Optional tag filter (e.g. "#todo").
     * @param limit Maximum number of results to return.
     * @return List of ranked search matches with snippets and spatial coordinates.
     */
    static std::vector<SearchResult> Search(sqlite3* db, 
                                            const std::string& notebookGuid, 
                                            const std::string& queryText, 
                                            const std::string& tagFilter = "", 
                                            int limit = 50);

    /**
     * @brief Retrieves all unique tags and occurrence counts across the entire notebook.
     */
    static std::vector<TagSummary> GetNotebookTags(sqlite3* db, const std::string& notebookGuid);

    /**
     * @brief Extracts hashtags (e.g. "#todo", "#exam", "#important") from a text buffer.
     */
    static std::vector<std::string> ExtractHashtags(const std::string& text);
};

} // namespace Folio
