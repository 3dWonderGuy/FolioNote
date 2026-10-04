#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "core/pdf_engine/pdf_types.hpp"

class DocumentSession;

namespace Folio {

/**
 * @brief Manages the high-level semantic data of a PDF document:
 * page count, physical dimensions, outline bookmarks, user bookmarks, and text highlights.
 */
class PdfDocument {
public:
    std::string filePath;
    int totalPages = 0;
    std::vector<PdfPageDimension> pageDimensions;
    std::vector<PdfOutlineItem> docOutline;
    bool isOutlineLoaded = false;
    std::vector<PdfUserBookmark> userBookmarks;
    std::string lastSyncedBookmarks;
    std::unordered_map<int, std::vector<TextHighlightSpan>> docHighlights;
    std::string lastSyncedHighlights;

    PdfDocument() = default;

    void Clear();

    /**
     * @brief Ingests summary parsed from single-pass structure inspection.
     */
    void SetStructure(PdfDocSummary&& summary);

    /**
     * @brief Deserializes tab-separated bookmarks string into userBookmarks.
     */
    void LoadBookmarksFromString(const std::string& data);

    /**
     * @brief Serializes userBookmarks into a compact tab-separated string.
     */
    [[nodiscard]] std::string SaveBookmarksToString() const;

    /**
     * @brief Flushes bookmarks to the active CanvasPage metadata and commits async to SQLite.
     */
    void SyncBookmarksToPage(DocumentSession& session);

    /**
     * @brief Deserializes tab-separated text highlight spans into docHighlights.
     */
    void LoadHighlightsFromString(const std::string& data);

    /**
     * @brief Serializes docHighlights into a compact tab-separated string.
     */
    [[nodiscard]] std::string SaveHighlightsToString() const;

    /**
     * @brief Flushes highlights to the active CanvasPage metadata and commits async to SQLite.
     */
    void SyncHighlightsToPage(DocumentSession& session);
};

} // namespace Folio
