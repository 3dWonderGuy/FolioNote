#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <memory>
#include <blend2d/blend2d.h>

#include "core/pdf_engine/pdf_types.hpp"
#include "core/pdf_engine/pdf_text_layer.hpp"
#include "core/pdf_engine/pdf_renderer.hpp"
#include "core/pdf_engine/pdf_document.hpp"
#include "core/pdf_engine/pdf_tile_cache.hpp"
#include "core/pdf_engine/pdf_virtualizer.hpp"

class DocumentSession;

namespace Folio {

/**
 * @struct PdfDocSessionState
 * @brief Retains the active in-memory viewport and decoded rendering cache for a PDF document.
 * When switching between multiple PDF documents, this working set preserves each document's
 * independent zoom scale, horizontal scroll, vertical scroll, and active page index.
 * If a document has not been viewed for longer than 60,000ms, its heavy textures are evicted.
 */
struct PdfDocSessionState {
    std::string filePath;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    float maxScrollX = 0.0f;
    float maxScrollY = 0.0f;
    float zoomScale = 1.25f;
    int activePageIndex = 0;
    uint64_t lastAccessTimeMs = 0;
    bool isLoaded = false;
    std::vector<PdfPageDimension> pageDimensions;
    std::vector<PdfOutlineItem> docOutline;
    bool isOutlineLoaded = false;
    std::vector<PdfUserBookmark> userBookmarks;
    std::unordered_map<int, std::vector<TextHighlightSpan>> docHighlights;
    std::unordered_map<int, CachedPdfViewerPage> pageCache;
    std::unordered_map<int, CachedThumbnail> thumbnailCache;

    void EvictTextures();
    void HomeViewport() noexcept;
};

/**
 * @brief Master PDF Engine coordinating document structure, multi-scale tile caching,
 * continuous page layout virtualization, and universal canvas layer integration.
 */
class PdfEngine {
public:
    PdfDocument document;
    PdfVirtualizer virtualizer;
    PdfTileCache tileCache;

    // Multi-document working set telemetry
    std::unordered_map<std::string, PdfDocSessionState> documentWorkingSet;

    // Background loading telemetry
    bool isLoading = false;
    std::string loadingDocPath;
    std::string loadingDocName;
    std::atomic<int> loadingCurrentPage{0};
    std::atomic<int> loadingTotalPages{0};
    std::atomic<bool> loadingFinished{false};
    std::atomic<bool> loadingFailed{false};
    std::mutex pendingSummaryMutex;
    PdfDocSummary pendingSummary;

    // Tool & Interaction State
    PdfToolMode activeTool = PdfToolMode::Highlight;
    int activeHighlightColorIdx = 0;
    int selectedPageIndex = -1;
    PdfTextSelection currentSelection;
    bool isSelectingText = false;

    PdfEngine();
    ~PdfEngine();

    // Non-copyable
    PdfEngine(const PdfEngine&) = delete;
    PdfEngine& operator=(const PdfEngine&) = delete;

    /**
     * @brief Initiates non-blocking background loading of a PDF document's structure and metadata.
     */
    void LoadDocument(const std::string& filePath);

    /**
     * @brief Updates engine state, checks background loader completion, and executes LRU maintenance.
     */
    void Update(DocumentSession* session = nullptr);

    /**
     * @brief Switches active document, preserving current working set state.
     */
    void SwitchDocument(const std::string& diskPath, DocumentSession* session = nullptr);

    /**
     * @brief Purges caches and closes active document.
     */
    void Clear();

    /**
     * @brief Resolves active highlight color.
     */
    [[nodiscard]] ImU32 GetActiveHighlightColor(bool inverted = false) const noexcept;

    /**
     * @brief Resolves active highlight color swatch.
     */
    [[nodiscard]] ImVec4 GetActiveHighlightSwatch() const noexcept;

    // -------------------------------------------------------------------------
    // UNIVERSAL CANVAS LAYER INTEGRATION
    // -------------------------------------------------------------------------
    /**
     * @brief Renders a single PDF page directly into a Blend2D graphics context.
     * Enables stamping or embedding vector PDF pages into the universal base canvas layer.
     * 
     * @param ctx Blend2D rendering context.
     * @param pageIndex 0-based page index.
     * @param xMm Destination top-left X in physical millimeters.
     * @param yMm Destination top-left Y in physical millimeters.
     * @param scale Physical millimeter scaling factor.
     * @param invert Dark canvas color inversion.
     * @return true on successful blit.
     */
    bool RenderPageToBlend2D(BLContext& ctx, int pageIndex, double xMm, double yMm, double scale = 1.0, bool invert = false);
};

} // namespace Folio
