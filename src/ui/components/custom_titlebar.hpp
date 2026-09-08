#pragma once
#include <SDL3/SDL.h>
#include "imgui.h"
#include "app/theme_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/icon_manager.hpp"
#include <algorithm>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

class CustomTitleBar {
public:
    static constexpr float TITLEBAR_HEIGHT = 48.0f;
    static constexpr float CAPTION_BTN_WIDTH = 46.0f;
    bool isVisible = true;

#if defined(_WIN32)
    HWND hwnd = NULL;
    WNDPROC oldWndProc = NULL;
    static inline CustomTitleBar* s_instance = nullptr;
    bool isMaximizeHovered = false;
    bool isMaximizePressed = false;

    static LRESULT CALLBACK SubclassWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (!s_instance) return DefWindowProc(hWnd, uMsg, wParam, lParam);

        switch (uMsg) {
        case WM_NCHITTEST: {
            LRESULT lResult = 0;
            if (DwmDefWindowProc(hWnd, uMsg, wParam, lParam, &lResult)) {
                return lResult;
            }

            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int clientW = rcClient.right - rcClient.left;

            // Check if cursor is within Maximize/Resize button region:
            // X: [clientW - 92, clientW - 46), Y: [0, TITLEBAR_HEIGHT)
            if (pt.y >= 0 && pt.y < static_cast<int>(TITLEBAR_HEIGHT) &&
                pt.x >= (clientW - static_cast<int>(CAPTION_BTN_WIDTH * 2.0f)) &&
                pt.x < (clientW - static_cast<int>(CAPTION_BTN_WIDTH))) {
                return HTMAXBUTTON;
            }
            break;
        }

        case WM_NCMOUSEMOVE: {
            if (wParam == HTMAXBUTTON) {
                if (!s_instance->isMaximizeHovered) {
                    s_instance->isMaximizeHovered = true;
                    TRACKMOUSEEVENT tme;
                    tme.cbSize = sizeof(TRACKMOUSEEVENT);
                    tme.dwFlags = TME_NONCLIENT | TME_LEAVE;
                    tme.hwndTrack = hWnd;
                    TrackMouseEvent(&tme);
                }
            }
            break;
        }

        case WM_NCMOUSELEAVE: {
            s_instance->isMaximizeHovered = false;
            s_instance->isMaximizePressed = false;
            break;
        }

        case WM_NCLBUTTONDOWN: {
            if (wParam == HTMAXBUTTON) {
                s_instance->isMaximizePressed = true;
                return 0;
            }
            break;
        }

        case WM_NCLBUTTONUP: {
            if (wParam == HTMAXBUTTON) {
                s_instance->isMaximizePressed = false;
                if (IsZoomed(hWnd)) {
                    ShowWindow(hWnd, SW_RESTORE);
                } else {
                    ShowWindow(hWnd, SW_MAXIMIZE);
                }
                return 0;
            }
            break;
        }
        }

        return CallWindowProc(s_instance->oldWndProc, hWnd, uMsg, wParam, lParam);
    }

    void AttachWindow(SDL_Window* win) {
        s_instance = this;
        if (!hwnd && win) {
            SDL_PropertiesID props = SDL_GetWindowProperties(win);
            hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
            if (hwnd) {
                oldWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassWndProc);
            }
        }
    }

    void DetachWindow() {
        if (hwnd && oldWndProc) {
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)oldWndProc);
            oldWndProc = NULL;
            hwnd = NULL;
        }
        if (s_instance == this) s_instance = nullptr;
    }
#else
    void AttachWindow(SDL_Window*) {}
    void DetachWindow() {}
#endif

    ~CustomTitleBar() {
#if defined(_WIN32)
        DetachWindow();
#endif
    }

    static SDL_HitTestResult SDLCALL HitTestCallback(SDL_Window* win, const SDL_Point* area, void* data) {
        CustomTitleBar* self = static_cast<CustomTitleBar*>(data);
        if (!win || !area) return SDL_HITTEST_NORMAL;

        Uint32 flags = SDL_GetWindowFlags(win);
        if (flags & SDL_WINDOW_FULLSCREEN) return SDL_HITTEST_NORMAL;

        int w = 0, h = 0;
        SDL_GetWindowSize(win, &w, &h);

        const int BORDER = 6;

        // Resize handles along outer 6px borders (disabled when maximized)
        if (!(flags & SDL_WINDOW_MAXIMIZED)) {
            if (area->y < BORDER) {
                if (area->x < BORDER) return SDL_HITTEST_RESIZE_TOPLEFT;
                if (area->x >= w - BORDER) return SDL_HITTEST_RESIZE_TOPRIGHT;
                return SDL_HITTEST_RESIZE_TOP;
            }
            if (area->y >= h - BORDER) {
                if (area->x < BORDER) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
                if (area->x >= w - BORDER) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
                return SDL_HITTEST_RESIZE_BOTTOM;
            }
            if (area->x < BORDER) return SDL_HITTEST_RESIZE_LEFT;
            if (area->x >= w - BORDER) return SDL_HITTEST_RESIZE_RIGHT;
        }

        // Title bar drag region (taller 48px header)
        if (self && self->isVisible && area->y < static_cast<int>(TITLEBAR_HEIGHT)) {
            // Exclude window caption buttons on the right (3 buttons x 46px = 138px)
            if (area->x >= (w - static_cast<int>(CAPTION_BTN_WIDTH * 3.0f))) {
                return SDL_HITTEST_NORMAL;
            }
            // Exclude centered Overlays & Studios button (~180px)
            int midX = w / 2;
            int buttonHalfW = 90;
            if (area->x >= (midX - buttonHalfW) && area->x <= (midX + buttonHalfW)) {
                return SDL_HITTEST_NORMAL;
            }
            return SDL_HITTEST_DRAGGABLE;
        }

        return SDL_HITTEST_NORMAL;
    }

    void Render(
        SDL_Window* window,
        float screenW,
        bool& running,
        bool& outDevTelemetryVisible,
        bool& outThemeModalVisible,
        bool& outTuningStudioVisible,
        bool& outToolbarDemoVisible,
        const ThemeManager& theme,
        const std::string& notebookTitle
    ) {
        if (!isVisible || screenW <= 0.0f) return;

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(screenW, TITLEBAR_HEIGHT));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.colorBg);

        if (ImGui::Begin("##CustomTitleBar", nullptr, flags)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 winPos = ImGui::GetWindowPos();

            // 1. LEFT: App Logo & Document Title (Vertically Centered)
            GLuint logoTex = g_IconManager.LoadOrGetSVG("app_logo", "assets/icons/logo.svg", 128);
            const float logoSize = 30.0f;
            const float logoY = (TITLEBAR_HEIGHT - logoSize) * 0.5f;

            if (logoTex != 0) {
                drawList->AddImage(
                    (ImTextureID)(intptr_t)logoTex,
                    ImVec2(winPos.x + 12.0f, winPos.y + logoY),
                    ImVec2(winPos.x + 12.0f + logoSize, winPos.y + logoY + logoSize)
                );
            } else {
                // Fallback brand pill badge "FN"
                ImVec2 badgePos = ImVec2(winPos.x + 12.0f, winPos.y + logoY + 2.0f);
                drawList->AddRectFilled(
                    badgePos,
                    ImVec2(badgePos.x + 26.0f, badgePos.y + 26.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.22f)),
                    5.0f
                );
                drawList->AddText(ImVec2(badgePos.x + 4.0f, badgePos.y + 4.0f), 
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f)), "FN");
            }

            ImGui::SetCursorPos(ImVec2(50.0f, 13.0f));
            ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));
            std::string fullTitle = "FolioNote  •  " + (notebookTitle.empty() ? "Untitled Notebook" : notebookTitle);
            ImGui::TextUnformatted(fullTitle.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // 2. MIDDLE: [ ⚡ Overlays & Studios ▾ ] Capsule Button (Vertically Centered at Y=9px)
            float buttonW = 168.0f;
            float buttonH = 30.0f;
            float midX = (screenW - buttonW) * 0.5f;

            ImGui::SetCursorPos(ImVec2(midX, 9.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 15.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.32f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));

            bool anyOverlayActive = outDevTelemetryVisible || outThemeModalVisible || outTuningStudioVisible || outToolbarDemoVisible;
            const char* overlaysLabel = anyOverlayActive ? "* Overlays [ON] v" : "Overlays v";

            if (ImGui::Button(overlaysLabel, ImVec2(buttonW, buttonH))) {
                ImGui::OpenPopup("##OverlaysDropdownPopup");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open Diagnostics, Tuning Studios, Themes & Showcase Overlays");
            }

            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(2);

            // Overlays Popover Dropdown Menu
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.colorShelf);
            ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

            if (ImGui::BeginPopup("##OverlaysDropdownPopup")) {
                ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
                ImGui::TextUnformatted("Diagnostic & Studio Overlays");
                ImGui::PopFont();
                ImGui::Separator();

                if (ImGui::MenuItem("Developer Diagnostics", "F3", outDevTelemetryVisible)) {
                    outDevTelemetryVisible = !outDevTelemetryVisible;
                }
                if (ImGui::MenuItem("Appearance & Themes", "F4", outThemeModalVisible)) {
                    outThemeModalVisible = !outThemeModalVisible;
                }
                if (ImGui::MenuItem("Handwriting Tuning Studio", "F5", outTuningStudioVisible)) {
                    outTuningStudioVisible = !outTuningStudioVisible;
                }
                if (ImGui::MenuItem("Ribbon Customizer & Editor", "F6", outToolbarDemoVisible)) {
                    outToolbarDemoVisible = !outToolbarDemoVisible;
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Hide All Overlays", "Esc")) {
                    outDevTelemetryVisible = false;
                    outThemeModalVisible   = false;
                    outTuningStudioVisible = false;
                    outToolbarDemoVisible  = false;
                }
                if (ImGui::MenuItem("Show All Overlays")) {
                    outDevTelemetryVisible = true;
                    outThemeModalVisible   = true;
                    outTuningStudioVisible = true;
                    outToolbarDemoVisible  = true;
                }

                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            // 3. RIGHT: Window Caption Buttons (Minimize, Maximize/Restore, Close)
            // Completely square, full bleed to the top and right boundaries (46x48px each)
            Uint32 winFlags = SDL_GetWindowFlags(window);
            bool isMaximized = (winFlags & SDL_WINDOW_MAXIMIZED) != 0;

            const float btnW = CAPTION_BTN_WIDTH;
            const float btnH = TITLEBAR_HEIGHT;
            const ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f));

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.32f));

            // --- MINIMIZE BUTTON ---
            float minX = screenW - (btnW * 3.0f);
            ImGui::SetCursorPos(ImVec2(minX, 0.0f));
            if (ImGui::Button("##CaptionMinimize", ImVec2(btnW, btnH))) {
                SDL_MinimizeWindow(window);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimize");

            // Mathematically centered horizontal line icon (11px wide, 1.5px thick)
            float minCx = minX + (btnW * 0.5f);
            float minCy = btnH * 0.5f;
            drawList->AddLine(ImVec2(minCx - 5.5f, minCy), ImVec2(minCx + 5.5f, minCy), iconColor, 1.5f);

            // --- MAXIMIZE / RESTORE BUTTON ---
            float maxX = screenW - (btnW * 2.0f);
            ImGui::SetCursorPos(ImVec2(maxX, 0.0f));

#if defined(_WIN32)
            // On Windows 11, hover is detected via HTMAXBUTTON and Snap Layouts
            bool maxHovered = isMaximizeHovered || ImGui::IsItemHovered();
            bool maxPressed = isMaximizePressed;
            if (maxHovered) {
                drawList->AddRectFilled(
                    ImVec2(winPos.x + maxX, winPos.y),
                    ImVec2(winPos.x + maxX + btnW, winPos.y + btnH),
                    ImGui::ColorConvertFloat4ToU32(maxPressed ? ImVec4(1.0f, 1.0f, 1.0f, 0.32f) : ImVec4(1.0f, 1.0f, 1.0f, 0.18f))
                );
            }
#endif
            if (ImGui::Button("##CaptionMaximize", ImVec2(btnW, btnH))) {
                if (isMaximized) {
                    SDL_RestoreWindow(window);
                } else {
                    SDL_MaximizeWindow(window);
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(isMaximized ? "Restore" : "Maximize");

            // Mathematically centered vector glyph
            float maxCx = maxX + (btnW * 0.5f);
            float maxCy = btnH * 0.5f;
            if (isMaximized) {
                // Restore: Two overlapping squares (8x8px)
                drawList->AddLine(ImVec2(maxCx - 2.0f, maxCy - 5.0f), ImVec2(maxCx + 5.0f, maxCy - 5.0f), iconColor, 1.5f);
                drawList->AddLine(ImVec2(maxCx + 5.0f, maxCy - 5.0f), ImVec2(maxCx + 5.0f, maxCy + 2.0f), iconColor, 1.5f);
                drawList->AddLine(ImVec2(maxCx + 5.0f, maxCy + 2.0f), ImVec2(maxCx + 2.0f, maxCy + 2.0f), iconColor, 1.5f);
                drawList->AddLine(ImVec2(maxCx - 2.0f, maxCy - 5.0f), ImVec2(maxCx - 2.0f, maxCy - 2.0f), iconColor, 1.5f);
                drawList->AddRect(ImVec2(maxCx - 5.0f, maxCy - 2.0f), ImVec2(maxCx + 2.0f, maxCy + 5.0f), iconColor, 0.0f, 0, 1.5f);
            } else {
                // Maximize: Single crisp square outline (10x10px)
                drawList->AddRect(ImVec2(maxCx - 5.0f, maxCy - 5.0f), ImVec2(maxCx + 5.0f, maxCy + 5.0f), iconColor, 0.0f, 0, 1.5f);
            }

            // --- CLOSE BUTTON ---
            float closeX = screenW - btnW;
            ImGui::SetCursorPos(ImVec2(closeX, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));

            if (ImGui::Button("##CaptionClose", ImVec2(btnW, btnH))) {
                SDL_Event quitEv;
                quitEv.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quitEv);
                running = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");

            ImGui::PopStyleColor(2);

            // Mathematically centered 'X' icon (10x10px)
            float closeCx = closeX + (btnW * 0.5f);
            float closeCy = btnH * 0.5f;
            drawList->AddLine(ImVec2(closeCx - 5.0f, closeCy - 5.0f), ImVec2(closeCx + 5.0f, closeCy + 5.0f), iconColor, 1.5f);
            drawList->AddLine(ImVec2(closeCx - 5.0f, closeCy + 5.0f), ImVec2(closeCx + 5.0f, closeCy - 5.0f), iconColor, 1.5f);

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);

            // Seamless color match: No divider line! Title bar and ribbon merge into one continuous block.
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
};

