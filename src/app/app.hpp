#pragma once
#include <SDL3/SDL.h>
// ANDROID: SDL_opengl.h is a desktop-only header (links against libGL / GLX).
// Android devices only support OpenGL ES. Include the GLES2 header instead,
// which covers both GLES2 and GLES3 function declarations.
#if defined(__ANDROID__)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"
#include "ui/components/tuning_overlay.hpp"
#include "app/theme_manager.hpp"
#include "app/window_state_manager.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/components/ribbon_bar.hpp"
#include "ui/components/modern_nav_panel.hpp"
#include "ui/components/debug_overlay.hpp"
#include "ui/components/dialogs.hpp"
#include "ui/views/notebook_hub.hpp"
#include "input/input_manager.hpp"
#include <chrono>
#include <thread>
#include <string>
#include <filesystem>

class Application {
public:
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    bool running = true;

    CanvasEngine canvas;
    WindowStateManager windowSM;
    InputManager inputManager;
    ThemeManager themeManager;
    DocumentSession session;

    RibbonBar ribbon;
    ModernNavPanel modernNav;
    NotebookHubView hubView;
    DebugOverlay devTelemetry;
    ThemeCustomizerModal themeModal;
    InkingTuningOverlay tuningStudio;

    AppViewMode currentView = AppViewMode::CanvasWorkspace;

    bool Init(const char* title = "FolioNote", int initialW = 1920, int initialH = 1080) {
        if (!SDL_Init(SDL_INIT_VIDEO)) return false;

#if defined(__ANDROID__)
        // ANDROID: Android devices do not support desktop OpenGL Core profile.
        // They only expose OpenGL ES. Request GLES 3.0 which gives us:
        // - VAOs (glBindVertexArray, used by ImGui)
        // - GL_RGBA8 internal texture format
        // - GL_TEXTURE_SWIZZLE_* for channel remapping
        // Without this, SDL_GL_CreateContext returns NULL and Init() fails.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window = SDL_CreateWindow(title, initialW, initialH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_Quit();
            return false;
        }

        glContext = SDL_GL_CreateContext(window);
        if (!glContext) {
            SDL_DestroyWindow(window);
            SDL_Quit();
            return false;
        }
        // VSync enabled (locks to 120Hz / 60Hz display refresh rate)
        SDL_GL_SetSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        FolioTheme::LoadModernFonts(io);
        themeManager.ApplyTheme(ThemePreset::FolioDark);
        themeManager.LoadFromJson("config/theme_custom.json");

        ImGui_ImplSDL3_InitForOpenGL(window, glContext);
#if defined(__ANDROID__)
        // ANDROID: ImGui's OpenGL3 backend compiles different shader code depending on
        // the GLSL version string passed here. "#version 300 es" selects the GLES3
        // shader path which avoids desktop-only features (e.g. layout(binding=..)).
        // Using "#version 330" on Android causes shader compilation to fail silently.
        ImGui_ImplOpenGL3_Init("#version 300 es");
#else
        ImGui_ImplOpenGL3_Init("#version 330");
#endif

        canvas.Init(initialW, initialH);
        // Pass the SDL window so WindowStateManager can toggle VSync during transitions
        windowSM.Init(initialW, initialH, window);
        inputManager.stateMachine.InitTiming();

        // ---------------------------------------------------------
        // DATABASE DIRECTORY INITIALIZATION
        // ---------------------------------------------------------
#if defined(__ANDROID__)
        // ANDROID: SDL_GetUserFolder() is not available on Android — it requires
        // platform-specific APIs (like MediaStore) that SDL3 does not abstract.
        // SDL_GetPrefPath() returns a guaranteed writable private app directory:
        //   e.g. /data/user/0/org.libsdl.app/files/
        // This directory is sandboxed to our app and persists across launches.
        const char* prefPath = SDL_GetPrefPath("UniversalFramework", "FolioNote");
        std::filesystem::path folioPath;
        if (prefPath) {
            folioPath = std::filesystem::path(prefPath);
        } else {
            folioPath = std::filesystem::path(".") / "FolioNote";
        }
#else
        // Ask SDL3 for the user's OS-specific Documents folder path
        const char* docsPath = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
        std::filesystem::path folioPath;

        if (docsPath) {
            // e.g., C:\Users\Name\Documents\FolioNote
            folioPath = std::filesystem::path(docsPath) / "FolioNote";
        } else {
            // Fallback if OS denies access (creates folder next to the .exe)
            folioPath = std::filesystem::path(".") / "FolioNote"; 
        }
#endif

#if !defined(__ANDROID__)
        // ANDROID: SDL_GetPrefPath already creates the directory on Android.
        // On desktop we must create it ourselves if it doesn't exist yet.
        std::error_code ec;
        if (!std::filesystem::exists(folioPath, ec)) {
            std::filesystem::create_directories(folioPath, ec);
        }
#endif

        // Initialize the SQLite session using this physical directory
        session.Init(folioPath.string());

        devTelemetry.LogEvent("FolioNote initialized.", LogCategory::System);
        return true;
    }
    // DEBUG ISOLATION RESULT: Level 1 (bare SDL/GL, no VSync, no ImGui, no canvas)
    // still froze on Intel Arc. Root cause CONFIRMED = Intel Arc OpenGL driver
    // stalls the calling thread during ANY window mode change (fullscreen/restore/resize)
    // regardless of what the application is doing. This is a driver-level bug.
    // Mitigation: use borderless-maximized windowed mode instead of true OS fullscreen.
    #define DEBUG_ISOLATION_LEVEL 5

    void Run() {
        ImGuiIO& io = ImGui::GetIO();

#if DEBUG_ISOLATION_LEVEL == 1
        // ---- LEVEL 1: Bare minimum. Just a black OpenGL window. ----
        // VSync OFF. No ImGui. No canvas. No state machine.
        SDL_GL_SetSwapInterval(0);
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
            }
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);
        }

#elif DEBUG_ISOLATION_LEVEL == 2
        // ---- LEVEL 2: Same as Level 1 but VSync ON. ----
        SDL_GL_SetSwapInterval(1);
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
            }
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);
        }

#elif DEBUG_ISOLATION_LEVEL == 3
        // ---- LEVEL 3: VSync ON + F11 fullscreen toggle. ----
        SDL_GL_SetSwapInterval(1);
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
                    bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(window, !fs);
                }
            }
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);
        }

#elif DEBUG_ISOLATION_LEVEL == 4
        // ---- LEVEL 4: VSync + Fullscreen + ImGui (no canvas). ----
        SDL_GL_SetSwapInterval(1);
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
                    bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_GL_SetSwapInterval(0); // drop vsync before toggle
                    SDL_SetWindowFullscreen(window, !fs);
                    SDL_GL_SetSwapInterval(1); // restore after
                }
            }
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::ShowDemoWindow(); // simplest possible ImGui content
            ImGui::Render();
            glViewport(0, 0, w, h);
            glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window);
        }

#else
        // ---- LEVEL 5: Full app (normal mode). ----
        // 120Hz frame budget: ~8.333 milliseconds (8,333,333 nanoseconds)
        constexpr uint64_t TARGET_FRAME_NS = 1000000000ULL / 120ULL;
        uint64_t lastRenderTimeNs = SDL_GetTicksNS();

        while (running) {

            int windowW, windowH;
            SDL_GetWindowSizeInPixels(window, &windowW, &windowH);
            if (windowW <= 0 || windowH <= 0) {
                SDL_Delay(10);
                continue;
            }

            auto& sm = inputManager.stateMachine;
            bool isActivelyDrawing = (sm.currentStylusState == StylusState::Engaged) ||
                                     (sm.ActiveDevice == DeviceType::Touch && sm.mouse.leftButton);

            bool needsHighRefresh = isActivelyDrawing ||
                                    sm.mouse.middleButton ||
                                    (windowSM.currentState != WindowState::Stable) ||
                                    canvas.isDirty ||
                                    canvas.liveLayer.HasActiveData() ||
                                    (modernNav.animatedTotalWidth != modernNav.GetTargetWidth()); // sidebar glide

            // 1. DRAIN HARDWARE INPUT: Process all pending input packets immediately at full digitizer speed (240Hz/360Hz/480Hz).
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_F3) devTelemetry.isVisible = !devTelemetry.isVisible;
                    else if (event.key.key == SDLK_F4) themeModal.isVisible = !themeModal.isVisible;
                    else if (event.key.key == SDLK_F5) tuningStudio.isVisible = !tuningStudio.isVisible;
                }

                inputManager.ProcessEvent(event, canvas, session, windowSM);
                if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_PEN_MOTION) {
                    devTelemetry.RecordInputPacket(static_cast<double>(SDL_GetTicks()) / 1000.0);
                }
            }
            if (!running) break;

            // 2. 120Hz FRAME PACER: Cap display presentation and full Blend2D rendering to 120Hz.
            uint64_t nowNs = SDL_GetTicksNS();
            uint64_t elapsedNs = nowNs - lastRenderTimeNs;

            if (elapsedNs < TARGET_FRAME_NS) {
                uint64_t remainingNs = TARGET_FRAME_NS - elapsedNs;
                int waitTimeoutMs = static_cast<int>(remainingNs / 1000000ULL);

                // When idle, sleep up to 16ms to save battery and conserve power
                if (!needsHighRefresh && waitTimeoutMs < 16) {
                    waitTimeoutMs = 16;
                }

                if (waitTimeoutMs > 0) {
                    if (SDL_WaitEventTimeout(&event, waitTimeoutMs)) {
                        ImGui_ImplSDL3_ProcessEvent(&event);
                        if (event.type == SDL_EVENT_QUIT) {
                            running = false;
                        }
                        else if (event.type == SDL_EVENT_KEY_DOWN) {
                            if (event.key.key == SDLK_F3) devTelemetry.isVisible = !devTelemetry.isVisible;
                            else if (event.key.key == SDLK_F4) themeModal.isVisible = !themeModal.isVisible;
                            else if (event.key.key == SDLK_F5) tuningStudio.isVisible = !tuningStudio.isVisible;
                        }

                        inputManager.ProcessEvent(event, canvas, session, windowSM);
                        if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_PEN_MOTION) {
                            devTelemetry.RecordInputPacket(static_cast<double>(SDL_GetTicks()) / 1000.0);
                        }
                    }
                }

                // Check again whether the 120Hz deadline has arrived
                nowNs = SDL_GetTicksNS();
                if (nowNs - lastRenderTimeNs < TARGET_FRAME_NS) {
                    continue; // Continue loop to ingest more input without redrawing prematurely
                }
            }

            lastRenderTimeNs = SDL_GetTicksNS();

            windowSM.Update();
            if (windowSM.currentState == WindowState::Minimized) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // =========================================================
            // WORKSPACE LAYOUT GEOMETRY
            // =========================================================
            float screenW = static_cast<float>(windowW);
            float screenH = static_cast<float>(windowH);

            if (currentView == AppViewMode::NotebookHub) {
                hubView.Render(0.0f, 0.0f, screenW, screenH, currentView);
            } else {
                // 1. TOP RIBBON BAR
                float ribbonH = ribbon.GetCurrentHeight();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(screenW, ribbonH));
                ImGuiWindowFlags ribbonFlags = ImGuiWindowFlags_NoTitleBar | 
                                               ImGuiWindowFlags_NoResize | 
                                               ImGuiWindowFlags_NoMove | 
                                               ImGuiWindowFlags_NoCollapse | 
                                               ImGuiWindowFlags_NoBringToFrontOnFocus;

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, themeManager.colorBg);
                ImGui::Begin("##RibbonPanel", nullptr, ribbonFlags);
                ribbon.Render(screenW, currentView, canvas, inputManager.stateMachine, themeManager);
                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();

                // 2. MODERN NAVIGATION SIDEBAR
                float contentY = ribbonH;
                float contentH = screenH - ribbonH;

                modernNav.Render(0.0f, contentY, contentH, session, canvas, themeManager);

                // 3. CANVAS WORKSPACE (FILLS EXACT REMAINDER)
                float navW = modernNav.GetTotalWidth();
                float canvasX = navW;
                float canvasW = screenW - navW;

                ImGui::SetNextWindowPos(ImVec2(canvasX, contentY));
                ImGui::SetNextWindowSize(ImVec2(canvasW, contentH));
                ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoTitleBar | 
                                               ImGuiWindowFlags_NoResize | 
                                               ImGuiWindowFlags_NoMove | 
                                               ImGuiWindowFlags_NoCollapse | 
                                               ImGuiWindowFlags_NoBringToFrontOnFocus;

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::Begin("##CanvasPanel", nullptr, canvasFlags);

                ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
                ImVec2 canvasSize = ImGui::GetContentRegionAvail();

                inputManager.stateMachine.canvasOriginX = canvasOrigin.x;
                inputManager.stateMachine.canvasOriginY = canvasOrigin.y;

                if (canvasSize.x > 10.0f && canvasSize.y > 10.0f) {
                    bool isHovered = ImGui::IsWindowHovered();
                    if (!isHovered && !canvas.liveLayer.HasActiveData() && sm.ActiveDevice == DeviceType::Mouse) {
                        sm.mouse.leftButton = false;
                    }

                    if (!windowSM.ShouldFreezeCanvasRender()) {
                        // Skip Resize() while the sidebar is animating.
                        // Calling Resize() every animation frame sets isDirty + needsFullRebake,
                        // triggering a full Blend2D canvas rebake (~60x per second) which is the
                        // source of the jitter. The existing texture is displayed scaled by ImGui's
                        // Image call for the ~80ms of animation — completely invisible in practice.
                        if (!modernNav.IsAnimating()) {
                            canvas.Resize(static_cast<int>(canvasSize.x), static_cast<int>(canvasSize.y));
                        }
                        std::vector<std::shared_ptr<CanvasObject>> visibleObjects = session.QueryVisible(canvas.GetViewport());
                        canvas.Render(visibleObjects);
                    }

                    if (canvas.glTexture != 0) {
                        ImVec2 uv1(1.0f, 1.0f);
                        if (canvas.allocatedCapacityW > 0 && canvas.allocatedCapacityH > 0) {
                            uv1.x = static_cast<float>(canvas.viewportW) / canvas.allocatedCapacityW;
                            uv1.y = static_cast<float>(canvas.viewportH) / canvas.allocatedCapacityH;
                        }
                        ImGui::Image((ImTextureID)(intptr_t)canvas.glTexture, canvasSize, ImVec2(0, 0), uv1);
                        inputManager.wasCanvasImageHovered = ImGui::IsItemHovered();
                    } else {
                        inputManager.wasCanvasImageHovered = false;
                    }

                    Point2D titleScreen = canvas.transform.WorldToScreen(80.0, 50.0);
                    float currentZoom = static_cast<float>(canvas.transform.zoom);
                    if (titleScreen.x > -600.0 && titleScreen.y > -200.0) {
                        ImGui::SetCursorPos(ImVec2(static_cast<float>(titleScreen.x), static_cast<float>(titleScreen.y)));
                        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
                        ImGui::PushItemWidth(500.0f * currentZoom);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_Text, themeManager.colorText);
                        if (ImGui::InputText("##PageTitleInput", canvas.pageTitle, sizeof(canvas.pageTitle))) {
                            canvas.isDirty = true;
                        }
                        ImGui::PopStyleColor(2);
                        ImGui::PopItemWidth();
                        ImGui::PopFont();

                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        float lineStartY = canvasOrigin.y + static_cast<float>(titleScreen.y) + (44.0f * currentZoom);
                        ImVec2 lineStart(canvasOrigin.x + static_cast<float>(titleScreen.x), lineStartY);
                        ImVec2 lineEnd(lineStart.x + (600.0f * currentZoom), lineStartY);
                        drawList->AddLine(lineStart, lineEnd, ImGui::ColorConvertFloat4ToU32(themeManager.colorBorder), 1.5f * currentZoom);

                        ImGui::SetCursorPos(ImVec2(static_cast<float>(titleScreen.x), static_cast<float>(titleScreen.y) + (48.0f * currentZoom)));
                        ImGui::PushStyleColor(ImGuiCol_Text, themeManager.colorTextMuted);
                        ImGui::Text("%s      %s", canvas.pageDateStr.c_str(), canvas.pageTimeStr.c_str());
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar();
            }

            // =========================================================
            // DIAGNOSTICS & MODAL OVERLAYS
            // =========================================================
            devTelemetry.Render(canvas, inputManager.stateMachine, windowSM, inputManager.stateMachine.canvasOriginX, inputManager.stateMachine.canvasOriginY);
            themeModal.Render(themeManager);
            tuningStudio.Render();

            ImGui::Render();
            glViewport(0, 0, windowW, windowH);
            glClearColor(themeManager.colorBg.x, themeManager.colorBg.y, themeManager.colorBg.z, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            SDL_GL_SwapWindow(window);
        }
#endif
    }

    void Shutdown() {
        // Automatically save the currently open notebook to SQLite when closing the app
        session.workspace.FlushActiveNotebookAsync();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        if (glContext) {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }
};