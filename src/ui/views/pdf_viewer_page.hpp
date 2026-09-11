#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <cmath>
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

namespace Folio {

struct TextHighlightSpan {
    AABB boundsMm;
    ImU32 color = IM_COL32(255, 235, 59, 110);
    int startChar = -1;
    int endChar = -1;
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
    Select
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

    // Active tool mode (Highlight is dual text-snapping highlighter/eraser)
    PdfToolMode activeTool = PdfToolMode::Highlight;

    // Sidebar on the RIGHT side
    bool isSidebarOpen = true;
    float sidebarWidth = 230.0f;
    constexpr static float COLLAPSE_STRIP_WIDTH = 26.0f;
    PdfSidebarTab activeSidebarTab = PdfSidebarTab::Thumbnails;

    // Auto-vanishing bottom HUD
    float hudInactivityTimer = 2.5f;

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

    // Caches & Dimensions
    std::vector<PdfPageDimension> pageDimensions;
    std::unordered_map<int, CachedPdfViewerPage> pageCache;
    std::unordered_map<int, CachedThumbnail> thumbnailCache;

    // Toast notification for copy actions
    float toastTimer = 0.0f;
    std::string toastMessage;

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
    }

    void LoadDocument(const std::string& filePath) {
        if (currentPdfPath == filePath && totalPages > 0) return;

        ClearCache();
        currentPdfPath = filePath;
        scrollY = 0.0f;
        selectedPageIndex = -1;
        currentSelection.Clear();
        hudInactivityTimer = 2.5f;

        PdfDocumentInfo info;
        if (PdfStorage::InspectPdf(filePath, info)) {
            totalPages = info.pageCount;
            pageDimensions.resize(totalPages);
            for (int p = 0; p < totalPages; ++p) {
                PdfRenderer::GetPageDimensions(filePath, p, pageDimensions[p].widthMm, pageDimensions[p].heightMm);
            }
            LOG_INFO(PdfStorage, "PdfViewerPage loaded: '" + filePath + "' (" + std::to_string(totalPages) + " pages)");
        } else {
            totalPages = 0;
        }

        // Load outline
        docOutline = PdfRenderer::LoadOutline(filePath);
        isOutlineLoaded = true;
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
        hudInactivityTimer = 2.5f;
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

    void Render(float viewX, float viewW, float screenH, float titleBarH, float ribbonH,
                DocumentSession& session, InputStateMachine& sm, const ThemeManager& theme,
                bool invertCanvas = false) {
        auto activePage = session.GetActivePage();
        if (!activePage || !activePage->isDedicatedPdf) return;

        if (currentPdfPath != activePage->dedicatedPdfPath) {
            LoadDocument(activePage->dedicatedPdfPath);
        }
        if (totalPages <= 0) return;

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
        float viewTop = titleBarH + ribbonH;
        float viewH = screenH - viewTop;
        if (viewH <= 0.0f || viewW <= 0.0f) return;

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

            // Trigger HUD visibility on bottom hover
            if (isMouseInContent && mousePos.y > origin.y + viewH - 65.0f) {
                hudInactivityTimer = 2.5f;
            }

            // Zoom Anchoring towards Mouse Cursor (Eliminates page drift during Ctrl+Wheel)
            if (isMouseInContent && std::abs(io.MouseWheel) > 0.01f) {
                hudInactivityTimer = 2.5f;
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

                            // Hovering an existing highlight indicates eraser action
                            if (isHovered && mouseLocalMmX >= (hl.boundsMm.minX - 0.5) && mouseLocalMmX <= (hl.boundsMm.maxX + 0.5) &&
                                mouseLocalMmY >= (hl.boundsMm.minY - 0.5) && mouseLocalMmY <= (hl.boundsMm.maxY + 0.5)) {
                                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                    delHlIdx = static_cast<int>(h);
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

            // Pointer release handling
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (isSelectingText && currentSelection.hasSelection && selectedPageIndex >= 0) {
                    if (activeTool == PdfToolMode::Highlight) {
                        auto it = pageCache.find(selectedPageIndex);
                        if (it != pageCache.end()) {
                            auto boxes = it->second.textLayer.GetSelectionBoxes(currentSelection);
                            for (const auto& b : boxes) {
                                TextHighlightSpan span;
                                span.boundsMm = b;
                                span.color = currentInvertState ? IM_COL32(0, 230, 255, 110) : IM_COL32(255, 235, 59, 110);
                                span.startChar = currentSelection.MinIdx();
                                span.endChar = currentSelection.MaxIdx();
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
                    hudInactivityTimer = 2.5f;
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

                // Expand button [ < ]
                ImGui::SetCursorScreenPos(ImVec2(sidebarX + 3.0f, origin.y + 12.0f));
                if (ImGui::Button("<##ExpandSb", ImVec2(COLLAPSE_STRIP_WIDTH - 6.0f, 26.0f))) {
                    isSidebarOpen = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Expand Sidebar (Thumbnails, Outline, Bookmarks)");
                }
            } else {
                // Expanded Sidebar on right
                ImVec2 sbMin(sidebarX, origin.y);
                ImVec2 sbMax(sidebarX + sidebarWidth, origin.y + viewH);
                ImU32 sbBg = currentInvertState ? IM_COL32(20, 22, 28, 255) : ImGui::GetColorU32(theme.colorPanel);
                dl->AddRectFilled(sbMin, sbMax, sbBg);
                dl->AddLine(ImVec2(sbMin.x, sbMin.y), ImVec2(sbMin.x, sbMax.y), ImGui::GetColorU32(theme.colorBorder));

                // Sidebar Header: Collapse Button + Segmented Tabs
                ImGui::SetCursorScreenPos(ImVec2(sidebarX + 6.0f, origin.y + 8.0f));

                // Collapse Button [ > ]
                if (ImGui::Button(">##CollapseSb", ImVec2(22.0f, 24.0f))) {
                    isSidebarOpen = false;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Collapse Sidebar");
                }

                ImGui::SameLine(0.0f, 6.0f);
                float tabBtnW = (sidebarWidth - 44.0f) / 3.0f;

                // Tab 1: Pages (Thumbnails)
                bool isTabPages = (activeSidebarTab == PdfSidebarTab::Thumbnails);
                if (isTabPages) ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                if (ImGui::Button("Pages", ImVec2(tabBtnW, 24.0f))) {
                    activeSidebarTab = PdfSidebarTab::Thumbnails;
                }
                if (isTabPages) ImGui::PopStyleColor();

                // Tab 2: Outline
                ImGui::SameLine(0.0f, 2.0f);
                bool isTabOutline = (activeSidebarTab == PdfSidebarTab::Outline);
                if (isTabOutline) ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                if (ImGui::Button("Tree", ImVec2(tabBtnW, 24.0f))) {
                    activeSidebarTab = PdfSidebarTab::Outline;
                }
                if (isTabOutline) ImGui::PopStyleColor();

                // Tab 3: Bookmarks
                ImGui::SameLine(0.0f, 2.0f);
                bool isTabBm = (activeSidebarTab == PdfSidebarTab::Bookmarks);
                if (isTabBm) ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                if (ImGui::Button("Marks", ImVec2(tabBtnW, 24.0f))) {
                    activeSidebarTab = PdfSidebarTab::Bookmarks;
                }
                if (isTabBm) ImGui::PopStyleColor();

                // Divider line below header
                dl->AddLine(ImVec2(sbMin.x, origin.y + 38.0f), ImVec2(sbMax.x, origin.y + 38.0f), ImGui::GetColorU32(theme.colorBorder));

                // Sidebar Tab Content Area
                ImGui::SetCursorScreenPos(ImVec2(sidebarX + 4.0f, origin.y + 44.0f));
                ImGui::BeginChild("##PdfSidebarChild", ImVec2(sidebarWidth - 8.0f, viewH - 48.0f), false,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar);

                // --- TAB 1: MINI PAGE THUMBNAILS ---
                if (activeSidebarTab == PdfSidebarTab::Thumbnails) {
                    float thumbTargetW = sidebarWidth - 36.0f;
                    if (thumbTargetW < 120.0f) thumbTargetW = 120.0f;

                    for (int i = 0; i < totalPages; ++i) {
                        double tW = (i < static_cast<int>(pageDimensions.size())) ? pageDimensions[i].widthMm : 210.0;
                        double tH = (i < static_cast<int>(pageDimensions.size())) ? pageDimensions[i].heightMm : 297.0;
                        float thumbH = thumbTargetW * static_cast<float>(tH / tW);

                        ImVec2 curPos = ImGui::GetCursorScreenPos();
                        ImVec2 cardMin(curPos.x + 4.0f, curPos.y + 2.0f);
                        ImVec2 cardMax(cardMin.x + thumbTargetW, cardMin.y + thumbH);

                        bool isActive = (activePageIndex == i);

                        dl->AddRectFilled(ImVec2(cardMin.x + 2.0f, cardMin.y + 2.0f),
                                          ImVec2(cardMax.x + 2.0f, cardMax.y + 2.0f), IM_COL32(0, 0, 0, 60), 3.0f);
                        dl->AddRectFilled(cardMin, cardMax, currentInvertState ? IM_COL32(24, 26, 32, 255) : IM_COL32(250, 250, 252, 255), 3.0f);

                        CachedThumbnail* thumb = GetOrLoadThumbnail(i, currentInvertState);
                        if (thumb && thumb->glTexture != 0) {
                            dl->AddImage((ImTextureID)(intptr_t)thumb->glTexture, cardMin, cardMax);
                        }

                        if (isActive) {
                            dl->AddRect(cardMin, cardMax, ImGui::GetColorU32(theme.colorPrimary), 3.0f, 0, 2.5f);
                        } else {
                            dl->AddRect(cardMin, cardMax, ImGui::GetColorU32(theme.colorBorder), 3.0f, 0, 1.0f);
                        }

                        ImGui::InvisibleButton(("##ThumbBtn_" + std::to_string(i)).c_str(), ImVec2(thumbTargetW + 8.0f, thumbH + 4.0f));
                        if (ImGui::IsItemClicked()) {
                            ScrollToPage(i);
                        }

                        char pNumStr[32];
                        std::snprintf(pNumStr, sizeof(pNumStr), "Page %d", i + 1);
                        ImVec2 pNumSz = ImGui::CalcTextSize(pNumStr);
                        float lblX = cardMin.x + (thumbTargetW - pNumSz.x) * 0.5f;
                        dl->AddText(ImVec2(lblX, cardMax.y + 3.0f),
                                    isActive ? ImGui::GetColorU32(theme.colorPrimary) : ImGui::GetColorU32(theme.colorTextMuted), pNumStr);

                        ImGui::Dummy(ImVec2(0.0f, 18.0f));
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

                            ImGui::SameLine();
                            if (ImGui::SmallButton("x")) {
                                delIdx = static_cast<int>(b);
                            }
                            ImGui::PopID();
                        }

                        if (delIdx >= 0 && delIdx < static_cast<int>(userBookmarks.size())) {
                            userBookmarks.erase(userBookmarks.begin() + delIdx);
                        }
                    }
                }

                ImGui::EndChild();
            }

            // =========================================================================
            // 4. FLOATING CONTEXT ACTION PILL FOR SELECTED TEXT
            // =========================================================================
            if (hasSelectionRects && selectedPageIndex >= 0 && currentSelection.hasSelection) {
                ImVec2 pillPos(selectedBoxMin.x, selectedBoxMin.y - 42.0f);
                if (pillPos.y < origin.y + 10.0f) pillPos.y = selectedBoxMax.y + 10.0f;

                ImGui::SetNextWindowPos(pillPos, ImGuiCond_Always);
                ImGuiWindowFlags pillFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoSavedSettings;

                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.14f, 0.18f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.38f, 0.45f, 0.80f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));

                if (ImGui::Begin("##PdfSelectionPill", nullptr, pillFlags)) {
                    if (ImGui::SmallButton("Copy Text")) {
                        auto it = pageCache.find(selectedPageIndex);
                        if (it != pageCache.end()) {
                            std::string text = it->second.textLayer.GetSelectedText(currentSelection);
                            SDL_SetClipboardText(text.c_str());
                            toastMessage = "Copied text to clipboard!";
                            toastTimer = 2.0f;
                            LOG_INFO(PdfStorage, "Copied " + std::to_string(text.size()) + " chars of text.");
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy as Markdown")) {
                        auto it = pageCache.find(selectedPageIndex);
                        if (it != pageCache.end()) {
                            std::string md = it->second.textLayer.GetSelectedMarkdown(currentSelection);
                            SDL_SetClipboardText(md.c_str());
                            toastMessage = "Copied formatted Markdown to clipboard!";
                            toastTimer = 2.0f;
                            LOG_INFO(PdfStorage, "Copied " + std::to_string(md.size()) + " chars as Markdown.");
                        }
                    }
                    ImGui::SameLine();
                    // Text-snapping highlight tool
                    if (ImGui::SmallButton("Highlight Text")) {
                        auto it = pageCache.find(selectedPageIndex);
                        if (it != pageCache.end()) {
                            auto boxes = it->second.textLayer.GetSelectionBoxes(currentSelection);
                            for (const auto& b : boxes) {
                                TextHighlightSpan span;
                                span.boundsMm = b;
                                span.color = currentInvertState ? IM_COL32(0, 230, 255, 110) : IM_COL32(255, 235, 59, 110);
                                it->second.textHighlights.push_back(span);
                            }
                            selectedPageIndex = -1;
                            currentSelection.Clear();
                            toastMessage = "Text highlighted";
                            toastTimer = 1.5f;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Deselect")) {
                        selectedPageIndex = -1;
                        currentSelection.Clear();
                    }
                    ImGui::End();
                }
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
            }

            // =========================================================================
            // 5. AUTO-VANISHING FLOATING HUD (PAGE INDICATOR & ZOOM CONTROLS)
            // =========================================================================
            if (hudInactivityTimer > 0.0f) {
                float hudAlpha = std::clamp(hudInactivityTimer / 0.4f, 0.0f, 1.0f);

                float hudW = 310.0f;
                float hudH = 38.0f;
                ImVec2 hudPos(contentX + (contentW - hudW) * 0.5f, origin.y + viewH - hudH - 16.0f);

                if (mousePos.x >= hudPos.x && mousePos.x <= hudPos.x + hudW &&
                    mousePos.y >= hudPos.y && mousePos.y <= hudPos.y + hudH) {
                    hudInactivityTimer = 2.5f;
                }

                uint8_t bgA = static_cast<uint8_t>(220 * hudAlpha);
                uint8_t borderA = static_cast<uint8_t>(180 * hudAlpha);
                uint8_t textA = static_cast<uint8_t>(255 * hudAlpha);

                dl->AddRectFilled(hudPos, ImVec2(hudPos.x + hudW, hudPos.y + hudH), IM_COL32(25, 28, 36, bgA), 19.0f);
                dl->AddRect(hudPos, ImVec2(hudPos.x + hudW, hudPos.y + hudH), IM_COL32(100, 110, 130, borderA), 19.0f, 0, 1.0f);

                char pageStatusStr[64];
                std::snprintf(pageStatusStr, sizeof(pageStatusStr), "Page %d of %d  (%.0f%%)",
                              activePageIndex + 1, totalPages, zoomScale * 100.0f);
                ImVec2 stSz = ImGui::CalcTextSize(pageStatusStr);
                dl->AddText(ImVec2(hudPos.x + 20.0f, hudPos.y + (hudH - stSz.y) * 0.5f),
                            IM_COL32(245, 248, 252, textA), pageStatusStr);

                ImGui::SetCursorScreenPos(ImVec2(hudPos.x + hudW - 110.0f, hudPos.y + 6.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.25f, 0.35f, 0.8f * hudAlpha));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, hudAlpha));

                if (ImGui::SmallButton("-##ZoomOut")) {
                    SetZoomScale(zoomScale - 0.15f);
                }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::SmallButton("+##ZoomIn")) {
                    SetZoomScale(zoomScale + 0.15f);
                }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::SmallButton("Fit##ZoomFit")) {
                    SetZoomScale(1.0f);
                }
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
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
