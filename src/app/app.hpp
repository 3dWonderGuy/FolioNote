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
#include "ui/components/toolbar_demo_overlay.hpp"
#include "ui/components/pdf_import_modal.hpp"
#include "app/settings_manager.hpp"
#include "app/theme_manager.hpp"
#include "app/window_state_manager.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/components/ribbon_bar.hpp"
#include "ui/components/modern_nav_panel.hpp"
#include "ui/components/debug_overlay.hpp"
#include "ui/components/custom_titlebar.hpp"
#include "ui/views/notebook_hub.hpp"
#include "ui/views/pdf_viewer_page.hpp"
#include "input/input_manager.hpp"
#include "utils/file_loader.hpp"
#include "utils/usage_tracker.hpp"
#include "app/theme_manager.hpp"
#include <lunasvg.h>
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

    CustomTitleBar customTitleBar;
    RibbonBar ribbon;
    ModernNavPanel modernNav;
    NotebookHubView hubView;
    DebugOverlay devTelemetry;
    InkingTuningOverlay tuningStudio;
    ToolbarDemoOverlay toolbarDemo;
    Folio::PdfImportModal pdfImportModal;
    Folio::PdfViewerPage pdfViewer;

    AppViewMode currentView = AppViewMode::CanvasWorkspace;

    /**
     * @brief Stored world coordinates (in millimeters) where the user right-clicked on the infinite canvas.
     * 
     * Mathematical derivation:
     *   P_world = ScreenToWorld(P_screen) = (P_screen / (DPI * Zoom)) - Pan
     * This world position serves as the insertion origin for clipboard text paste operations
     * or context-dependent annotations spawned from the right-click menu.
     */
    Point2D generalContextMenuWorldPos{0.0, 0.0};

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

        window = SDL_CreateWindow(title, initialW, initialH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) {
            SDL_Quit();
            return false;
        }

        // Enable native window dragging & edge resizing for custom software titlebar
        SDL_SetWindowHitTest(window, CustomTitleBar::HitTestCallback, &customTitleBar);
        customTitleBar.AttachWindow(window);
        canvas.sdlWindow = window;

#if defined(_WIN32)
        // Set Win32 Taskbar and Window Icon directly on the HWND from compiled resource
        SDL_PropertiesID winProps = SDL_GetWindowProperties(window);
        HWND hwnd = (HWND)SDL_GetPointerProperty(winProps, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hwnd) {
            HINSTANCE hInst = GetModuleHandle(NULL);
            HICON hIconBig = (HICON)LoadImage(hInst, MAKEINTRESOURCE(1), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
            if (!hIconBig) {
                hIconBig = (HICON)LoadImage(hInst, MAKEINTRESOURCE(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            }
            HICON hIconSmall = (HICON)LoadImage(hInst, MAKEINTRESOURCE(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
            if (hIconBig) SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
            if (hIconSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        }
#endif

        // Set OS Window Icon from logo SVG across candidate search paths
        std::vector<std::string> iconCandidates = {
            "assets/icons/logo.svg",
            "icons/logo.svg",
            "../assets/icons/logo.svg",
            "../../assets/icons/logo.svg",
            "build/bin/assets/icons/logo.svg"
        };
        std::vector<uint8_t> logoSvgBytes;
        for (const auto& path : iconCandidates) {
            if (FileLoader::Exists(path) && FileLoader::ReadToBuffer(path, logoSvgBytes)) {
                auto doc = lunasvg::Document::loadFromData(reinterpret_cast<const char*>(logoSvgBytes.data()), logoSvgBytes.size());
                if (doc) {
                    lunasvg::Bitmap bm = doc->renderToBitmap(128, 128);
                    if (bm.valid()) {
                        SDL_Surface* iconSurf = SDL_CreateSurfaceFrom(
                            bm.width(), bm.height(),
                            SDL_PIXELFORMAT_ARGB8888,
                            bm.data(),
                            bm.stride()
                        );
                        if (iconSurf) {
                            SDL_SetWindowIcon(window, iconSurf);
                            SDL_DestroySurface(iconSurf);
                        }
                    }
                }
                break;
            }
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
        themeManager.ApplyTheme(ThemePreset::FolioColor);
        themeManager.LoadFromJson("config/theme_custom.json");
        ::Folio::UsageTracker::Instance().LoadFromJson("config/usage_stats.json");
        themeManager.UpdateOSWindowFrame(window);
        canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
        canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);

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

        // Load persistent JSON settings
        SettingsManager::Instance().Load();

        // Sync loaded settings to RibbonBar and InputStateMachine
        ribbon.drawWithTouch = SettingsManager::Instance().drawWithTouch;
        ribbon.rulerEnabled = SettingsManager::Instance().rulerEnabled;
        ribbon.autoShapesEnabled = SettingsManager::Instance().autoShapesEnabled;
        ribbon.isStrokeEraser = SettingsManager::Instance().isStrokeEraser;
        ribbon.eraserSizeMm = SettingsManager::Instance().eraserSizeMm;
        inputManager.stateMachine.isStrokeEraser = SettingsManager::Instance().isStrokeEraser;
        inputManager.stateMachine.eraserRadiusMm = SettingsManager::Instance().eraserSizeMm * 0.5f;
        ribbon.isCanvasInverted = SettingsManager::Instance().isCanvasInverted;

        // Restore per-device default tools from settings.
        // (Placeholder — settings UI not yet built; values come from the JSON defaults.)
        {
            auto toolFromStr = [](const std::string& s) -> InteractionState {
                if (s == "Inking")    return InteractionState::Inking;
                if (s == "Eraser")    return InteractionState::Eraser;
                if (s == "Selecting") return InteractionState::Selecting;
                if (s == "Panning")   return InteractionState::Panning;
                return InteractionState::Idle;
            };
            auto& sm = inputManager.stateMachine;
            sm.SetToolForDevice(DeviceType::Stylus, toolFromStr(SettingsManager::Instance().defaultStylusTool));
            sm.SetToolForDevice(DeviceType::Touch,  toolFromStr(SettingsManager::Instance().defaultTouchTool));
            sm.SetToolForDevice(DeviceType::Mouse,  toolFromStr(SettingsManager::Instance().defaultMouseTool));

            // Also honour the saved drawWithTouch toggle so the touch tool
            // starts in Inking mode if the user had it enabled last session.
            if (SettingsManager::Instance().drawWithTouch) {
                sm.SetToolForDevice(DeviceType::Touch, InteractionState::Inking);
            }

            sm.currentAction = sm.GetActiveDeviceTool();
        }
        if (ribbon.isCanvasInverted) {
            canvas.canvasBgColor = BLRgba32(0x1E, 0x20, 0x26);
            canvas.gridLineColor = BLRgba32(0x34, 0x38, 0x44);
            canvas.inkColorInverted = true;
        } else {
            canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
            canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
            canvas.inkColorInverted = false;
        }

        // Apply loaded presets and active preset
        ribbon.presetManager.LoadFromSettings();
        auto* activeP = ribbon.presetManager.GetActivePreset();
        if (activeP) {
            ribbon.presetManager.ApplyPreset(*activeP, inputManager.stateMachine.palette.GetActivePen());
        }

        // Apply display mode
        if (SettingsManager::Instance().ribbonDisplayMode == "Collapsed") {
            ribbon.SetDisplayMode(RibbonDisplayMode::Collapsed);
        } else if (SettingsManager::Instance().ribbonDisplayMode == "MiniToolbar") {
            ribbon.SetDisplayMode(RibbonDisplayMode::MiniToolbar);
        } else if (SettingsManager::Instance().ribbonDisplayMode == "FullyHidden") {
            ribbon.SetDisplayMode(RibbonDisplayMode::FullyHidden);
        } else {
            ribbon.SetDisplayMode(RibbonDisplayMode::FullRibbon);
        }

        // Apply active tab
        if (SettingsManager::Instance().ribbonActiveTab == "Home") ribbon.activeTab = RibbonTab::Home;
        else if (SettingsManager::Instance().ribbonActiveTab == "Insert") ribbon.activeTab = RibbonTab::Insert;
        else if (SettingsManager::Instance().ribbonActiveTab == "Draw") ribbon.activeTab = RibbonTab::Draw;
        else if (SettingsManager::Instance().ribbonActiveTab == "History") ribbon.activeTab = RibbonTab::History;
        else if (SettingsManager::Instance().ribbonActiveTab == "Review") ribbon.activeTab = RibbonTab::Review;
        else if (SettingsManager::Instance().ribbonActiveTab == "View") ribbon.activeTab = RibbonTab::View;
        else if (SettingsManager::Instance().ribbonActiveTab == "Help") ribbon.activeTab = RibbonTab::Help;

        toolbarDemo.LoadFromSettings();

        canvas.onPdfImportRequested = [this](const std::string& path, DocumentSession* s) {
            pdfImportModal.Open(path, s);
        };

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

            int pixelW = 0, pixelH = 0;
            SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);
            if (pixelW <= 0 || pixelH <= 0) {
                SDL_Delay(10);
                continue;
            }

            int logicalW = 0, logicalH = 0;
            SDL_GetWindowSize(window, &logicalW, &logicalH);
            if (logicalW <= 0 || logicalH <= 0) {
                logicalW = pixelW;
                logicalH = pixelH;
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
                    else if (event.key.key == SDLK_F4) { canvas.devMode = !canvas.devMode; canvas.isDirty = true; }
                    else if (event.key.key == SDLK_F5) tuningStudio.isVisible = !tuningStudio.isVisible;
                    else if (event.key.key == SDLK_F6) toolbarDemo.isVisible = !toolbarDemo.isVisible;
                    else if (event.key.key == SDLK_V && (SDL_GetModState() & SDL_KMOD_CTRL) && !ImGui::GetIO().WantCaptureKeyboard) {
                        canvas.InsertImageFromClipboard(&session);
                    }
                    else if (event.key.key == SDLK_DELETE) canvas.DeleteSelectedObjects(&session);
                }
                else if (event.type == SDL_EVENT_DROP_FILE) {
                    if (event.drop.data) {
                        std::string droppedPath = event.drop.data;
                        std::string ext = "";
                        auto dotPos = droppedPath.find_last_of('.');
                        if (dotPos != std::string::npos) {
                            ext = droppedPath.substr(dotPos);
                            for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                        }
                        if (ext == ".pdf") {
                            pdfImportModal.Open(droppedPath, &session);
                        }
                    }
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
                            else if (event.key.key == SDLK_F4) { canvas.devMode = !canvas.devMode; canvas.isDirty = true; }
                            else if (event.key.key == SDLK_F5) tuningStudio.isVisible = !tuningStudio.isVisible;
                            else if (event.key.key == SDLK_F6) toolbarDemo.isVisible = !toolbarDemo.isVisible;
                            else if (event.key.key == SDLK_V && (SDL_GetModState() & SDL_KMOD_CTRL) && !ImGui::GetIO().WantCaptureKeyboard) {
                                canvas.InsertImageFromClipboard(&session);
                            }
                            else if (event.key.key == SDLK_DELETE) canvas.DeleteSelectedObjects(&session);
                            else if (event.key.key == SDLK_F1 && (SDL_GetModState() & SDL_KMOD_CTRL)) ribbon.CycleDisplayMode();
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

            uint64_t currentFrameNs = SDL_GetTicksNS();
            double frameDtSec = static_cast<double>(currentFrameNs - lastRenderTimeNs) / 1000000000.0;
            lastRenderTimeNs = currentFrameNs;

            // Usage Telemetry Tracking
            if (currentView == AppViewMode::NotebookHub) {
                ::Folio::UsageTracker::Instance().RecordHubTime(frameDtSec);
                if (hubView.activeMainCategory == HubMainCategory::Settings) {
                    ::Folio::UsageTracker::Instance().RecordSettingsTime(frameDtSec);
                }
            } else {
                ::Folio::UsageTracker::Instance().RecordCanvasTime(frameDtSec);
                if (isActivelyDrawing) {
                    ::Folio::UsageTracker::Instance().RecordDrawingTime(frameDtSec);
                }
            }

            windowSM.Update();
            if (windowSM.currentState == WindowState::Minimized) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ::Folio::UsageTracker::Instance().RecordClick();
            }

            // =========================================================
            // WORKSPACE LAYOUT GEOMETRY (LOGICAL WINDOW COORDINATES)
            // =========================================================
            float screenW = static_cast<float>(logicalW);
            float screenH = static_cast<float>(logicalH);

            Uint32 winFlags = SDL_GetWindowFlags(window);
            bool isFullscreen = (winFlags & SDL_WINDOW_FULLSCREEN) != 0;
            float titleBarH = (customTitleBar.isVisible && !isFullscreen) ? CustomTitleBar::TITLEBAR_HEIGHT : 0.0f;

            // 0. CUSTOM SOFTWARE TITLE BAR (Borderless Window Frame)
            if (titleBarH > 0.0f) {
                std::string nbTitle = "DemoBook";
                if (auto nb = session.workspace.GetActiveNotebook()) {
                    nbTitle = nb->name;
                }
                customTitleBar.Render(
                    window,
                    screenW,
                    running,
                    devTelemetry.isVisible,
                    tuningStudio.isVisible,
                    toolbarDemo.isVisible,
                    themeManager,
                    nbTitle
                );
            }

            if (currentView == AppViewMode::NotebookHub) {
                hubView.Render(0.0f, titleBarH, screenW, screenH - titleBarH, currentView, themeManager, session, canvas, window);
            } else {
                // 1. TOP RIBBON BAR (Smooth Animated 4-State Ribbon)
                float ribbonH = ribbon.GetAnimatedHeight();
                if (ribbonH > 0.5f) {
                    ImGui::SetNextWindowPos(ImVec2(0.0f, titleBarH));
                    ImGui::SetNextWindowSize(ImVec2(screenW, ribbonH));
                    ImGuiWindowFlags ribbonFlags = ImGuiWindowFlags_NoTitleBar | 
                                                   ImGuiWindowFlags_NoResize | 
                                                   ImGuiWindowFlags_NoMove | 
                                                   ImGuiWindowFlags_NoCollapse | 
                                                   ImGuiWindowFlags_NoScrollbar |
                                                   ImGuiWindowFlags_NoScrollWithMouse |
                                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, themeManager.colorBg);
                    float ribbonRounding = (ribbon.displayMode == RibbonDisplayMode::Collapsed) ? 10.0f : 0.0f;
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ribbonRounding);
                    ImGui::Begin("##RibbonPanel", nullptr, ribbonFlags);
                    ribbon.Render(screenW, currentView, canvas, inputManager.stateMachine, themeManager, &session);
                    ImGui::End();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar();
                } else {
                    // Advance animation clock even when fully hidden
                    ribbon.Render(screenW, currentView, canvas, inputManager.stateMachine, themeManager, &session);
                }

                // Floating Pull Tab when ribbon is collapsed/hidden into canvas fullscreen
                if (ribbonH <= 8.0f) {
                    ImGui::SetNextWindowPos(ImVec2((screenW - 130.0f) * 0.5f, titleBarH + 4.0f));
                    ImGui::SetNextWindowSize(ImVec2(130.0f, 32.0f));
                    ImGuiWindowFlags pullTabFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                                    ImGuiWindowFlags_NoBackground;
                    ImGui::Begin("##RibbonPullTab", nullptr, pullTabFlags);
                    ImGui::PushStyleColor(ImGuiCol_Button, themeManager.colorBg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, themeManager.colorPrimaryHover);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
                    if (ImGui::Button("v  Show Ribbon", ImVec2(130.0f, 28.0f))) {
                        ribbon.SetDisplayMode(ribbon.previousActiveMode);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Restore Ribbon Bar (Ctrl+F1)");
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(3);
                    ImGui::End();
                }

                // 2. MODERN NAVIGATION SIDEBAR
                float contentY = titleBarH + ribbonH;
                float contentH = screenH - contentY;

                modernNav.Render(0.0f, contentY, contentH, session, canvas, themeManager, &currentView);

                // 3. CANVAS WORKSPACE OR DEDICATED PDF VIEWER (FILLS EXACT REMAINDER)
                float navW = modernNav.GetTotalWidth();
                float canvasX = navW;
                float canvasW = screenW - navW;

                auto activePg = session.GetActivePage();
                if (activePg && activePg->isDedicatedPdf) {
                    pdfViewer.Render(canvasX, canvasW, screenH, titleBarH, ribbonH, session, inputManager.stateMachine, themeManager, canvas.inkColorInverted);
                } else {

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
                        if (!modernNav.IsAnimating() && !ribbon.IsAnimating()) {
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
                        inputManager.stateMachine.isCanvasHovered = inputManager.wasCanvasImageHovered;
                    } else {
                        inputManager.wasCanvasImageHovered = false;
                        inputManager.stateMachine.isCanvasHovered = false;
                    }

                    // =========================================================================
                    // RIGHT-CLICK DETECTION ON GENERAL CANVAS
                    // =========================================================================
                    // When the user right clicks on the canvas viewport, we capture the mouse
                    // cursor screen coordinates and project them into the continuous 2D world
                    // coordinate space:
                    //   worldX = (screenX - originX) / (pixelsPerMm * zoom) - panXMm
                    //   worldY = (screenY - originY) / (pixelsPerMm * zoom) - panYMm
                    // This position is retained so pasted elements appear exactly under the cursor.
                    if (inputManager.wasCanvasImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        ImVec2 mousePos = ImGui::GetMousePos();
                        float localX = mousePos.x - canvasOrigin.x;
                        float localY = mousePos.y - canvasOrigin.y;
                        generalContextMenuWorldPos = canvas.transform.ScreenToWorld(localX, localY);
                        ImGui::OpenPopup("##GeneralCanvasContextMenu");
                    }

                    Point2D titleScreen = canvas.transform.WorldToScreen(80.0, 50.0);
                    float currentZoom = static_cast<float>(canvas.transform.zoom);
                    if (titleScreen.x > -800.0 && titleScreen.y > -300.0) {
                        ImVec2 winPos = ImGui::GetWindowPos();
                        ImVec2 boxScreenPos(winPos.x + static_cast<float>(titleScreen.x), winPos.y + static_cast<float>(titleScreen.y));

                        // 1. Dynamic width & height calculation
                        const char* displayTitle = (canvas.pageTitle[0] != '\0') ? canvas.pageTitle : "New Untitled";
                        ImFont* titleFont = FolioTheme::FontRibbonBoldLarge ? FolioTheme::FontRibbonBoldLarge : FolioTheme::FontRegular;
                        ImGui::PushFont(titleFont);
                        ImVec2 titleSize = ImGui::CalcTextSize(displayTitle);
                        ImGui::PopFont();

                        std::string dateTimeStr = canvas.pageDateStr + "   |   " + canvas.pageTimeStr;
                        ImVec2 dateSize = ImGui::CalcTextSize(dateTimeStr.c_str());

                        float maxContentW = std::max(titleSize.x, dateSize.x);
                        float padX = 14.0f;
                        float padY = 10.0f;
                        float gapY = 5.0f;
                        float titleH = titleSize.y > 0 ? titleSize.y : 24.0f;
                        float dateH = dateSize.y > 0 ? dateSize.y : 15.0f;

                        float minBoxW = 200.0f * currentZoom;
                        float boxW = std::max(minBoxW, maxContentW + padX * 2.0f);
                        float boxH = padY * 2.0f + titleH + gapY + dateH;

                        // 2. Non-selectable translucent box outline with slightly rounded corners
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        ImVec2 boxMin = boxScreenPos;
                        ImVec2 boxMax = ImVec2(boxScreenPos.x + boxW, boxScreenPos.y + boxH);
                        float rounding = 6.0f; // slightly rounded corners

                        bool isDark = (themeManager.colorBg.x < 0.5f);
                        // Translucent background
                        ImU32 bgCol = isDark ? IM_COL32(25, 28, 36, 115) : IM_COL32(248, 250, 253, 125);
                        // Clean outline border
                        ImU32 borderCol = isDark ? IM_COL32(110, 118, 135, 140) : IM_COL32(175, 182, 195, 150);

                        drawList->AddRectFilled(boxMin, boxMax, bgCol, rounding);
                        drawList->AddRect(boxMin, boxMax, borderCol, rounding, 0, 1.0f);

                        // 3. Clean non-selectable text rendering
                        ImU32 titleTextCol = isDark ? IM_COL32(235, 238, 245, 245) : IM_COL32(25, 28, 35, 255);
                        ImU32 dateTextCol = isDark ? IM_COL32(150, 158, 172, 210) : IM_COL32(115, 122, 134, 210);

                        ImGui::PushFont(titleFont);
                        drawList->AddText(ImVec2(boxMin.x + padX, boxMin.y + padY), titleTextCol, displayTitle);
                        ImGui::PopFont();

                        drawList->AddText(ImVec2(boxMin.x + padX, boxMin.y + padY + titleH + gapY), dateTextCol, dateTimeStr.c_str());
                    }

                    // =========================================================================
                    // GENERAL INFINITE CANVAS RIGHT-CLICK CONTEXT MENU
                    // =========================================================================
                    // Provides standard workspace operations:
                    // 1. Paste text from clipboard as a new persistent text block at click position.
                    // 2. Select All / Deselect objects via the interactive SelectionGizmo.
                    // 3. Create bidirectional Markdown link to this page: [Title](folionote://page/<guid>)
                    // 4. Viewport controls: Reset Zoom (100%) and Reset View to origin (0, 0).
                    {
                        ContextMenuThemeScope ctxScope(themeManager);
                        if (ImGui::BeginPopup("##GeneralCanvasContextMenu")) {
                            auto activePg = session.GetActivePage();
                            const char* displayTitle = (canvas.pageTitle[0] != '\0') ? canvas.pageTitle : (activePg ? activePg->title.c_str() : "Untitled Page");

                            ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
                            ImGui::TextColored(themeManager.colorPrimary, "%s", displayTitle);
                            ImGui::PopFont();
                            ImGui::TextColored(themeManager.colorTextMuted, "Pos: (%.1f, %.1f) mm  |  Zoom: %.0f%%", 
                                               generalContextMenuWorldPos.x, generalContextMenuWorldPos.y, canvas.transform.zoom * 100.0);
                            ImGui::Separator();

                            // 1. Paste (enabled if clipboard has text)
                            bool canPaste = SDL_HasClipboardText();
                            if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) {
                                char* clipText = SDL_GetClipboardText();
                                if (clipText) {
                                    if (clipText[0] != '\0') {
                                        auto tb = std::make_shared<Folio::TextBoxObject>();
                                        tb->worldX = generalContextMenuWorldPos.x;
                                        tb->worldY = generalContextMenuWorldPos.y;
                                        tb->text = clipText;
                                        tb->UpdateBounds();
                                        session.AddTextBox(tb);
                                        canvas.isDirty = true;
                                    }
                                    SDL_free(clipText);
                                }
                            }

                            // 2. Select All & Clear Selection
                            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                                auto allObjs = session.QueryVisible(canvas.GetViewport());
                                for (auto& obj : allObjs) {
                                    if (obj) obj->isSelected = 1;
                                }
                                canvas.selectionGizmo.SetSelectedObjects(allObjs);
                                canvas.isDirty = true;
                            }

                            if (canvas.selectionGizmo.HasSelection()) {
                                if (ImGui::MenuItem("Clear Selection", "Esc")) {
                                    canvas.selectionGizmo.ClearSelection();
                                    canvas.isDirty = true;
                                }
                            }

                            ImGui::Separator();

                            // 3. Create Link to This Page
                            if (ImGui::MenuItem("Create Link to This Page")) {
                                if (activePg) {
                                    std::string linkMarkdown = "[" + std::string(displayTitle) + "](folionote://page/" + activePg->guid + ")";
                                    SDL_SetClipboardText(linkMarkdown.c_str());
                                }
                            }

                            ImGui::Separator();

                            // 4. Zoom and Viewport Reset
                            if (ImGui::MenuItem("Reset Zoom to 100%")) {
                                canvas.transform.zoom = 1.0;
                                canvas.isDirty = true;
                            }

                            if (ImGui::MenuItem("Reset View to Origin")) {
                                canvas.transform.panXMm = 0.0;
                                canvas.transform.panYMm = 0.0;
                                canvas.transform.zoom = 1.0;
                                canvas.isDirty = true;
                            }

                            ImGui::EndPopup();
                        }
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar();
                } // End of if (activePg && activePg->isDedicatedPdf) else

                // 4. ADVANCED DOCUMENT OPTIONS SLIDING PANEL
                // Floats over canvas from right side when View tab -> Adv. Options is toggled.
                ribbon.RenderAdvancedOptionsPanel(screenW, screenH, titleBarH, ribbonH, canvas, themeManager);

                // 5. FLOATING COLLAPSED RIBBON OVERLAY
                // When ribbon is in collapsed mode and a tab is selected, this floats over canvas
                // without shifting/moving canvas, with pure shelf background (no orange), and auto-hides on canvas input.
                ribbon.RenderCollapsedPopup(screenW, titleBarH, canvas, inputManager.stateMachine, themeManager, &session);

            }

            // =========================================================
            // DIAGNOSTICS & MODAL OVERLAYS
            // =========================================================
            devTelemetry.Render(canvas, inputManager.stateMachine, windowSM, session, inputManager.stateMachine.canvasOriginX, inputManager.stateMachine.canvasOriginY, themeManager);
            tuningStudio.Render(themeManager);
            if (ribbon.showDemoOverlay) {
                toolbarDemo.isVisible = true;
                ribbon.showDemoOverlay = false;
            }
            toolbarDemo.Render(themeManager);
            pdfImportModal.Render(session, canvas, themeManager);

            ImGui::Render();
            glViewport(0, 0, pixelW, pixelH);
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
        ::Folio::UsageTracker::Instance().SaveToJson("config/usage_stats.json");

        // Sync final UI and Ribbon state to SettingsManager before saving
        SettingsManager::Instance().drawWithTouch = ribbon.drawWithTouch;
        SettingsManager::Instance().rulerEnabled = ribbon.rulerEnabled;
        SettingsManager::Instance().autoShapesEnabled = ribbon.autoShapesEnabled;
        SettingsManager::Instance().isStrokeEraser = ribbon.isStrokeEraser;
        SettingsManager::Instance().eraserSizeMm = ribbon.eraserSizeMm;
        SettingsManager::Instance().isCanvasInverted = ribbon.isCanvasInverted;

        switch (ribbon.displayMode) {
            case RibbonDisplayMode::FullRibbon: SettingsManager::Instance().ribbonDisplayMode = "FullRibbon"; break;
            case RibbonDisplayMode::MiniToolbar: SettingsManager::Instance().ribbonDisplayMode = "MiniToolbar"; break;
            case RibbonDisplayMode::Collapsed: SettingsManager::Instance().ribbonDisplayMode = "Collapsed"; break;
            case RibbonDisplayMode::FullyHidden: SettingsManager::Instance().ribbonDisplayMode = "FullyHidden"; break;
        }

        switch (ribbon.activeTab) {
            case RibbonTab::Home: SettingsManager::Instance().ribbonActiveTab = "Home"; break;
            case RibbonTab::Insert: SettingsManager::Instance().ribbonActiveTab = "Insert"; break;
            case RibbonTab::Draw: SettingsManager::Instance().ribbonActiveTab = "Draw"; break;
            case RibbonTab::History: SettingsManager::Instance().ribbonActiveTab = "History"; break;
            case RibbonTab::Review: SettingsManager::Instance().ribbonActiveTab = "Review"; break;
            case RibbonTab::View: SettingsManager::Instance().ribbonActiveTab = "View"; break;
            case RibbonTab::Help: SettingsManager::Instance().ribbonActiveTab = "Help"; break;
            case RibbonTab::ShapeFormat: break;
        }

        toolbarDemo.SaveToSettings();
        SettingsManager::Instance().Save();

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