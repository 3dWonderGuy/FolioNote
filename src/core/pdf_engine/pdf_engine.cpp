#include "core/pdf_engine/pdf_engine.hpp"
#include "core/document/document_session.hpp"
#include "core/document/canvas_page.hpp"
#include "utils/logger.hpp"
#include "io/file_manager.hpp"
#include "utils/thread_pool.hpp"

#include <SDL3/SDL.h>
#include <algorithm>

namespace Folio {

void PdfDocSessionState::EvictTextures() {
    for (auto& [idx, p] : pageCache) p.DestroyTexture();
    pageCache.clear();
    for (auto& [idx, t] : thumbnailCache) t.DestroyTexture();
    thumbnailCache.clear();
}

void PdfDocSessionState::HomeViewport() noexcept {
    scrollX = 0.0f;
    scrollY = 0.0f;
    zoomScale = 1.25f;
    activePageIndex = 0;
}

PdfEngine::PdfEngine() = default;

PdfEngine::~PdfEngine() {
    Clear();
    for (auto& [path, state] : documentWorkingSet) {
        state.EvictTextures();
    }
    documentWorkingSet.clear();
}

void PdfEngine::Clear() {
    tileCache.EvictAll();
    document.Clear();
    virtualizer.ResetViewport();
    selectedPageIndex = -1;
    currentSelection.Clear();
    isSelectingText = false;
}

void PdfEngine::LoadDocument(const std::string& filePath) {
    if (filePath.empty()) return;
    if (document.filePath == filePath && (document.totalPages > 0 || isLoading)) return;

    Clear();
    document.filePath = filePath;
    virtualizer.ResetViewport();
    selectedPageIndex = -1;
    currentSelection.Clear();

    loadingDocPath = filePath;
    loadingDocName = FileManager::GetFileName(filePath);
    isLoading = true;
    loadingFinished.store(false);
    loadingFailed.store(false);
    loadingCurrentPage.store(0);
    loadingTotalPages.store(0);

    LOG_INFO(PdfStorage, "PdfEngine starting background structure load: " + filePath);

    GetGlobalThreadPool().Enqueue([this, filePath]() {
        PdfDocSummary summary;
        bool ok = PdfRenderer::InspectAndLoadDocStructure(filePath, summary, [this](int cur, int tot) {
            loadingCurrentPage.store(cur);
            loadingTotalPages.store(tot);
        });

        if (ok && summary.pageCount > 0) {
            std::lock_guard<std::mutex> lock(pendingSummaryMutex);
            pendingSummary = std::move(summary);
            loadingFinished.store(true);
        } else {
            loadingFailed.store(true);
            LOG_ERROR(PdfStorage, "PdfEngine background structure load failed for '" + filePath + 
                      "': " + (ok ? "document reported 0 pages" : "InspectAndLoadDocStructure failed (PDFium missing or document unreadable)"));
        }
    });
}

void PdfEngine::Update(DocumentSession* session) {
    // 1. Check if background loading thread finished
    if (isLoading && loadingFinished.load()) {
        isLoading = false;
        std::lock_guard<std::mutex> lock(pendingSummaryMutex);
        document.SetStructure(std::move(pendingSummary));

        if (session) {
            auto activePage = session->GetActivePage();
            if (activePage) {
                document.LoadBookmarksFromString(activePage->dedicatedPdfBookmarks);
                document.LoadHighlightsFromString(activePage->dedicatedPdfHighlights);
            }
        }

        LOG_INFO(PdfStorage, "PdfEngine background loading complete: " + document.filePath + " (" + std::to_string(document.totalPages) + " pages)");
    }

    // 2. Working-set maintenance: evict textures and home viewports of documents absent for >60,000ms
    uint64_t nowMs = SDL_GetTicks();
    constexpr uint64_t WORKING_SET_TIMEOUT_MS = 60000;
    for (auto& [path, docState] : documentWorkingSet) {
        if (path != document.filePath && (nowMs - docState.lastAccessTimeMs > WORKING_SET_TIMEOUT_MS)) {
            docState.EvictTextures();
            docState.HomeViewport();
            docState.isLoaded = false;
        }
    }
}

void PdfEngine::SwitchDocument(const std::string& diskPath, DocumentSession* session) {
    if (document.filePath == diskPath || diskPath.empty()) return;

    uint64_t nowMs = SDL_GetTicks();

    // Save outgoing document session state
    if (!document.filePath.empty()) {
        auto& outState = documentWorkingSet[document.filePath];
        outState.filePath = document.filePath;
        outState.scrollX = virtualizer.scrollX;
        outState.scrollY = virtualizer.scrollY;
        outState.maxScrollX = virtualizer.maxScrollX;
        outState.maxScrollY = virtualizer.maxScrollY;
        outState.zoomScale = virtualizer.zoomScale;
        outState.activePageIndex = virtualizer.activePageIndex;
        outState.lastAccessTimeMs = nowMs;
        outState.isLoaded = (!isLoading && document.totalPages > 0);
        outState.pageDimensions = std::move(document.pageDimensions);
        outState.docOutline = std::move(document.docOutline);
        outState.isOutlineLoaded = document.isOutlineLoaded;
        outState.userBookmarks = std::move(document.userBookmarks);
        outState.docHighlights = std::move(document.docHighlights);
        outState.pageCache = std::move(tileCache.pageCache);
        outState.thumbnailCache = std::move(tileCache.thumbnailCache);
    }

    // Clear active state
    Clear();

    // Check if incoming document exists in working set
    auto it = documentWorkingSet.find(diskPath);
    if (it != documentWorkingSet.end() && it->second.isLoaded && !it->second.pageDimensions.empty()) {
        auto& inState = it->second;
        document.filePath = diskPath;
        virtualizer.scrollX = inState.scrollX;
        virtualizer.scrollY = inState.scrollY;
        virtualizer.maxScrollX = inState.maxScrollX;
        virtualizer.maxScrollY = inState.maxScrollY;
        virtualizer.zoomScale = inState.zoomScale;
        virtualizer.activePageIndex = inState.activePageIndex;
        document.totalPages = static_cast<int>(inState.pageDimensions.size());
        document.pageDimensions = std::move(inState.pageDimensions);
        document.docOutline = std::move(inState.docOutline);
        document.isOutlineLoaded = inState.isOutlineLoaded;
        document.userBookmarks = std::move(inState.userBookmarks);
        document.docHighlights = std::move(inState.docHighlights);
        tileCache.pageCache = std::move(inState.pageCache);
        tileCache.thumbnailCache = std::move(inState.thumbnailCache);
        inState.lastAccessTimeMs = nowMs;

        if (session) {
            auto activePage = session->GetActivePage();
            if (activePage) {
                if (document.lastSyncedBookmarks != activePage->dedicatedPdfBookmarks) {
                    document.LoadBookmarksFromString(activePage->dedicatedPdfBookmarks);
                }
                if (document.lastSyncedHighlights != activePage->dedicatedPdfHighlights) {
                    document.LoadHighlightsFromString(activePage->dedicatedPdfHighlights);
                }
            }
        }
    } else {
        LoadDocument(diskPath);
    }
}

ImU32 PdfEngine::GetActiveHighlightColor(bool inverted) const noexcept {
    const auto& presets = GetHighlightColorPresets();
    if (activeHighlightColorIdx >= 0 && activeHighlightColorIdx < static_cast<int>(presets.size())) {
        return inverted ? presets[activeHighlightColorIdx].darkColor : presets[activeHighlightColorIdx].lightColor;
    }
    return inverted ? IM_COL32(0, 230, 255, 130) : IM_COL32(255, 235, 59, 115);
}

ImVec4 PdfEngine::GetActiveHighlightSwatch() const noexcept {
    const auto& presets = GetHighlightColorPresets();
    if (activeHighlightColorIdx >= 0 && activeHighlightColorIdx < static_cast<int>(presets.size())) {
        return presets[activeHighlightColorIdx].swatch;
    }
    return ImVec4(1.00f, 0.92f, 0.23f, 1.0f);
}

bool PdfEngine::RenderPageToBlend2D(BLContext& ctx, int pageIndex, double xMm, double yMm, double scale, bool invert) {
    if (document.filePath.empty() || pageIndex < 0 || pageIndex >= document.totalPages) {
        return false;
    }

    CachedPdfViewerPage* cached = tileCache.GetOrLoadPage(document.filePath, pageIndex, document.docHighlights, 150.0, invert);
    if (!cached || cached->image.is_empty()) {
        return false;
    }

    double drawW_mm = cached->widthMm * scale;
    double drawH_mm = cached->heightMm * scale;

    ctx.save();
    BLRect dstRect(xMm, yMm, drawW_mm, drawH_mm);
    ctx.blit_image(dstRect, cached->image);
    ctx.restore();
    return true;
}

} // namespace Folio
