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

struct PdfInkPoint {
    float x = 0.0f; // mm
    float y = 0.0f; // mm
    float pressure = 1.0f;
};

struct PdfPageStroke {
    std::vector<PdfInkPoint> points;
    ImU32 color = IM_COL32(20, 20, 25, 255);
    float thicknessMm = 0.8f;
    bool isHighlighter = false;
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
    std::vector<PdfPageStroke> strokes;
    bool isLoaded = false;

    void DestroyTexture() {
        if (glTexture != 0) {
            glDeleteTextures(1, &glTexture);
            glTexture = 0;
        }
    }
};

class PdfViewerPage {
public:
    std::string currentPdfPath;
    int totalPages = 0;
    float scrollY = 0.0f;
    float maxScrollY = 0.0f;
    float zoomScale = 1.25f; // 1.25 = 125% zoom
    int activePageIndex = 0;

    // Page text selection state
    int selectedPageIndex = -1;
    PdfTextSelection currentSelection;
    bool isSelectingText = false;

    // Inking on PDF pages (strictly clamped to page boundaries)
    int activeInkingPageIndex = -1;
    PdfPageStroke activeStroke;

    // Page cache
    std::unordered_map<int, CachedPdfViewerPage> pageCache;

    // Toast notification for copy actions
    float toastTimer = 0.0f;
    std::string toastMessage;

    ~PdfViewerPage() {
        ClearCache();
    }

    void ClearCache() {
        for (auto& [idx, p] : pageCache) {
            p.DestroyTexture();
        }
        pageCache.clear();
    }

    void LoadDocument(const std::string& filePath) {
        if (currentPdfPath == filePath && totalPages > 0) return;

        ClearCache();
        currentPdfPath = filePath;
        scrollY = 0.0f;
        selectedPageIndex = -1;
        currentSelection.Clear();
        activeInkingPageIndex = -1;
        activeStroke.points.clear();

        PdfDocumentInfo info;
        if (PdfStorage::InspectPdf(filePath, info)) {
            totalPages = info.pageCount;
            LOG_INFO(PdfStorage, "PdfViewerPage loaded: '" + filePath + "' (" + std::to_string(totalPages) + " pages)");
        } else {
            totalPages = 0;
        }
    }

    CachedPdfViewerPage* GetOrLoadPage(int pageIdx, double targetDpi = 150.0) {
        auto it = pageCache.find(pageIdx);
        if (it != pageCache.end() && it->second.isLoaded) {
            return &it->second;
        }

        auto res = PdfRenderer::RenderPage(currentPdfPath, pageIdx, targetDpi);
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

    void Render(float viewX, float viewW, float screenH, float titleBarH, float ribbonH,
                DocumentSession& session, InputStateMachine& sm, const ThemeManager& theme) {
        auto activePage = session.GetActivePage();
        if (!activePage || !activePage->isDedicatedPdf) return;

        if (currentPdfPath != activePage->dedicatedPdfPath) {
            LoadDocument(activePage->dedicatedPdfPath);
        }
        if (totalPages <= 0) return;

        ImGuiIO& io = ImGui::GetIO();
        float viewTop = titleBarH + ribbonH;
        float viewH = screenH - viewTop;
        if (viewH <= 0.0f || viewW <= 0.0f) return;

        ImGui::SetNextWindowPos(ImVec2(viewX, viewTop));
        ImGui::SetNextWindowSize(ImVec2(viewW, viewH));
        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.colorSectionBg);

        if (ImGui::Begin("##DedicatedPdfViewerCanvas", nullptr, winFlags)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 origin = ImGui::GetCursorScreenPos();

            // 1. Mouse wheel scrolling
            if (ImGui::IsWindowHovered() && std::abs(io.MouseWheel) > 0.01f) {
                if (io.KeyCtrl) {
                    zoomScale = std::clamp(zoomScale + io.MouseWheel * 0.1f, 0.5f, 3.5f);
                } else {
                    scrollY = std::clamp(scrollY - io.MouseWheel * 80.0f, 0.0f, maxScrollY);
                }
            }

            // 2. Render centered document pages
            constexpr float PAGE_GAP_PX = 24.0f;
            float currentY = 20.0f - scrollY;
            float maxContentWidth = 0.0f;

            int currentVisiblePage = 0;
            ImVec2 selectedBoxMin(0.0f, 0.0f), selectedBoxMax(0.0f, 0.0f);
            bool hasSelectionRects = false;

            for (int i = 0; i < totalPages; ++i) {
                float pageW_px = 794.0f * (zoomScale / 1.0f);
                float pageH_px = 1123.0f * (zoomScale / 1.0f);

                // Check visibility against viewport window
                if (currentY + pageH_px > 0.0f && currentY < viewH) {
                    currentVisiblePage = i;
                    float pageX = (viewW - pageW_px) * 0.5f;
                    if (pageX < 20.0f) pageX = 20.0f;

                    ImVec2 pMin(origin.x + pageX, origin.y + currentY);
                    ImVec2 pMax(pMin.x + pageW_px, pMin.y + pageH_px);

                    // Drop shadow
                    dl->AddRectFilled(ImVec2(pMin.x + 3.0f, pMin.y + 4.0f), ImVec2(pMax.x + 3.0f, pMax.y + 4.0f),
                                      IM_COL32(0, 0, 0, 45), 4.0f);
                    // Page surface
                    dl->AddRectFilled(pMin, pMax, IM_COL32(255, 255, 255, 255), 2.0f);

                    // Load or retrieve rendered page bitmap from PDFium
                    CachedPdfViewerPage* cached = GetOrLoadPage(i, 150.0 * zoomScale);
                    if (cached && cached->glTexture != 0) {
                        dl->AddImage((ImTextureID)(intptr_t)cached->glTexture, pMin, pMax);

                        ImVec2 mousePos = io.MousePos;
                        bool isHovered = (mousePos.x >= pMin.x && mousePos.x <= pMax.x &&
                                          mousePos.y >= pMin.y && mousePos.y <= pMax.y);

                        double scaleX = cached->widthMm / pageW_px;
                        double scaleY = cached->heightMm / pageH_px;
                        double mouseLocalMmX = (mousePos.x - pMin.x) * scaleX;
                        double mouseLocalMmY = (mousePos.y - pMin.y) * scaleY;

                        // -------------------------------------------------------------
                        // 3A. INKING TOOL MODE (STRICT PAGE-BOUNDARY CLAMPING)
                        // -------------------------------------------------------------
                        if (sm.currentAction == InteractionState::Inking) {
                            if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                activeInkingPageIndex = i;
                                activeStroke.points.clear();

                                auto pen = sm.palette.GetActivePen();
                                BLRgba32 c = pen.color;
                                uint8_t alpha = (pen.penType == PenType::Highlighter) ? 90 : static_cast<uint8_t>(pen.color.a() * pen.opacity);
                                activeStroke.color = IM_COL32(c.r(), c.g(), c.b(), alpha);
                                activeStroke.thicknessMm = (pen.penType == PenType::Highlighter) ? pen.baseSize * 4.0f : pen.baseSize;
                                activeStroke.isHighlighter = (pen.penType == PenType::Highlighter);

                                float clampedX = std::clamp(static_cast<float>(mouseLocalMmX), 0.0f, static_cast<float>(cached->widthMm));
                                float clampedY = std::clamp(static_cast<float>(mouseLocalMmY), 0.0f, static_cast<float>(cached->heightMm));
                                activeStroke.points.push_back({ clampedX, clampedY, 1.0f });
                            }

                            if (activeInkingPageIndex == i && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                                // Clamp strictly to page boundary (cannot draw outside the page!)
                                float clampedX = std::clamp(static_cast<float>(mouseLocalMmX), 0.0f, static_cast<float>(cached->widthMm));
                                float clampedY = std::clamp(static_cast<float>(mouseLocalMmY), 0.0f, static_cast<float>(cached->heightMm));
                                activeStroke.points.push_back({ clampedX, clampedY, 1.0f });
                            }
                        }
                        // -------------------------------------------------------------
                        // 3B. ERASER TOOL MODE (ERASES INK STROKES ON THIS PAGE)
                        // -------------------------------------------------------------
                        else if (sm.currentAction == InteractionState::Eraser) {
                            if (isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                                float eraseRad = 6.0f; // 6mm eraser radius
                                auto& strks = cached->strokes;
                                strks.erase(std::remove_if(strks.begin(), strks.end(), [&](const PdfPageStroke& s) {
                                    for (const auto& pt : s.points) {
                                        float dx = pt.x - static_cast<float>(mouseLocalMmX);
                                        float dy = pt.y - static_cast<float>(mouseLocalMmY);
                                        if (dx * dx + dy * dy <= eraseRad * eraseRad) return true;
                                    }
                                    return false;
                                }), strks.end());
                            }
                        }
                        // -------------------------------------------------------------
                        // 3C. SELECTING / TEXT INTERACTION MODE
                        // -------------------------------------------------------------
                        else {
                            if (isHovered) {
                                int hitChar = cached->textLayer.HitTestChar(mouseLocalMmX, mouseLocalMmY, 3.5);
                                if (hitChar >= 0) {
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

                                if (isSelectingText && selectedPageIndex == i && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                                    if (hitChar >= 0) {
                                        currentSelection.endChar = hitChar;
                                        currentSelection.hasSelection = true;
                                    }
                                }
                            }
                        }

                        // -------------------------------------------------------------
                        // 3D. RENDER INK STROKES ON THIS PAGE (CLIPPED TO PAGE BOUNDS)
                        // -------------------------------------------------------------
                        dl->PushClipRect(pMin, pMax, true);

                        for (const auto& s : cached->strokes) {
                            if (s.points.size() < 2) continue;
                            float pxThick = std::max(1.5f, static_cast<float>(s.thicknessMm / scaleX));
                            for (size_t p = 0; p < s.points.size() - 1; ++p) {
                                ImVec2 pt0(pMin.x + static_cast<float>(s.points[p].x / scaleX),
                                           pMin.y + static_cast<float>(s.points[p].y / scaleY));
                                ImVec2 pt1(pMin.x + static_cast<float>(s.points[p + 1].x / scaleX),
                                           pMin.y + static_cast<float>(s.points[p + 1].y / scaleY));
                                dl->AddLine(pt0, pt1, s.color, pxThick);
                            }
                        }

                        if (activeInkingPageIndex == i && activeStroke.points.size() >= 2) {
                            float pxThick = std::max(1.5f, static_cast<float>(activeStroke.thicknessMm / scaleX));
                            for (size_t p = 0; p < activeStroke.points.size() - 1; ++p) {
                                ImVec2 pt0(pMin.x + static_cast<float>(activeStroke.points[p].x / scaleX),
                                           pMin.y + static_cast<float>(activeStroke.points[p].y / scaleY));
                                ImVec2 pt1(pMin.x + static_cast<float>(activeStroke.points[p + 1].x / scaleX),
                                           pMin.y + static_cast<float>(activeStroke.points[p + 1].y / scaleY));
                                dl->AddLine(pt0, pt1, activeStroke.color, pxThick);
                            }
                        }

                        // -------------------------------------------------------------
                        // 3E. RENDER TEXT SELECTION HIGHLIGHTS
                        // -------------------------------------------------------------
                        if (selectedPageIndex == i && currentSelection.hasSelection) {
                            auto boxes = cached->textLayer.GetSelectionBoxes(currentSelection);
                            for (const auto& b : boxes) {
                                float bx0 = pMin.x + static_cast<float>(b.minX / scaleX);
                                float by0 = pMin.y + static_cast<float>(b.minY / scaleY);
                                float bx1 = pMin.x + static_cast<float>(b.maxX / scaleX);
                                float by1 = pMin.y + static_cast<float>(b.maxY / scaleY);

                                dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), IM_COL32(33, 150, 243, 90), 2.0f);

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

                        dl->PopClipRect();
                    }

                    // Border outline
                    dl->AddRect(pMin, pMax, IM_COL32(200, 205, 215, 255), 2.0f, 0, 1.0f);

                    // Page number footer label
                    char pageNumStr[32];
                    std::snprintf(pageNumStr, sizeof(pageNumStr), "- %d -", i + 1);
                    ImVec2 lblSz = ImGui::CalcTextSize(pageNumStr);
                    dl->AddText(ImVec2(pMin.x + (pageW_px - lblSz.x) * 0.5f, pMax.y + 4.0f),
                                ImGui::GetColorU32(theme.colorTextMuted), pageNumStr);
                }

                if (pageW_px > maxContentWidth) maxContentWidth = pageW_px;
                currentY += (pageH_px + PAGE_GAP_PX);
            }

            activePageIndex = currentVisiblePage;
            maxScrollY = std::max(0.0f, (currentY + scrollY) - viewH + 60.0f);

            // Commit inking stroke on pointer up
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (activeInkingPageIndex >= 0 && !activeStroke.points.empty()) {
                    auto it = pageCache.find(activeInkingPageIndex);
                    if (it != pageCache.end()) {
                        it->second.strokes.push_back(activeStroke);
                    }
                    activeStroke.points.clear();
                    activeInkingPageIndex = -1;
                }
                isSelectingText = false;
            }

            // -------------------------------------------------------------
            // 4. FLOATING CONTEXT ACTION PILL FOR SELECTED TEXT
            // -------------------------------------------------------------
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
                    if (ImGui::SmallButton("Highlight")) {
                        auto it = pageCache.find(selectedPageIndex);
                        if (it != pageCache.end()) {
                            auto boxes = it->second.textLayer.GetSelectionBoxes(currentSelection);
                            for (const auto& b : boxes) {
                                PdfPageStroke hl;
                                hl.color = IM_COL32(255, 235, 59, 110); // Translucent yellow
                                hl.thicknessMm = static_cast<float>(b.maxY - b.minY);
                                hl.isHighlighter = true;
                                float midY = static_cast<float>((b.minY + b.maxY) * 0.5);
                                hl.points.push_back({ static_cast<float>(b.minX), midY, 1.0f });
                                hl.points.push_back({ static_cast<float>(b.maxX), midY, 1.0f });
                                it->second.strokes.push_back(hl);
                            }
                            selectedPageIndex = -1;
                            currentSelection.Clear();
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

            // -------------------------------------------------------------
            // 5. FLOATING HUD: PAGE INDICATOR & ZOOM CONTROLS
            // -------------------------------------------------------------
            float hudW = 280.0f;
            float hudH = 38.0f;
            ImVec2 hudPos(origin.x + (viewW - hudW) * 0.5f, origin.y + viewH - hudH - 16.0f);

            dl->AddRectFilled(hudPos, ImVec2(hudPos.x + hudW, hudPos.y + hudH), IM_COL32(25, 28, 36, 220), 19.0f);
            dl->AddRect(hudPos, ImVec2(hudPos.x + hudW, hudPos.y + hudH), IM_COL32(100, 110, 130, 180), 19.0f, 0, 1.0f);

            char pageStatusStr[64];
            std::snprintf(pageStatusStr, sizeof(pageStatusStr), "Page %d of %d  (%.0f%%)",
                          activePageIndex + 1, totalPages, zoomScale * 100.0f);
            ImVec2 stSz = ImGui::CalcTextSize(pageStatusStr);
            dl->AddText(ImVec2(hudPos.x + (hudW - stSz.x) * 0.5f, hudPos.y + (hudH - stSz.y) * 0.5f),
                        IM_COL32(245, 248, 252, 255), pageStatusStr);

            // Toast notification popup
            if (toastTimer > 0.0f) {
                toastTimer -= io.DeltaTime;
                ImVec2 toastSz = ImGui::CalcTextSize(toastMessage.c_str());
                ImVec2 tPos(origin.x + (viewW - toastSz.x - 30.0f) * 0.5f, origin.y + 20.0f);
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
