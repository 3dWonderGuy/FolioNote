#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <mutex>
#include <sstream>
#include "imgui.h"
#include <SDL3/SDL.h>
#include "core/document/document_session.hpp"
#include "core/document/canvas_page.hpp"
#include "core/render/pdf_renderer.hpp"
#include "core/render/pdf_text_layer.hpp"
#include "input/input_state_machine.hpp"
#include "app/theme_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "utils/logger.hpp"
#include "utils/thread_pool.hpp"

namespace Folio {

/**
 * @brief Highlighting color preset definitions with light, dark-tuned, and swatch colors.
 */
struct PdfHighlightColorPreset {
    const char* name;
    ImU32 lightColor;
    ImU32 darkColor;
    ImVec4 swatch;
};

/**
 * @brief Provides the standard curated palette of digital notebook highlighter colors.
 * Includes Yellow, Green, Sky Blue, Rose Pink, Warm Orange, and Lavender Purple.
 */
inline const std::vector<PdfHighlightColorPreset>& GetHighlightColorPresets() {
    static const std::vector<PdfHighlightColorPreset> s_presets = {
        { "Sunshine Yellow", IM_COL32(255, 235, 59, 115),  IM_COL32(255, 235, 59, 130),  ImVec4(1.00f, 0.92f, 0.23f, 1.0f) },
        { "Neon Green",      IM_COL32(76, 217, 100, 115),  IM_COL32(76, 217, 100, 130),  ImVec4(0.30f, 0.85f, 0.39f, 1.0f) },
        { "Sky Blue",        IM_COL32(33, 150, 243, 115),  IM_COL32(0, 210, 255, 130),   ImVec4(0.13f, 0.59f, 0.95f, 1.0f) },
        { "Rose Pink",       IM_COL32(255, 64, 129, 115),  IM_COL32(255, 64, 129, 130),  ImVec4(1.00f, 0.25f, 0.51f, 1.0f) },
        { "Warm Orange",     IM_COL32(255, 152, 0, 115),   IM_COL32(255, 152, 0, 130),   ImVec4(1.00f, 0.60f, 0.00f, 1.0f) },
        { "Lavender Purple", IM_COL32(171, 71, 188, 115), IM_COL32(186, 104, 200, 130), ImVec4(0.67f, 0.28f, 0.74f, 1.0f) }
    };
    return s_presets;
}

/**
 * @brief Represents a persistent text highlight span overlaid on a PDF page.
 * Stores bounding box in millimeters, display color, character offsets, and extracted text snippet.
 */
struct TextHighlightSpan {
    AABB boundsMm;
    ImU32 color = IM_COL32(255, 235, 59, 115);
    int startChar = -1;
    int endChar = -1;
    std::string text;
    int pageIndex = 0;
};

struct CachedPdfViewerPage {
    int pageIndex = 0;
    BLImage image;
    GLuint glTexture = 0;
    int pixelW = 0;
    int pixelH = 0;
    double widthMm = 210.0;
    double heightMm = 297.0;
    PdfTextLayer textLayer;
    std::vector<PdfPageLink> links;
    std::vector<TextHighlightSpan> textHighlights;
    bool isLoaded = false;
    bool isInverted = false;

    void DestroyTexture() {
        if (glTexture != 0) {
            glDeleteTextures(1, &glTexture);
            glTexture = 0;
        }
    }
};

struct CachedThumbnail {
    int pageIndex = 0;
    GLuint glTexture = 0;
    int pixelW = 0;
    int pixelH = 0;
    double widthMm = 210.0;
    double heightMm = 297.0;
    bool isLoaded = false;
    bool isInverted = false;

    void DestroyTexture() {
        if (glTexture != 0) {
            glDeleteTextures(1, &glTexture);
            glTexture = 0;
        }
    }
};

struct PdfUserBookmark {
    int pageIndex = 0;
    std::string title;
};

struct PdfPageDimension {
    double widthMm = 210.0;
    double heightMm = 297.0;
};

enum class PdfSidebarTab : uint8_t {
    Thumbnails = 0,
    Outline,
    Bookmarks
};

enum class PdfToolMode : uint8_t {
    Highlight = 0,
    Select,
    Eraser
};

class PdfViewerPage {
public:
    static inline PdfViewerPage* s_activeInstance = nullptr;

    std::string currentPdfPath;
    int totalPages = 0;
    float scrollY = 0.0f;
    float maxScrollY = 0.0f;
    float zoomScale = 1.25f; // 1.25 = 125% zoom
    int activePageIndex = 0;
    bool currentInvertState = false;

    // Active tool mode (Highlight is text-snapping highlighter, Eraser removes highlights)
    PdfToolMode activeTool = PdfToolMode::Highlight;

    // Active highlighter color index into GetHighlightColorPresets()
    int activeHighlightColorIdx = 0; // Default: Sunshine Yellow

    /**
     * @brief Resolves the current highlight color, adapting brightness for inverted dark canvas.
     */
    [[nodiscard]] ImU32 GetActiveHighlightColor(bool inverted = false) const noexcept {
        const auto& presets = GetHighlightColorPresets();
        if (activeHighlightColorIdx >= 0 && activeHighlightColorIdx < static_cast<int>(presets.size())) {
            return inverted ? presets[activeHighlightColorIdx].darkColor : presets[activeHighlightColorIdx].lightColor;
        }
        return inverted ? IM_COL32(0, 230, 255, 130) : IM_COL32(255, 235, 59, 115);
    }

    /**
     * @brief Returns the RGBA swatch color for the active highlighter preset.
     */
    [[nodiscard]] ImVec4 GetActiveHighlightSwatch() const noexcept {
        const auto& presets = GetHighlightColorPresets();
        if (activeHighlightColorIdx >= 0 && activeHighlightColorIdx < static_cast<int>(presets.size())) {
            return presets[activeHighlightColorIdx].swatch;
        }
        return ImVec4(1.00f, 0.92f, 0.23f, 1.0f);
    }

    // Sidebar on the RIGHT side
    bool isSidebarOpen = true;
    float sidebarWidth = 270.0f;
    constexpr static float COLLAPSE_STRIP_WIDTH = 26.0f;
    PdfSidebarTab activeSidebarTab = PdfSidebarTab::Thumbnails;

    // Auto-vanishing bottom HUD (0.0f on start so it does not pop up automatically)
    float hudInactivityTimer = 0.0f;

    // Interactive Scrollbar dragging
    bool isDraggingScrollbar = false;
    float scrollbarGrabOffsetY = 0.0f;

    // Page text selection state
    int selectedPageIndex = -1;
    PdfTextSelection currentSelection;
    bool isSelectingText = false;

    // Outline & Bookmarks
    std::vector<PdfOutlineItem> docOutline;
    bool isOutlineLoaded = false;
    std::vector<PdfUserBookmark> userBookmarks;

    // Bookmark Sync & Rename Modal State
    std::string lastSyncedBookmarks;
    bool openBookmarkRenameModal = false;
    int bookmarkRenameIndex = -1;
    char bookmarkRenameBuffer[128] = "";

    // Context Menu State on PDF Canvas
    struct PdfContextMenuState {
        int pageIndex = -1;
        int highlightIndex = -1;
        std::string highlightText;
        bool hasTextSelection = false;
        ImVec2 clickPos;
    } contextMenuState;

    // Caches & Dimensions
    std::vector<PdfPageDimension> pageDimensions;
    std::unordered_map<int, CachedPdfViewerPage> pageCache;
    std::unordered_map<int, CachedThumbnail> thumbnailCache;

    // Toast notification for copy actions
    float toastTimer = 0.0f;
    std::string toastMessage;

    // Background Loading State & Telemetry for Loader Overlay
    bool isLoading = false;
    std::string loadingDocPath;
    std::string loadingDocName;
    std::atomic<int> loadingCurrentPage{0};
    std::atomic<int> loadingTotalPages{0};
    std::atomic<bool> loadingFinished{false};
    std::atomic<bool> loadingFailed{false};
    std::mutex pendingSummaryMutex;
    PdfRenderer::PdfDocSummary pendingSummary;
    float loaderSpinnerAngle = 0.0f;

    PdfViewerPage() {
        s_activeInstance = this;
    }

    ~PdfViewerPage() {
        if (s_activeInstance == this) {
            s_activeInstance = nullptr;
        }
        ClearCache();
    }

    static PdfViewerPage* GetActiveInstance() {
        return s_activeInstance;
    }

    /**
     * @brief Deserializes tab-separated bookmarks string into the in-memory userBookmarks list.
     * Format: <pageIndex>\t<title>\n per record.
     * @param data Serialized string retrieved from SQLite database record.
     */
    void LoadBookmarksFromString(const std::string& data) {
        userBookmarks.clear();
        if (data.empty()) return;
        std::istringstream stream(data);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            size_t tabPos = line.find('\t');
            if (tabPos != std::string::npos) {
                try {
                    int p = std::stoi(line.substr(0, tabPos));
                    std::string t = line.substr(tabPos + 1);
                    userBookmarks.push_back({p, t});
                } catch (...) {}
            }
        }
    }

    /**
     * @brief Serializes the in-memory userBookmarks list into a compact tab/newline string.
     * @return Formatted string for SQLite persistence in 'pages.dedicated_pdf_bookmarks'.
     */
    std::string SaveBookmarksToString() const {
        std::string out;
        for (const auto& bm : userBookmarks) {
            out += std::to_string(bm.pageIndex) + "\t" + bm.title + "\n";
        }
        return out;
    }

    /**
     * @brief Flushes user bookmarks to the active CanvasPage metadata and commits async to SQLite.
     * Ensures bookmarks are never lost when closing or reopening documents.
     */
    void SyncBookmarksToPage(DocumentSession& session) {
        auto activePage = session.GetActivePage();
        if (activePage) {
            activePage->dedicatedPdfBookmarks = SaveBookmarksToString();
            lastSyncedBookmarks = activePage->dedicatedPdfBookmarks;
            activePage->isModified = true;
            session.workspace.FlushActiveNotebookAsync();
            LOG_INFO(PdfStorage, "Synced " + std::to_string(userBookmarks.size()) + " bookmarks to page metadata.");
        }
    }

    void ToggleSidebar() {
        isSidebarOpen = !isSidebarOpen;
    }

    void PrevPage() {
        ScrollToPage(activePageIndex - 1);
    }

    void NextPage() {
        ScrollToPage(activePageIndex + 1);
    }

    void SetZoomScale(float z) {
        zoomScale = std::clamp(z, 0.4f, 4.0f);
        hudInactivityTimer = 2.5f;
    }

    void ClearCache() {
        for (auto& [idx, p] : pageCache) {
            p.DestroyTexture();
        }
        pageCache.clear();

        for (auto& [idx, t] : thumbnailCache) {
            t.DestroyTexture();
        }
        thumbnailCache.clear();
        docOutline.clear();
        isOutlineLoaded = false;
        pageDimensions.clear();
        totalPages = 0;
    }

    /**
     * @brief Initiates non-blocking background loading of a PDF document's structure and metadata.
     * Offloads PDFium parsing to a background worker thread so the UI thread runs at full 120 FPS
     * without stuttering, while the Loader Overlay displays live real-time progress.
     */
    void LoadDocument(const std::string& filePath) {
        if (filePath.empty()) return;
        if (currentPdfPath == filePath && (totalPages > 0 || isLoading)) return;

        ClearCache();
        currentPdfPath = filePath;
        scrollY = 0.0f;
        selectedPageIndex = -1;
        currentSelection.Clear();
        hudInactivityTimer = 2.5f;

        std::filesystem::path p(filePath);
        loadingDocPath = filePath;
        loadingDocName = p.filename().string();
        isLoading = true;
        loadingFinished.store(false);
        loadingFailed.store(false);
        loadingCurrentPage.store(0);
        loadingTotalPages.store(0);

        LOG_INFO(PdfStorage, "PdfViewerPage starting background structure load: " + filePath);

        GetGlobalThreadPool().Enqueue([this, filePath]() {
            PdfRenderer::PdfDocSummary summary;
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
            }
        });
    }

    void ScrollToPage(int pageIdx) {
        if (pageIdx < 0) pageIdx = 0;
        if (pageIdx >= totalPages) pageIdx = totalPages - 1;
        activePageIndex = pageIdx;

        constexpr float PAGE_GAP_PX = 24.0f;
        float targetY = 0.0f;
        float pxPerMm = (96.0f / 25.4f) * zoomScale;

        for (int p = 0; p < pageIdx; ++p) {
            double hMm = (p < static_cast<int>(pageDimensions.size())) ? pageDimensions[p].heightMm : 297.0;
            targetY += static_cast<float>(hMm * pxPerMm) + PAGE_GAP_PX;
        }

        scrollY = std::clamp(targetY, 0.0f, maxScrollY);
    }

    CachedPdfViewerPage* GetOrLoadPage(int pageIdx, double targetDpi = 150.0, bool invert = false) {
        auto it = pageCache.find(pageIdx);
        if (it != pageCache.end() && it->second.isLoaded && it->second.isInverted == invert) {
            return &it->second;
        }

        if (it != pageCache.end()) {
            it->second.DestroyTexture();
            pageCache.erase(it);
        }

        auto res = PdfRenderer::RenderPage(currentPdfPath, pageIdx, targetDpi, invert);
        if (!res.success || res.image.is_empty()) {
            return nullptr;
        }

        CachedPdfViewerPage entry;
        entry.pageIndex = pageIdx;
        entry.image = res.image;
        entry.pixelW = res.pixelWidth;
        entry.pixelH = res.pixelHeight;
        entry.widthMm = res.widthMm;
        entry.heightMm = res.heightMm;
        entry.links = std::move(res.links);
        entry.isInverted = invert;

        // Generate OpenGL texture for blitting
        glGenTextures(1, &entry.glTexture);
        glBindTexture(GL_TEXTURE_2D, entry.glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        BLImageData imgData;
        if (entry.image.get_data(&imgData) == BL_SUCCESS) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, entry.pixelW, entry.pixelH, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, imgData.pixel_data);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }

        // Load text layer for interaction and selection
        PdfRenderer::LoadTextLayer(currentPdfPath, pageIdx, entry.textLayer);
        entry.isLoaded = true;

        pageCache[pageIdx] = std::move(entry);
        return &pageCache[pageIdx];
    }

    CachedThumbnail* GetOrLoadThumbnail(int pageIdx, bool invert = false) {
        auto it = thumbnailCache.find(pageIdx);
        if (it != thumbnailCache.end() && it->second.isLoaded && it->second.isInverted == invert) {
            return &it->second;
        }

        if (it != thumbnailCache.end()) {
            it->second.DestroyTexture();
            thumbnailCache.erase(it);
        }

        // Render at 32 DPI for fast, lightweight previews
        auto res = PdfRenderer::RenderPage(currentPdfPath, pageIdx, 32.0, invert);
        if (!res.success || res.image.is_empty()) {
            return nullptr;
        }

        CachedThumbnail entry;
        entry.pageIndex = pageIdx;
        entry.pixelW = res.pixelWidth;
        entry.pixelH = res.pixelHeight;
        entry.widthMm = res.widthMm;
        entry.heightMm = res.heightMm;
        entry.isInverted = invert;

        glGenTextures(1, &entry.glTexture);
        glBindTexture(GL_TEXTURE_2D, entry.glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        BLImageData imgData;
        if (res.image.get_data(&imgData) == BL_SUCCESS) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, entry.pixelW, entry.pixelH, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, imgData.pixel_data);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
        entry.isLoaded = true;

        thumbnailCache[pageIdx] = std::move(entry);
        return &thumbnailCache[pageIdx];
    }

    /**
     * @brief Renders an elegant, non-blocking Loader Overlay when reading multi-hundred-page documents.
     * Prevents the user from perceiving the app as frozen while PDFium parses page dimensions and outline.
     */
    void RenderLoaderOverlay(float viewX, float viewTop, float viewW, float viewH, const ThemeManager& theme) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(viewX, viewTop));
        ImGui::SetNextWindowSize(ImVec2(viewW, viewH));
        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImVec4 bgCol = currentInvertState ? ImVec4(0.06f, 0.07f, 0.09f, 1.0f) : theme.colorSectionBg;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bgCol);

        if (ImGui::Begin("##PdfLoaderOverlayWindow", nullptr, winFlags)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();

            float cardW = std::min(460.0f, viewW - 40.0f);
            float cardH = 210.0f;
            float cardX = viewX + (viewW - cardW) * 0.5f;
            float cardY = viewTop + (viewH - cardH) * 0.5f;

            ImVec2 cMin(cardX, cardY);
            ImVec2 cMax(cardX + cardW, cardY + cardH);

            // Soft outer drop shadow
            dl->AddRectFilled(ImVec2(cMin.x + 4.0f, cMin.y + 6.0f), ImVec2(cMax.x + 4.0f, cMax.y + 6.0f),
                              IM_COL32(0, 0, 0, 80), 12.0f);

            // Card body (modern surface)
            ImU32 cardBg = currentInvertState ? IM_COL32(26, 29, 38, 255) : ImGui::GetColorU32(theme.colorPanel);
            dl->AddRectFilled(cMin, cMax, cardBg, 12.0f);
            dl->AddRect(cMin, cMax, ImGui::GetColorU32(theme.colorBorder), 12.0f, 0, 1.2f);

            // Top accent bar
            dl->AddRectFilled(ImVec2(cMin.x, cMin.y), ImVec2(cMax.x, cMin.y + 4.0f), ImGui::GetColorU32(theme.colorPrimary), 12.0f, ImDrawFlags_RoundCornersTop);

            // 1. Modern animated circular spinner
            loaderSpinnerAngle += io.DeltaTime * 6.5f;
            if (loaderSpinnerAngle > 62.83f) loaderSpinnerAngle -= 62.83f;

            ImVec2 spinnerCenter(cardX + cardW * 0.5f, cardY + 46.0f);
            float spinnerR = 17.0f;
            ImU32 spinnerTrack = currentInvertState ? IM_COL32(50, 56, 70, 160) : IM_COL32(215, 220, 230, 180);
            dl->AddCircle(spinnerCenter, spinnerR, spinnerTrack, 36, 3.0f);
            dl->PathArcTo(spinnerCenter, spinnerR, loaderSpinnerAngle, loaderSpinnerAngle + 1.85f, 24);
            dl->PathStroke(ImGui::GetColorU32(theme.colorPrimary), 0, 3.5f);

            // 2. Title: "Loading PDF Document"
            const char* titleTxt = loadingFailed.load() ? "Failed to Load PDF" : "Loading PDF Document";
            ImVec2 titleSz = ImGui::CalcTextSize(titleTxt);
            float titleX = cardX + (cardW - titleSz.x) * 0.5f;
            float titleY = cardY + 76.0f;
            ImU32 titleCol = loadingFailed.load() ? IM_COL32(230, 60, 60, 255) : ImGui::GetColorU32(theme.colorText);
            dl->AddText(ImVec2(titleX, titleY), titleCol, titleTxt);

            // 3. Document Filename
            std::string dispName = loadingDocName.empty() ? "Document" : loadingDocName;
            if (dispName.length() > 42) dispName = dispName.substr(0, 39) + "...";
            ImVec2 nameSz = ImGui::CalcTextSize(dispName.c_str());
            float nameX = cardX + (cardW - nameSz.x) * 0.5f;
            float nameY = titleY + titleSz.y + 5.0f;
            dl->AddText(ImVec2(nameX, nameY), ImGui::GetColorU32(theme.colorPrimary), dispName.c_str());

            // 4. Smooth Progress Bar
            float barW = cardW - 60.0f;
            float barH = 6.0f;
            float barX = cardX + 30.0f;
            float barY = nameY + nameSz.y + 14.0f;

            int cur = loadingCurrentPage.load();
            int tot = loadingTotalPages.load();
            float progress = (tot > 0) ? std::clamp(static_cast<float>(cur) / static_cast<float>(tot), 0.0f, 1.0f) : 0.0f;

            ImU32 barBg = currentInvertState ? IM_COL32(40, 45, 58, 200) : IM_COL32(220, 225, 235, 220);
            dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), barBg, 3.0f);
            if (progress > 0.01f) {
                dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW * progress, barY + barH), ImGui::GetColorU32(theme.colorPrimary), 3.0f);
            }

            // 5. Status / Page counter text
            char statusBuf[128];
            if (loadingFailed.load()) {
                std::snprintf(statusBuf, sizeof(statusBuf), "Could not open or parse PDF file.");
            } else if (tot > 0) {
                int pct = static_cast<int>(progress * 100.0f);
                std::snprintf(statusBuf, sizeof(statusBuf), "Processing page %d of %d (%d%%)", cur, tot, pct);
            } else {
                std::snprintf(statusBuf, sizeof(statusBuf), "Inspecting document structure...");
            }

            ImVec2 statusSz = ImGui::CalcTextSize(statusBuf);
            float statusX = cardX + (cardW - statusSz.x) * 0.5f;
            float statusY = barY + barH + 8.0f;
            dl->AddText(ImVec2(statusX, statusY), ImGui::GetColorU32(theme.colorTextMuted), statusBuf);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void Render(float viewX, float viewW, float screenH, float titleBarH, float ribbonH,
                DocumentSession& session, InputStateMachine& sm, const ThemeManager& theme,
                bool invertCanvas = false) {
        auto activePage = session.GetActivePage();
        if (!activePage || !activePage->isDedicatedPdf) return;

        std::string diskPath = PdfStorage::ResolveDiskPath(activePage->dedicatedPdfPath, &session);
        if (currentPdfPath != diskPath && !diskPath.empty()) {
            LoadDocument(diskPath);
            LoadBookmarksFromString(activePage->dedicatedPdfBookmarks);
            lastSyncedBookmarks = activePage->dedicatedPdfBookmarks;
        } else if (lastSyncedBookmarks != activePage->dedicatedPdfBookmarks) {
            LoadBookmarksFromString(activePage->dedicatedPdfBookmarks);
            lastSyncedBookmarks = activePage->dedicatedPdfBookmarks;
        }

        // Check if background worker finished loading PDF structure
        if (isLoading && loadingFinished.load()) {
            std::lock_guard<std::mutex> lock(pendingSummaryMutex);
            totalPages = pendingSummary.pageCount;
            pageDimensions.resize(totalPages);
            for (int i = 0; i < totalPages; ++i) {
                pageDimensions[i].widthMm = pendingSummary.dimensions[i].first;
                pageDimensions[i].heightMm = pendingSummary.dimensions[i].second;
            }
            docOutline = std::move(pendingSummary.outline);
            isOutlineLoaded = true;
            isLoading = false;
            loadingFinished.store(false);
            LOG_INFO(PdfStorage, "PdfViewerPage background loading complete: " + currentPdfPath + " (" + std::to_string(totalPages) + " pages)");
        }

        float viewTop = titleBarH + ribbonH;
        float viewH = screenH - viewTop;
        if (viewH <= 0.0f || viewW <= 0.0f) return;

        // Show Loader Overlay whenever document is processing or not yet loaded
        if (isLoading || totalPages <= 0) {
            RenderLoaderOverlay(viewX, viewTop, viewW, viewH, theme);
            return;
        }

        // Invert state change detection
        if (invertCanvas != currentInvertState) {
            currentInvertState = invertCanvas;
            // Invalidate textures so they re-render inverted
            for (auto& [idx, p] : pageCache) {
                p.DestroyTexture();
                p.isLoaded = false;
            }
            for (auto& [idx, t] : thumbnailCache) {
                t.DestroyTexture();
                t.isLoaded = false;
            }
        }

        ImGuiIO& io = ImGui::GetIO();

        // Main Window container
        ImGui::SetNextWindowPos(ImVec2(viewX, viewTop));
        ImGui::SetNextWindowSize(ImVec2(viewW, viewH));
        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, currentInvertState ? ImVec4(0.06f, 0.07f, 0.09f, 1.0f) : theme.colorSectionBg);

        if (ImGui::Begin("##DedicatedPdfViewerCanvas", nullptr, winFlags)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 origin = ImGui::GetCursorScreenPos();

            // Handle HUD inactivity timer
            if (hudInactivityTimer > 0.0f) {
                hudInactivityTimer -= io.DeltaTime;
            }

            // Layout calculation: Sidebar on the RIGHT side
            float currentSidebarW = isSidebarOpen ? sidebarWidth : COLLAPSE_STRIP_WIDTH;
            float contentX = origin.x;
            float contentW = viewW - currentSidebarW;
            float sidebarX = origin.x + contentW;

            ImVec2 mousePos = io.MousePos;
            bool isMouseInContent = (mousePos.x >= contentX && mousePos.x <= contentX + contentW &&
                                     mousePos.y >= origin.y && mousePos.y <= origin.y + viewH);

            // Trigger HUD visibility ONLY when explicitly hovering the bottom-center pill zone
            float hudPreviewW = 340.0f;
            float hudPreviewH = 42.0f;
            float hudZoneMinX = contentX + (contentW - hudPreviewW) * 0.5f - 10.0f;
            float hudZoneMaxX = hudZoneMinX + hudPreviewW + 20.0f;
            float hudZoneMinY = origin.y + viewH - hudPreviewH - 16.0f;
            float hudZoneMaxY = origin.y + viewH;

            if (mousePos.x >= hudZoneMinX && mousePos.x <= hudZoneMaxX &&
                mousePos.y >= hudZoneMinY && mousePos.y <= hudZoneMaxY) {
                hudInactivityTimer = 2.5f;
            }

            // Zoom Anchoring towards Mouse Cursor (Eliminates page drift during Ctrl+Wheel)
            if (isMouseInContent && std::abs(io.MouseWheel) > 0.01f) {
                if (io.KeyCtrl) {
                    float oldZoom = zoomScale;
                    zoomScale = std::clamp(zoomScale + io.MouseWheel * 0.12f, 0.4f, 4.0f);

                    float mouseYRel = mousePos.y - origin.y;
                    float docY = scrollY + mouseYRel;
                    float newDocY = docY * (zoomScale / oldZoom);
                    scrollY = std::clamp(newDocY - mouseYRel, 0.0f, maxScrollY);
                } else {
                    scrollY = std::clamp(scrollY - io.MouseWheel * 80.0f, 0.0f, maxScrollY);
                }
            }

            // =========================================================================
            // 1. MAIN DOCUMENT VIEWPORT (CENTERED PAGES, CLIPPED TO CONTENT BOUNDS)
            // =========================================================================
            ImVec2 contentClipMin(contentX, origin.y);
            ImVec2 contentClipMax(contentX + contentW, origin.y + viewH);
            dl->PushClipRect(contentClipMin, contentClipMax, true);

            constexpr float PAGE_GAP_PX = 24.0f;
            float currentY = 20.0f - scrollY;
            float pxPerMm = (96.0f / 25.4f) * zoomScale;

            int currentVisiblePage = 0;
            ImVec2 selectedBoxMin(0.0f, 0.0f), selectedBoxMax(0.0f, 0.0f);
            bool hasSelectionRects = false;

            for (int i = 0; i < totalPages; ++i) {
                double mmW = (i < static_cast<int>(pageDimensions.size())) ? pageDimensions[i].widthMm : 210.0;
                double mmH = (i < static_cast<int>(pageDimensions.size())) ? pageDimensions[i].heightMm : 297.0;

                float pageW_px = static_cast<float>(mmW * pxPerMm);
                float pageH_px = static_cast<float>(mmH * pxPerMm);

                // Check visibility against viewport window
                if (currentY + pageH_px > 0.0f && currentY < viewH) {
                    currentVisiblePage = i;
                    float pageX = (contentW - pageW_px) * 0.5f;
                    if (pageX < 15.0f) pageX = 15.0f;

                    ImVec2 pMin(contentX + pageX, origin.y + currentY);
                    ImVec2 pMax(pMin.x + pageW_px, pMin.y + pageH_px);

                    // Drop shadow
                    dl->AddRectFilled(ImVec2(pMin.x + 3.0f, pMin.y + 4.0f), ImVec2(pMax.x + 3.0f, pMax.y + 4.0f),
                                      IM_COL32(0, 0, 0, 50), 4.0f);

                    // Page paper surface (inverted dark paper or bright paper)
                    ImU32 paperBg = currentInvertState ? IM_COL32(24, 26, 32, 255) : IM_COL32(255, 255, 255, 255);
                    dl->AddRectFilled(pMin, pMax, paperBg, 2.0f);

                    // Load or retrieve rendered page bitmap from PDFium
                    CachedPdfViewerPage* cached = GetOrLoadPage(i, 150.0 * zoomScale, currentInvertState);
                    if (cached && cached->glTexture != 0) {
                        dl->AddImage((ImTextureID)(intptr_t)cached->glTexture, pMin, pMax);

                        bool isHovered = (mousePos.x >= pMin.x && mousePos.x <= pMax.x &&
                                          mousePos.y >= pMin.y && mousePos.y <= pMax.y);

                        // True mm mapping (1.0 / pxPerMm)
                        double scale = 1.0 / pxPerMm;
                        double mouseLocalMmX = (mousePos.x - pMin.x) * scale;
                        double mouseLocalMmY = (mousePos.y - pMin.y) * scale;

                        // -------------------------------------------------------------
                        // 1A. DYNAMIC PDF CLICKABLE LINKS
                        // -------------------------------------------------------------
                        if (isHovered) {
                            for (const auto& l : cached->links) {
                                if (mouseLocalMmX >= l.minX_mm && mouseLocalMmX <= l.maxX_mm &&
                                    mouseLocalMmY >= l.minY_mm && mouseLocalMmY <= l.maxY_mm) {
                                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                                    float lx0 = pMin.x + static_cast<float>(l.minX_mm * pxPerMm);
                                    float ly0 = pMin.y + static_cast<float>(l.minY_mm * pxPerMm);
                                    float lx1 = pMin.x + static_cast<float>(l.maxX_mm * pxPerMm);
                                    float ly1 = pMin.y + static_cast<float>(l.maxY_mm * pxPerMm);

                                    dl->AddRect(ImVec2(lx0, ly0), ImVec2(lx1, ly1), IM_COL32(33, 150, 243, 200), 2.0f);

                                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                        if (l.targetPageIndex >= 0) {
                                            ScrollToPage(l.targetPageIndex);
                                            LOG_INFO(PdfStorage, "Followed PDF link to page " + std::to_string(l.targetPageIndex + 1));
                                        } else if (!l.uri.empty()) {
                                            SDL_OpenURL(l.uri.c_str());
                                            LOG_INFO(PdfStorage, "Opened external PDF URL: " + l.uri);
                                        }
                                    }
                                    break;
                                }
                            }
                        }

                        // -------------------------------------------------------------
                        // 1B. SAVED TEXT HIGHLIGHTS & CLICK-TO-ERASE
                        // -------------------------------------------------------------
                        bool erasedHighlight = false;
                        int delHlIdx = -1;

                        for (size_t h = 0; h < cached->textHighlights.size(); ++h) {
                            const auto& hl = cached->textHighlights[h];
                            float hx0 = pMin.x + static_cast<float>(hl.boundsMm.minX * pxPerMm);
                            float hy0 = pMin.y + static_cast<float>(hl.boundsMm.minY * pxPerMm);
                            float hx1 = pMin.x + static_cast<float>(hl.boundsMm.maxX * pxPerMm);
                            float hy1 = pMin.y + static_cast<float>(hl.boundsMm.maxY * pxPerMm);

                            dl->AddRectFilled(ImVec2(hx0, hy0), ImVec2(hx1, hy1), hl.color, 2.0f);

                            // Hovering an existing highlight indicates eraser action only when Eraser tool is active
                            if (isHovered && mouseLocalMmX >= (hl.boundsMm.minX - 0.5) && mouseLocalMmX <= (hl.boundsMm.maxX + 0.5) &&
                                mouseLocalMmY >= (hl.boundsMm.minY - 0.5) && mouseLocalMmY <= (hl.boundsMm.maxY + 0.5)) {
                                if (activeTool == PdfToolMode::Eraser) {
                                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                        delHlIdx = static_cast<int>(h);
                                    }
                                }
                            }
                        }

                        if (delHlIdx >= 0 && delHlIdx < static_cast<int>(cached->textHighlights.size())) {
                            cached->textHighlights.erase(cached->textHighlights.begin() + delHlIdx);
                            toastMessage = "Highlight erased";
                            toastTimer = 1.5f;
                            selectedPageIndex = -1;
                            currentSelection.Clear();
                            isSelectingText = false;
                            erasedHighlight = true;
                        }

                        // -------------------------------------------------------------
                        // RIGHT-CLICK CONTEXT MENU DETECTION ON THIS PAGE
                        // -------------------------------------------------------------
                        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            contextMenuState.pageIndex = i;
                            contextMenuState.highlightIndex = -1;
                            contextMenuState.highlightText.clear();
                            contextMenuState.hasTextSelection = (selectedPageIndex >= 0 && currentSelection.hasSelection);
                            contextMenuState.clickPos = mousePos;

                            // Check if right click landed directly on a highlight span
                            for (size_t h = 0; h < cached->textHighlights.size(); ++h) {
                                const auto& hl = cached->textHighlights[h];
                                if (mouseLocalMmX >= (hl.boundsMm.minX - 0.5) && mouseLocalMmX <= (hl.boundsMm.maxX + 0.5) &&
                                    mouseLocalMmY >= (hl.boundsMm.minY - 0.5) && mouseLocalMmY <= (hl.boundsMm.maxY + 0.5)) {
                                    contextMenuState.highlightIndex = static_cast<int>(h);
                                    contextMenuState.highlightText = hl.text;
                                    break;
                                }
                            }

                            ImGui::OpenPopup("##PdfCanvasContextMenu");
                        }

                        // -------------------------------------------------------------
                        // 1C. TEXT INTERACTION & PRECISE DRAG SELECTION
                        // -------------------------------------------------------------
                        if (isHovered && !erasedHighlight) {
                            int hitChar = cached->textLayer.HitTestChar(mouseLocalMmX, mouseLocalMmY, 3.0);
                            if (hitChar >= 0 && !isSelectingText) {
                                ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
                            }

                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl) {
                                if (hitChar >= 0) {
                                    selectedPageIndex = i;
                                    currentSelection.startChar = hitChar;
                                    currentSelection.endChar = hitChar;
                                    currentSelection.hasSelection = true;
                                    isSelectingText = true;
                                } else {
                                    selectedPageIndex = -1;
                                    currentSelection.Clear();
                                }
                            }
                        }

                        // Continuous drag selection across page
                        if (isSelectingText && selectedPageIndex == i && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                            int trackChar = cached->textLayer.FindNearestChar(mouseLocalMmX, mouseLocalMmY);
                            if (trackChar >= 0) {
                                currentSelection.endChar = trackChar;
                                currentSelection.hasSelection = true;
                            }
                        }

                        // -------------------------------------------------------------
                        // 1D. RENDER ACTIVE TEXT SELECTION HIGHLIGHTS
                        // -------------------------------------------------------------
                        if (selectedPageIndex == i && currentSelection.hasSelection) {
                            auto boxes = cached->textLayer.GetSelectionBoxes(currentSelection);
                            ImU32 selColor = currentInvertState ? IM_COL32(0, 200, 255, 90) : IM_COL32(33, 150, 243, 90);

                            for (const auto& b : boxes) {
                                float bx0 = pMin.x + static_cast<float>(b.minX * pxPerMm);
                                float by0 = pMin.y + static_cast<float>(b.minY * pxPerMm);
                                float bx1 = pMin.x + static_cast<float>(b.maxX * pxPerMm);
                                float by1 = pMin.y + static_cast<float>(b.maxY * pxPerMm);

                                dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), selColor, 2.0f);

                                if (!hasSelectionRects) {
                                    selectedBoxMin = ImVec2(bx0, by0);
                                    selectedBoxMax = ImVec2(bx1, by1);
                                    hasSelectionRects = true;
                                } else {
                                    selectedBoxMin.x = std::min(selectedBoxMin.x, bx0);
                                    selectedBoxMin.y = std::min(selectedBoxMin.y, by0);
                                    selectedBoxMax.x = std::max(selectedBoxMax.x, bx1);
                                    selectedBoxMax.y = std::max(selectedBoxMax.y, by1);
                                }
                            }
                        }
                    }

                    // Border outline
                    ImU32 borderColor = currentInvertState ? IM_COL32(60, 65, 75, 255) : IM_COL32(200, 205, 215, 255);
                    dl->AddRect(pMin, pMax, borderColor, 2.0f, 0, 1.0f);

                    // Page number footer label
                    char pageNumStr[32];
                    std::snprintf(pageNumStr, sizeof(pageNumStr), "- %d -", i + 1);
                    ImVec2 lblSz = ImGui::CalcTextSize(pageNumStr);
                    dl->AddText(ImVec2(pMin.x + (pageW_px - lblSz.x) * 0.5f, pMax.y + 4.0f),
                                ImGui::GetColorU32(theme.colorTextMuted), pageNumStr);
                }

                currentY += (pageH_px + PAGE_GAP_PX);
            }

            activePageIndex = currentVisiblePage;
            maxScrollY = std::max(0.0f, (currentY + scrollY) - viewH + 60.0f);

            // Pop Content Viewport Clipping
            dl->PopClipRect();

            // Right-click on empty canvas margin outside page bounds
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsPopupOpen("##PdfCanvasContextMenu")) {
                if (mousePos.x >= contentX && mousePos.x <= contentX + contentW) {
                    contextMenuState.pageIndex = activePageIndex;
                    contextMenuState.highlightIndex = -1;
                    contextMenuState.highlightText.clear();
                    contextMenuState.hasTextSelection = (selectedPageIndex >= 0 && currentSelection.hasSelection);
                    contextMenuState.clickPos = mousePos;
                    ImGui::OpenPopup("##PdfCanvasContextMenu");
                }
            }

            // Pointer release handling
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (isSelectingText && currentSelection.hasSelection && selectedPageIndex >= 0) {
                    if (activeTool == PdfToolMode::Highlight) {
                        auto it = pageCache.find(selectedPageIndex);
                        if (it != pageCache.end()) {
                            auto boxes = it->second.textLayer.GetSelectionBoxes(currentSelection);
                            std::string selText = it->second.textLayer.GetSelectedText(currentSelection);
                            for (const auto& b : boxes) {
                                TextHighlightSpan span;
                                span.boundsMm = b;
                                span.color = GetActiveHighlightColor(currentInvertState);
                                span.startChar = currentSelection.MinIdx();
                                span.endChar = currentSelection.MaxIdx();
                                span.text = selText;
                                span.pageIndex = selectedPageIndex;
                                it->second.textHighlights.push_back(span);
                            }
                            toastMessage = "Text highlighted";
                            toastTimer = 1.5f;
                            selectedPageIndex = -1;
                            currentSelection.Clear();
                        }
                    }
                }
                isSelectingText = false;
                isDraggingScrollbar = false;
            }

            // Keyboard shortcuts: Ctrl+C copies selected text
            if (currentSelection.hasSelection && selectedPageIndex >= 0) {
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
                    auto it = pageCache.find(selectedPageIndex);
                    if (it != pageCache.end()) {
                        std::string text = it->second.textLayer.GetSelectedText(currentSelection);
                        if (!text.empty()) {
                            SDL_SetClipboardText(text.c_str());
                            toastMessage = "Copied text to clipboard!";
                            toastTimer = 2.0f;
                        }
                    }
                }
            }

            // =========================================================================
            // 2. INTERACTIVE VERTICAL SCROLLBAR (RIGHT EDGE OF CONTENT VIEWPORT)
            // =========================================================================
            constexpr float SCROLLBAR_W = 12.0f;
            float sbX = contentX + contentW - SCROLLBAR_W - 2.0f;
            float sbY = origin.y + 4.0f;
            float sbH = viewH - 8.0f;

            if (maxScrollY > 0.0f) {
                ImVec2 trackMin(sbX, sbY);
                ImVec2 trackMax(sbX + SCROLLBAR_W, sbY + sbH);
                dl->AddRectFilled(trackMin, trackMax, IM_COL32(18, 20, 26, 120), 6.0f);

                float totalContentH = maxScrollY + viewH;
                float thumbH = std::clamp(sbH * (viewH / totalContentH), 32.0f, sbH);
                float thumbY = sbY + (scrollY / maxScrollY) * (sbH - thumbH);

                ImVec2 thumbMin(sbX + 1.0f, thumbY);
                ImVec2 thumbMax(sbX + SCROLLBAR_W - 1.0f, thumbY + thumbH);

                bool isThumbHovered = (mousePos.x >= thumbMin.x && mousePos.x <= thumbMax.x &&
                                       mousePos.y >= thumbMin.y && mousePos.y <= thumbMax.y);
                bool isTrackHovered = (mousePos.x >= trackMin.x && mousePos.x <= trackMax.x &&
                                       mousePos.y >= trackMin.y && mousePos.y <= trackMax.y);

                if (isTrackHovered || isDraggingScrollbar) {
                    hudInactivityTimer = 2.5f;
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isThumbHovered) {
                    isDraggingScrollbar = true;
                    scrollbarGrabOffsetY = mousePos.y - thumbY;
                } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isTrackHovered) {
                    float clickRatio = (mousePos.y - sbY - thumbH * 0.5f) / (sbH - thumbH);
                    scrollY = std::clamp(clickRatio * maxScrollY, 0.0f, maxScrollY);
                    isDraggingScrollbar = true;
                    scrollbarGrabOffsetY = thumbH * 0.5f;
                }

                if (isDraggingScrollbar && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    float newThumbY = mousePos.y - scrollbarGrabOffsetY;
                    float newRatio = (newThumbY - sbY) / (sbH - thumbH);
                    scrollY = std::clamp(newRatio * maxScrollY, 0.0f, maxScrollY);
                }

                ImU32 thumbColor = (isDraggingScrollbar || isThumbHovered)
                    ? IM_COL32(130, 145, 170, 240)
                    : IM_COL32(85, 95, 115, 180);

                dl->AddRectFilled(thumbMin, thumbMax, thumbColor, 5.0f);
            }

            // =========================================================================
            // 3. FOLDABLE SIDEBAR (ON THE RIGHT SIDE)
            // =========================================================================
            if (!isSidebarOpen) {
                // Collapsed vertical strip on right
                ImVec2 stripMin(sidebarX, origin.y);
                ImVec2 stripMax(sidebarX + COLLAPSE_STRIP_WIDTH, origin.y + viewH);
                ImU32 stripBg = currentInvertState ? IM_COL32(20, 22, 28, 255) : ImGui::GetColorU32(theme.colorPanel);
                dl->AddRectFilled(stripMin, stripMax, stripBg);
                dl->AddLine(ImVec2(stripMin.x, stripMin.y), ImVec2(stripMin.x, stripMax.y), ImGui::GetColorU32(theme.colorBorder));

                // Sleek Expand button with centered chevron
                float expW = COLLAPSE_STRIP_WIDTH - 6.0f;
                float expH = 28.0f;
                ImVec2 expMin(sidebarX + 3.0f, origin.y + 10.0f);
                ImVec2 expMax(expMin.x + expW, expMin.y + expH);
                bool expHovered = (mousePos.x >= expMin.x && mousePos.x <= expMax.x && mousePos.y >= expMin.y && mousePos.y <= expMin.y + expH);

                if (expHovered) {
                    dl->AddRectFilled(expMin, expMax, ImGui::GetColorU32(theme.colorItemHover), 4.0f);
                    ImGui::SetTooltip("Expand Sidebar (Pages, Outline, Bookmarks)");
                }
                dl->AddRect(expMin, expMax, ImGui::GetColorU32(theme.colorBorder), 4.0f, 0, 1.0f);

                float expArrX = expMin.x + expW * 0.5f;
                float expArrY = expMin.y + expH * 0.5f;
                ImU32 expArrCol = ImGui::GetColorU32(theme.colorText);
                dl->AddLine(ImVec2(expArrX + 2.5f, expArrY - 4.5f), ImVec2(expArrX - 2.5f, expArrY), expArrCol, 1.5f);
                dl->AddLine(ImVec2(expArrX - 2.5f, expArrY), ImVec2(expArrX + 2.5f, expArrY + 4.5f), expArrCol, 1.5f);

                ImGui::SetCursorScreenPos(expMin);
                if (ImGui::InvisibleButton("##ExpandSbBtn", ImVec2(expW, expH))) {
                    isSidebarOpen = true;
                }
            } else {
                // Expanded Sidebar on right
                ImVec2 sbMin(sidebarX, origin.y);
                ImVec2 sbMax(sidebarX + sidebarWidth, origin.y + viewH);
                ImU32 sbBg = currentInvertState ? IM_COL32(20, 22, 28, 255) : ImGui::GetColorU32(theme.colorPanel);
                dl->AddRectFilled(sbMin, sbMax, sbBg);
                dl->AddLine(ImVec2(sbMin.x, sbMin.y), ImVec2(sbMin.x, sbMax.y), ImGui::GetColorU32(theme.colorBorder));

                // Top Header: Collapse chevron + modern segmented tabs container
                float headerH = 36.0f;
                float headerY = origin.y + 6.0f;

                // 1. Sleek collapse button
                float collW = 26.0f;
                float collH = 28.0f;
                ImVec2 collMin(sidebarX + 6.0f, headerY);
                ImVec2 collMax(collMin.x + collW, collMin.y + collH);
                bool collHovered = (mousePos.x >= collMin.x && mousePos.x <= collMax.x && mousePos.y >= collMin.y && mousePos.y <= collMin.y + collH);

                if (collHovered) {
                    dl->AddRectFilled(collMin, collMax, ImGui::GetColorU32(theme.colorItemHover), 4.0f);
                    ImGui::SetTooltip("Collapse Sidebar");
                }
                dl->AddRect(collMin, collMax, ImGui::GetColorU32(theme.colorBorder), 4.0f, 0, 1.0f);

                float collArrX = collMin.x + collW * 0.5f;
                float collArrY = collMin.y + collH * 0.5f;
                ImU32 collArrCol = ImGui::GetColorU32(theme.colorText);
                dl->AddLine(ImVec2(collArrX - 2.5f, collArrY - 4.5f), ImVec2(collArrX + 2.5f, collArrY), collArrCol, 1.5f);
                dl->AddLine(ImVec2(collArrX + 2.5f, collArrY), ImVec2(collArrX - 2.5f, collArrY + 4.5f), collArrCol, 1.5f);

                ImGui::SetCursorScreenPos(collMin);
                if (ImGui::InvisibleButton("##CollapseSbBtn", ImVec2(collW, collH))) {
                    isSidebarOpen = false;
                }

                // 2. Modern Segmented Tab Bar Container (widened buttons with ample breathing room)
                float segStartX = collMax.x + 6.0f;
                float segTotalW = (sidebarX + sidebarWidth - 8.0f) - segStartX;
                float tabH = 28.0f;
                float tabW = (segTotalW - 4.0f) / 3.0f;

                ImVec2 segMin(segStartX, headerY);
                ImVec2 segMax(segStartX + segTotalW, headerY + tabH);
                ImU32 segBg = currentInvertState ? IM_COL32(15, 17, 23, 255) : IM_COL32(234, 237, 244, 255);
                dl->AddRectFilled(segMin, segMax, segBg, 4.0f);
                dl->AddRect(segMin, segMax, ImGui::GetColorU32(theme.colorBorder), 4.0f, 0, 1.0f);

                const char* tabNames[3] = { "Pages", "Outline", "Marks" };
                PdfSidebarTab tabEnums[3] = { PdfSidebarTab::Thumbnails, PdfSidebarTab::Outline, PdfSidebarTab::Bookmarks };

                for (int t = 0; t < 3; ++t) {
                    ImVec2 tMin(segStartX + 2.0f + t * tabW, segMin.y + 2.0f);
                    ImVec2 tMax(tMin.x + tabW, segMax.y - 2.0f);
                    bool isTabActive = (activeSidebarTab == tabEnums[t]);
                    bool isTabHov = (mousePos.x >= tMin.x && mousePos.x <= tMax.x && mousePos.y >= tMin.y && mousePos.y <= tMax.y);

                    if (isTabActive) {
                        dl->AddRectFilled(tMin, tMax, ImGui::GetColorU32(theme.colorPrimary), 3.0f);
                    } else if (isTabHov) {
                        dl->AddRectFilled(tMin, tMax, ImGui::GetColorU32(theme.colorItemHover), 3.0f);
                    }

                    // Precise mathematical vertical text centering
                    ImVec2 tSz = ImGui::CalcTextSize(tabNames[t]);
                    float tx = tMin.x + (tabW - tSz.x) * 0.5f;
                    float ty = tMin.y + ((tMax.y - tMin.y) - tSz.y) * 0.5f;
                    ImU32 txCol = isTabActive ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(theme.colorText);
                    dl->AddText(ImVec2(tx, ty), txCol, tabNames[t]);

                    ImGui::SetCursorScreenPos(tMin);
                    std::string btnId = "##PdfTabBtn_" + std::to_string(t);
                    if (ImGui::InvisibleButton(btnId.c_str(), ImVec2(tabW, tMax.y - tMin.y))) {
                        activeSidebarTab = tabEnums[t];
                    }
                }

                // Divider line below header
                dl->AddLine(ImVec2(sbMin.x, headerY + tabH + 6.0f), ImVec2(sbMax.x, headerY + tabH + 6.0f), ImGui::GetColorU32(theme.colorBorder));

                // Sidebar Tab Content Area
                ImGui::SetCursorScreenPos(ImVec2(sidebarX + 4.0f, headerY + tabH + 10.0f));
                ImGuiWindowFlags sbFlags = ImGuiWindowFlags_AlwaysVerticalScrollbar;
                if (activeSidebarTab == PdfSidebarTab::Outline) {
                    sbFlags |= ImGuiWindowFlags_HorizontalScrollbar;
                }
                ImGui::BeginChild("##PdfSidebarChild", ImVec2(sidebarWidth - 8.0f, viewH - (headerY - origin.y + tabH + 14.0f)), false, sbFlags);

                // Use the child window's draw list so all graphics are strictly clipped below the header buttons
                ImDrawList* childDl = ImGui::GetWindowDrawList();

                // --- TAB 1: MINI PAGE THUMBNAILS ---
                if (activeSidebarTab == PdfSidebarTab::Thumbnails) {
                    float thumbTargetW = sidebarWidth - 36.0f;
                    if (thumbTargetW < 120.0f) thumbTargetW = 120.0f;

                    // Frustum culling metrics: Only load and draw thumbnails currently visible in scroll viewport
                    float scrollY = ImGui::GetScrollY();
                    float childH = ImGui::GetWindowHeight();
                    float clipMinY = scrollY - 250.0f;
                    float clipMaxY = scrollY + childH + 250.0f;
                    float curY = 0.0f;

                    for (int i = 0; i < totalPages; ++i) {
                        double tW = (i < static_cast<int>(pageDimensions.size())) ? pageDimensions[i].widthMm : 210.0;
                        double tH = (i < static_cast<int>(pageDimensions.size())) ? pageDimensions[i].heightMm : 297.0;
                        float thumbH = thumbTargetW * static_cast<float>(tH / tW);
                        float totalItemH = thumbH + 24.0f;

                        // Skip processing if out of visible vertical scroll window
                        if (curY + totalItemH < clipMinY || curY > clipMaxY) {
                            ImGui::Dummy(ImVec2(thumbTargetW + 8.0f, totalItemH));
                            curY += totalItemH;
                            continue;
                        }

                        ImVec2 curPos = ImGui::GetCursorScreenPos();
                        ImVec2 cardMin(curPos.x + 4.0f, curPos.y + 2.0f);
                        ImVec2 cardMax(cardMin.x + thumbTargetW, cardMin.y + thumbH);

                        bool isActive = (activePageIndex == i);

                        childDl->AddRectFilled(ImVec2(cardMin.x + 2.0f, cardMin.y + 2.0f),
                                               ImVec2(cardMax.x + 2.0f, cardMax.y + 2.0f), IM_COL32(0, 0, 0, 60), 3.0f);
                        childDl->AddRectFilled(cardMin, cardMax, currentInvertState ? IM_COL32(24, 26, 32, 255) : IM_COL32(250, 250, 252, 255), 3.0f);

                        CachedThumbnail* thumb = GetOrLoadThumbnail(i, currentInvertState);
                        if (thumb && thumb->glTexture != 0) {
                            childDl->AddImage((ImTextureID)(intptr_t)thumb->glTexture, cardMin, cardMax);
                        }

                        if (isActive) {
                            childDl->AddRect(cardMin, cardMax, ImGui::GetColorU32(theme.colorPrimary), 3.0f, 0, 2.5f);
                        } else {
                            childDl->AddRect(cardMin, cardMax, ImGui::GetColorU32(theme.colorBorder), 3.0f, 0, 1.0f);
                        }

                        ImGui::InvisibleButton(("##ThumbBtn_" + std::to_string(i)).c_str(), ImVec2(thumbTargetW + 8.0f, thumbH + 4.0f));
                        if (ImGui::IsItemClicked()) {
                            ScrollToPage(i);
                        }

                        char pNumStr[32];
                        std::snprintf(pNumStr, sizeof(pNumStr), "Page %d", i + 1);
                        ImVec2 pNumSz = ImGui::CalcTextSize(pNumStr);
                        float lblX = cardMin.x + (thumbTargetW - pNumSz.x) * 0.5f;
                        childDl->AddText(ImVec2(lblX, cardMax.y + 3.0f),
                                         isActive ? ImGui::GetColorU32(theme.colorPrimary) : ImGui::GetColorU32(theme.colorTextMuted), pNumStr);

                        ImGui::Dummy(ImVec2(0.0f, 18.0f));
                        curY += totalItemH;
                    }
                }
                // --- TAB 2: PDF ORGANIZATION TREE (OUTLINE) ---
                else if (activeSidebarTab == PdfSidebarTab::Outline) {
                    if (docOutline.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                        ImGui::TextWrapped("No document outline / bookmarks found in this PDF file.");
                        ImGui::PopStyleColor();
                    } else {
                        auto RenderOutlineNode = [&](auto& self, const PdfOutlineItem& node) -> void {
                            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                            if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
                            if (node.pageIndex == activePageIndex) flags |= ImGuiTreeNodeFlags_Selected;

                            bool open = ImGui::TreeNodeEx((void*)&node, flags, "%s", node.title.c_str());
                            if (ImGui::IsItemClicked() && node.pageIndex >= 0) {
                                ScrollToPage(node.pageIndex);
                            }
                            if (open) {
                                for (const auto& child : node.children) {
                                    self(self, child);
                                }
                                ImGui::TreePop();
                            }
                        };

                        for (const auto& rootItem : docOutline) {
                            RenderOutlineNode(RenderOutlineNode, rootItem);
                        }
                    }
                }
                // --- TAB 3: USER BOOKMARKS ---
                else if (activeSidebarTab == PdfSidebarTab::Bookmarks) {
                    char addBmLabel[64];
                    std::snprintf(addBmLabel, sizeof(addBmLabel), "+ Bookmark Page %d", activePageIndex + 1);
                    if (ImGui::Button(addBmLabel, ImVec2(sidebarWidth - 24.0f, 28.0f))) {
                        PdfUserBookmark bm;
                        bm.pageIndex = activePageIndex;
                        bm.title = "Page " + std::to_string(activePageIndex + 1);
                        userBookmarks.push_back(bm);
                        SyncBookmarksToPage(session);
                        LOG_INFO(PdfStorage, "Added bookmark for page " + std::to_string(activePageIndex + 1));
                    }

                    ImGui::Separator();
                    ImGui::Spacing();

                    if (userBookmarks.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                        ImGui::TextWrapped("No bookmarks yet. Click above to bookmark the active page.");
                        ImGui::PopStyleColor();
                    } else {
                        int delIdx = -1;
                        for (size_t b = 0; b < userBookmarks.size(); ++b) {
                            ImGui::PushID(static_cast<int>(b));
                            bool isCurrentBm = (userBookmarks[b].pageIndex == activePageIndex);

                            if (isCurrentBm) ImGui::PushStyleColor(ImGuiCol_Text, theme.colorPrimary);
                            if (ImGui::Selectable(userBookmarks[b].title.c_str(), isCurrentBm, 0, ImVec2(sidebarWidth - 58.0f, 22.0f))) {
                                ScrollToPage(userBookmarks[b].pageIndex);
                            }
                            if (isCurrentBm) ImGui::PopStyleColor();

                            // Right-click context menu on bookmark
                            ContextMenuThemeScope bmCtxScope(theme);
                            if (ImGui::BeginPopupContextItem(("##BookmarkCtx_" + std::to_string(b)).c_str())) {
                                ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
                                ImGui::TextColored(theme.colorPrimary, "%s", userBookmarks[b].title.c_str());
                                ImGui::PopFont();
                                ImGui::TextColored(theme.colorTextMuted, "Bookmark • Page %d", userBookmarks[b].pageIndex + 1);
                                ImGui::Separator();

                                if (ImGui::MenuItem("Create Link to Bookmark")) {
                                    std::string pageGuid = session.GetActivePage() ? session.GetActivePage()->guid : "";
                                    std::string docTitle = std::filesystem::path(currentPdfPath).filename().string();
                                    std::string link = "[Bookmark: \"" + userBookmarks[b].title + "\" (" + docTitle + ", Page " +
                                                       std::to_string(userBookmarks[b].pageIndex + 1) + ")](folionote://page/" +
                                                       pageGuid + "?pdfPage=" + std::to_string(userBookmarks[b].pageIndex + 1) + ")";
                                    SDL_SetClipboardText(link.c_str());
                                    toastMessage = "Copied bookmark link to clipboard!";
                                    toastTimer = 2.0f;
                                }

                                if (ImGui::MenuItem("Rename Bookmark...")) {
                                    openBookmarkRenameModal = true;
                                    bookmarkRenameIndex = static_cast<int>(b);
                                    strncpy(bookmarkRenameBuffer, userBookmarks[b].title.c_str(), sizeof(bookmarkRenameBuffer) - 1);
                                    bookmarkRenameBuffer[sizeof(bookmarkRenameBuffer) - 1] = '\0';
                                }

                                ImGui::Separator();
                                if (ImGui::MenuItem("Delete Bookmark")) {
                                    delIdx = static_cast<int>(b);
                                }
                                ImGui::EndPopup();
                            }

                            ImGui::SameLine();
                            if (ImGui::SmallButton("x")) {
                                delIdx = static_cast<int>(b);
                            }
                            ImGui::PopID();
                        }

                        if (delIdx >= 0 && delIdx < static_cast<int>(userBookmarks.size())) {
                            userBookmarks.erase(userBookmarks.begin() + delIdx);
                            SyncBookmarksToPage(session);
                        }
                    }
                }

                ImGui::EndChild();
            }

            // =========================================================================
            // 4. CANVAS RIGHT-CLICK CONTEXT MENU
            // =========================================================================
            {
                ContextMenuThemeScope ctxScope(theme);
                if (ImGui::BeginPopup("##PdfCanvasContextMenu")) {
                    ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
                    std::string docTitle = std::filesystem::path(currentPdfPath).filename().string();
                    ImGui::TextColored(theme.colorPrimary, "%s", docTitle.c_str());
                    ImGui::PopFont();
                    int targetPage = (contextMenuState.pageIndex >= 0) ? contextMenuState.pageIndex : activePageIndex;
                    ImGui::TextColored(theme.colorTextMuted, "Page %d of %d", targetPage + 1, totalPages);
                    ImGui::Separator();

                    // 1. Copy (enabled only if text is selected)
                    if (ImGui::MenuItem("Copy", "Ctrl+C", false, contextMenuState.hasTextSelection)) {
                        auto it = pageCache.find(selectedPageIndex);
                        if (it != pageCache.end()) {
                            std::string text = it->second.textLayer.GetSelectedText(currentSelection);
                            SDL_SetClipboardText(text.c_str());
                            toastMessage = "Copied text to clipboard!";
                            toastTimer = 2.0f;
                            LOG_INFO(PdfStorage, "Copied text via context menu.");
                        }
                    }

                    // 2. Paste (enabled if clipboard has text)
                    bool canPaste = SDL_HasClipboardText();
                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) {
                        char* clipText = SDL_GetClipboardText();
                        if (clipText) {
                            toastMessage = "Pasted from clipboard";
                            toastTimer = 2.0f;
                            SDL_free(clipText);
                        }
                    }

                    // 3. Highlight-specific actions
                    if (contextMenuState.highlightIndex >= 0) {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Create Link to Highlighted Section")) {
                            std::string pageGuid = session.GetActivePage() ? session.GetActivePage()->guid : "";
                            std::string snippet = contextMenuState.highlightText;
                            if (snippet.length() > 50) snippet = snippet.substr(0, 47) + "...";

                            std::string link = "[Highlight: \"" + snippet + "\" (" + docTitle + ", Page " +
                                               std::to_string(targetPage + 1) + ")](folionote://page/" +
                                               pageGuid + "?pdfPage=" + std::to_string(targetPage + 1) + ")";
                            SDL_SetClipboardText(link.c_str());
                            toastMessage = "Copied link to highlighted section!";
                            toastTimer = 2.0f;
                        }

                        if (ImGui::MenuItem("Erase Highlight")) {
                            auto it = pageCache.find(targetPage);
                            if (it != pageCache.end() && contextMenuState.highlightIndex < static_cast<int>(it->second.textHighlights.size())) {
                                it->second.textHighlights.erase(it->second.textHighlights.begin() + contextMenuState.highlightIndex);
                                toastMessage = "Highlight erased";
                                toastTimer = 1.5f;
                            }
                        }

                        // Change Highlight Color Swatches
                        ImGui::Separator();
                        ImGui::TextColored(theme.colorTextMuted, "Change Color:");
                        const auto& hlPresets = GetHighlightColorPresets();
                        for (int c = 0; c < static_cast<int>(hlPresets.size()); ++c) {
                            if (c > 0) ImGui::SameLine(0.0f, 6.0f);
                            ImGui::PushID(c + 9200);
                            if (ImGui::ColorButton("##ChangeHlColor", hlPresets[c].swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(22.0f, 22.0f))) {
                                auto it = pageCache.find(targetPage);
                                if (it != pageCache.end() && contextMenuState.highlightIndex < static_cast<int>(it->second.textHighlights.size())) {
                                    it->second.textHighlights[contextMenuState.highlightIndex].color = currentInvertState ? hlPresets[c].darkColor : hlPresets[c].lightColor;
                                    activeHighlightColorIdx = c;
                                    toastMessage = std::string("Color changed to ") + hlPresets[c].name;
                                    toastTimer = 1.5f;
                                }
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", hlPresets[c].name);
                            }
                            ImGui::PopID();
                        }
                    }

                    // 4. Create link to this specific page in general
                    ImGui::Separator();
                    std::string pageLinkItem = "Create Link to Page " + std::to_string(targetPage + 1);
                    if (ImGui::MenuItem(pageLinkItem.c_str())) {
                        std::string pageGuid = session.GetActivePage() ? session.GetActivePage()->guid : "";
                        std::string link = "[" + docTitle + " - Page " + std::to_string(targetPage + 1) +
                                           "](folionote://page/" + pageGuid + "?pdfPage=" + std::to_string(targetPage + 1) + ")";
                        SDL_SetClipboardText(link.c_str());
                        toastMessage = "Copied link to Page " + std::to_string(targetPage + 1) + "!";
                        toastTimer = 2.0f;
                    }

                    // 5. Contextual actions for selected text
                    if (contextMenuState.hasTextSelection) {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Copy as Markdown")) {
                            auto it = pageCache.find(selectedPageIndex);
                            if (it != pageCache.end()) {
                                std::string md = it->second.textLayer.GetSelectedMarkdown(currentSelection);
                                SDL_SetClipboardText(md.c_str());
                                toastMessage = "Copied formatted Markdown to clipboard!";
                                toastTimer = 2.0f;
                            }
                        }

                        if (ImGui::MenuItem("Quote to Notes")) {
                            auto it = pageCache.find(selectedPageIndex);
                            if (it != pageCache.end()) {
                                std::string rawText = it->second.textLayer.GetSelectedText(currentSelection);
                                std::string quoteBlock = "> \"";
                                for (char c : rawText) {
                                    quoteBlock += c;
                                    if (c == '\n') quoteBlock += "> ";
                                }
                                quoteBlock += "\"\n\n— *[" + docTitle + ", Page " + std::to_string(selectedPageIndex + 1) + "]*";
                                SDL_SetClipboardText(quoteBlock.c_str());
                                toastMessage = "Copied quote with citation to clipboard!";
                                toastTimer = 2.0f;
                            }
                        }

                        // Highlight color palette picker for selected text
                        ImGui::Separator();
                        ImGui::TextColored(theme.colorTextMuted, "Highlight As:");
                        const auto& hlPresetsSel = GetHighlightColorPresets();
                        for (int c = 0; c < static_cast<int>(hlPresetsSel.size()); ++c) {
                            if (c > 0) ImGui::SameLine(0.0f, 6.0f);
                            ImGui::PushID(c + 9300);
                            bool isCurrentCol = (activeHighlightColorIdx == c);
                            ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoTooltip;
                            if (!isCurrentCol) flags |= ImGuiColorEditFlags_NoBorder;
                            if (ImGui::ColorButton("##HlColorPick", hlPresetsSel[c].swatch, flags, ImVec2(22.0f, 22.0f))) {
                                activeHighlightColorIdx = c;
                                auto it = pageCache.find(selectedPageIndex);
                                if (it != pageCache.end()) {
                                    auto boxes = it->second.textLayer.GetSelectionBoxes(currentSelection);
                                    std::string selText = it->second.textLayer.GetSelectedText(currentSelection);
                                    for (const auto& b : boxes) {
                                        TextHighlightSpan span;
                                        span.boundsMm = b;
                                        span.color = currentInvertState ? hlPresetsSel[c].darkColor : hlPresetsSel[c].lightColor;
                                        span.startChar = currentSelection.MinIdx();
                                        span.endChar = currentSelection.MaxIdx();
                                        span.text = selText;
                                        span.pageIndex = selectedPageIndex;
                                        it->second.textHighlights.push_back(span);
                                    }
                                    selectedPageIndex = -1;
                                    currentSelection.Clear();
                                    toastMessage = std::string("Highlighted in ") + hlPresetsSel[c].name;
                                    toastTimer = 1.5f;
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Highlight as %s", hlPresetsSel[c].name);
                            }
                            ImGui::PopID();
                        }

                        if (ImGui::MenuItem("Highlight Selected Text")) {
                            auto it = pageCache.find(selectedPageIndex);
                            if (it != pageCache.end()) {
                                auto boxes = it->second.textLayer.GetSelectionBoxes(currentSelection);
                                std::string selText = it->second.textLayer.GetSelectedText(currentSelection);
                                for (const auto& b : boxes) {
                                    TextHighlightSpan span;
                                    span.boundsMm = b;
                                    span.color = GetActiveHighlightColor(currentInvertState);
                                    span.startChar = currentSelection.MinIdx();
                                    span.endChar = currentSelection.MaxIdx();
                                    span.text = selText;
                                    span.pageIndex = selectedPageIndex;
                                    it->second.textHighlights.push_back(span);
                                }
                                selectedPageIndex = -1;
                                currentSelection.Clear();
                                toastMessage = "Text highlighted";
                                toastTimer = 1.5f;
                            }
                        }

                        if (ImGui::MenuItem("Clear Selection")) {
                            selectedPageIndex = -1;
                            currentSelection.Clear();
                        }
                    }

                    ImGui::EndPopup();
                }
            }

            // =========================================================================
            // MODAL DIALOG: RENAMING USER BOOKMARKS (THEMED WITH MODALTHEMESCOPE)
            // =========================================================================
            if (openBookmarkRenameModal) {
                ImGui::OpenPopup("RenameBookmarkModal##Pdf");
                openBookmarkRenameModal = false;
            }
            ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
            {
                ModalThemeScope modalScope(theme);
                if (ImGui::BeginPopupModal("RenameBookmarkModal##Pdf", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    float availW = ImGui::GetContentRegionAvail().x;

                    ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
                    ImGui::TextColored(theme.colorText, "Rename Bookmark");
                    ImGui::PopFont();
                    ImGui::Dummy(ImVec2(0.0f, 2.0f));

                    ImGui::TextColored(theme.colorTextMuted, "Enter a new descriptive title for this bookmark:");
                    ImGui::Dummy(ImVec2(0.0f, 2.0f));

                    ImGui::SetNextItemWidth(availW);
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere();
                    }
                    bool enterPressed = ImGui::InputText("##BmRenameInput", bookmarkRenameBuffer, sizeof(bookmarkRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    float saveBtnW = 90.0f;
                    float cancelBtnW = 86.0f;
                    float btnGap = 10.0f;
                    float rightAlignX = ImGui::GetCursorPosX() + availW - (saveBtnW + cancelBtnW + btnGap);
                    if (rightAlignX > ImGui::GetCursorPosX()) {
                        ImGui::SetCursorPosX(rightAlignX);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    bool saveClicked = ImGui::Button("Save", ImVec2(saveBtnW, 32.0f));
                    ImGui::PopStyleColor(3);

                    if (saveClicked || enterPressed) {
                        if (bookmarkRenameIndex >= 0 && bookmarkRenameIndex < static_cast<int>(userBookmarks.size())) {
                            if (strlen(bookmarkRenameBuffer) > 0) {
                                userBookmarks[bookmarkRenameIndex].title = bookmarkRenameBuffer;
                                SyncBookmarksToPage(session);
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine(0.0f, btnGap);
                    if (ImGui::Button("Cancel", ImVec2(cancelBtnW, 32.0f))) {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
            }

            // =========================================================================
            // 5. AUTO-VANISHING FLOATING HUD (PAGE INDICATOR, ZOOM)
            // =========================================================================
            // Sized generously (340x42px) with ample margin between the text and buttons
            // so buttons never overflow the pill container.
            if (hudInactivityTimer > 0.0f) {
                float hudAlpha = std::clamp(hudInactivityTimer / 0.4f, 0.0f, 1.0f);

                float hudW = 340.0f;
                float hudH = 42.0f;
                ImVec2 hudPos(contentX + (contentW - hudW) * 0.5f, origin.y + viewH - hudH - 16.0f);

                if (mousePos.x >= hudPos.x && mousePos.x <= hudPos.x + hudW &&
                    mousePos.y >= hudPos.y && mousePos.y <= hudPos.y + hudH) {
                    hudInactivityTimer = 2.5f;
                }

                uint8_t bgA = static_cast<uint8_t>(220 * hudAlpha);
                uint8_t borderA = static_cast<uint8_t>(180 * hudAlpha);
                uint8_t textA = static_cast<uint8_t>(255 * hudAlpha);

                dl->AddRectFilled(hudPos, ImVec2(hudPos.x + hudW, hudPos.y + hudH), IM_COL32(22, 25, 32, bgA), 21.0f);
                dl->AddRect(hudPos, ImVec2(hudPos.x + hudW, hudPos.y + hudH), IM_COL32(110, 120, 145, borderA), 21.0f, 0, 1.0f);

                // Left: Page status text
                char pageStatusStr[64];
                std::snprintf(pageStatusStr, sizeof(pageStatusStr), "%d / %d   •   %.0f%%",
                              activePageIndex + 1, totalPages, zoomScale * 100.0f);
                ImVec2 stSz = ImGui::CalcTextSize(pageStatusStr);
                dl->AddText(ImVec2(hudPos.x + 18.0f, hudPos.y + (hudH - stSz.y) * 0.5f),
                            IM_COL32(245, 248, 252, textA), pageStatusStr);

                // Right: Zoom controls (mathematically aligned from right margin)
                float btnW = 28.0f;
                float fitBtnW = 44.0f;
                float btnGap = 5.0f;
                float rightPad = 14.0f;
                float totalBtnGroupW = btnW + btnGap + btnW + btnGap + fitBtnW;
                float btnStartX = hudPos.x + hudW - rightPad - totalBtnGroupW;
                float btnStartY = hudPos.y + (hudH - 26.0f) * 0.5f;

                ImGui::SetCursorScreenPos(ImVec2(btnStartX, btnStartY));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.28f, 0.38f, 0.85f * hudAlpha));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.38f, 0.50f, 0.95f * hudAlpha));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, hudAlpha));

                if (ImGui::Button("-##ZoomOut", ImVec2(btnW, 26.0f))) {
                    SetZoomScale(zoomScale - 0.15f);
                }
                ImGui::SameLine(0.0f, btnGap);
                if (ImGui::Button("+##ZoomIn", ImVec2(btnW, 26.0f))) {
                    SetZoomScale(zoomScale + 0.15f);
                }
                ImGui::SameLine(0.0f, btnGap);
                if (ImGui::Button("Fit##ZoomFit", ImVec2(fitBtnW, 26.0f))) {
                    SetZoomScale(1.0f);
                }
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
            }

            // Toast notification popup
            if (toastTimer > 0.0f) {
                toastTimer -= io.DeltaTime;
                ImVec2 toastSz = ImGui::CalcTextSize(toastMessage.c_str());
                ImVec2 tPos(contentX + (contentW - toastSz.x - 30.0f) * 0.5f, origin.y + 20.0f);
                dl->AddRectFilled(tPos, ImVec2(tPos.x + toastSz.x + 30.0f, tPos.y + 32.0f),
                                  IM_COL32(46, 125, 50, 235), 6.0f);
                dl->AddText(ImVec2(tPos.x + 15.0f, tPos.y + 7.0f), IM_COL32(255, 255, 255, 255), toastMessage.c_str());
            }

            ImGui::End();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
};

} // namespace Folio
