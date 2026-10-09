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
#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "core/clipboard/clipboard_manager.hpp"
#include "core/objects/attachment_container/attachment_container.hpp"
#include "core/objects/media/audio/audio_container.hpp"
#include "core/objects/media/audio/audio_overlay_ui.hpp"
#include "core/history/canvas_command.hpp"
#include "app/context_menu_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/components/ribbon_bar.hpp"
#include "ui/components/modern_nav_panel.hpp"
#include "ui/components/debug_overlay.hpp"
#include "ui/components/custom_titlebar.hpp"
#include "ui/views/notebook_hub.hpp"
#include "ui/views/pdf_viewer_page.hpp"
#include "ui/views/md_editor_view.hpp"
#include "ui/shell/app_shell.hpp"
#include "ui/overlays/overlay_manager.hpp"
#include "app/actions/ui_action_registry.hpp"
#include "input/input_manager.hpp"
#include "io/file_reader.hpp"
#include "utils/usage_tracker.hpp"
#include "app/theme_manager.hpp"
#include <lunasvg.h>
#include <chrono>
#include <thread>
#include <string>
#include <filesystem>
#include <unordered_set>
#include <fstream>

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

    // Modularized Overlays & Modals Subsystem
    Folio::OverlayManager overlayManager;
    DebugOverlay& devTelemetry = overlayManager.devTelemetry;
    InkingTuningOverlay& tuningStudio = overlayManager.tuningStudio;
    ToolbarDemoOverlay& toolbarDemo = overlayManager.toolbarDemo;
    Folio::PdfImportModal& pdfImportModal = overlayManager.pdfImportModal;
    Folio::ActionCommandPalette& commandPalette = overlayManager.commandPalette;

    Folio::PdfViewerPage pdfViewer;
    Folio::MdEditorView mdEditor;

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
    Folio::ContextMenuManager contextMenuManager;

    /**
     * @brief GUID of the canvas page that was active in the previous frame.
     * Used to detect page transitions so we can save/restore the canvas
     * viewport (panXMm, panYMm, zoom) in each page's inMemoryViewport.
     */
    std::string lastActivePageGuid;

    /**
     * @brief Dedicated directory where incoming virtual print spools and imported PDF files are placed.
     * Standard path: Documents/FolioNote/Imports (or platform preference folder).
     */
    std::filesystem::path importsDirectory;

    /**
     * @brief Set of canonical file paths already registered or processed from the Imports folder.
     * Prevents continuous re-prompting for documents that have already been imported or rejected.
     */
    std::unordered_set<std::string> knownImportedPdfs;

    /**
     * @brief Timestamp (SDL_GetTicks) of the last directory scan of the Imports folder.
     * Throttled to execute once every 1.5 - 2.0 seconds to minimize I/O overhead.
     */
    uint64_t lastImportScanTicksMs = 0;

    /**
     * @brief Pre-indexes and ensures existence of the dedicated user Imports directory.
     *
     * GENERAL WORKING PROCESS:
     * 1. Forms the subdirectory path `<rootPath>/Imports` (e.g. `Documents/FolioNote/Imports`).
     * 2. Creates the directory tree if it does not yet exist on disk.
     * 3. Scans existing files within the folder and caches their canonical paths in `knownImportedPdfs`.
     *    This ensures that when FolioNote starts up, pre-existing files do not trigger an avalanche
     *    of modal prompts, and only newly deposited documents (e.g. freshly printed via
     *    "Print to FolioNote") trigger interactive placement.
     *
     * @param rootPath Root directory of FolioNote data (typically OS Documents folder / FolioNote).
     */
    void EnsureImportsDirectory(const std::filesystem::path& rootPath) {
        importsDirectory = rootPath / "Imports";
        std::error_code ec;
        if (!std::filesystem::exists(importsDirectory, ec)) {
            std::filesystem::create_directories(importsDirectory, ec);
        } else {
            // Index existing files so only newly deposited documents trigger the modal
            for (const auto& entry : std::filesystem::directory_iterator(importsDirectory, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".pdf") {
                    knownImportedPdfs.insert(std::filesystem::canonical(entry.path(), ec).string());
                }
            }
        }
    }

    /**
     * @brief Stages an external document into the Imports folder and presents the import placement dialog.
     *
     * GENERAL WORKING PROCESS:
     * 1. Inspects the source file to verify existence and `.pdf` format.
     * 2. If the file is not already inside `importsDirectory`, copies it into `importsDirectory`.
     *    If a file with the same filename already exists, appends an incremental numeric suffix
     *    (e.g., `document_1.pdf`) to prevent accidental overwrites of existing imports.
     * 3. Marks the destination canonical path in `knownImportedPdfs` so background polling ignores it.
     * 4. Calls `pdfImportModal.Open(dest, &session)` which launches the interactive UI modal
     *    asking the user where to place the document (which notebook, section, page, or standalone viewer).
     *
     * @param sourcePath Canonical or relative filesystem path to the external PDF document.
     * @return true if the document was successfully staged and modal was opened; false otherwise.
     */
    bool ImportExternalPdf(const std::string& sourcePath) {
        if (sourcePath.empty()) return false;
        std::error_code ec;
        std::filesystem::path src(sourcePath);
        if (!std::filesystem::exists(src, ec)) return false;

        std::filesystem::path dest = src;
        if (!importsDirectory.empty()) {
            std::filesystem::path target = importsDirectory / src.filename();
            // Avoid copying onto itself if the file was already placed directly into Imports
            if (std::filesystem::canonical(src, ec) != std::filesystem::canonical(target, ec)) {
                int counter = 1;
                while (std::filesystem::exists(target, ec)) {
                    std::string stem = src.stem().string() + "_" + std::to_string(counter++);
                    target = importsDirectory / (stem + src.extension().string());
                }
                std::filesystem::copy_file(src, target, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) dest = target;
            }
        }

        std::string canonicalDest = std::filesystem::canonical(dest, ec).string();
        if (canonicalDest.empty()) canonicalDest = dest.string();
        knownImportedPdfs.insert(canonicalDest);

        pdfImportModal.Open(dest.string(), &session);
        return true;
    }

    /**
     * @brief Scans the Imports directory for incoming documents (e.g. from 'Print to FolioNote').
     *
     * GENERAL WORKING PROCESS:
     * 1. Guard check: If `pdfImportModal` is already open, avoids popping an overlapping modal.
     * 2. Iterates over regular `.pdf` files located in `importsDirectory`.
     * 3. For any file not present in `knownImportedPdfs`:
     *    - Checks that the file size is greater than 0 bytes.
     *    - Verifies file is not locked: Attempts an exclusive binary stream open. If locked by the
     *      Windows Print Spooler or browser during an active print write, defers until next scan cycle.
     *    - Adds path to `knownImportedPdfs` and triggers `pdfImportModal.Open(...)`.
     *    - Returns true immediately to process one document at a time.
     *
     * @return true if a new incoming PDF document was detected and opened; false if none.
     */
    bool CheckImportFolder() {
        if (pdfImportModal.isOpen) return false;
        if (importsDirectory.empty()) return false;

        std::error_code ec;
        if (!std::filesystem::exists(importsDirectory, ec)) return false;

        for (const auto& entry : std::filesystem::directory_iterator(importsDirectory, ec)) {
            if (!entry.is_regular_file()) continue;

            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            if (ext != ".pdf") continue;

            std::string canonicalPath = std::filesystem::canonical(entry.path(), ec).string();
            if (canonicalPath.empty()) canonicalPath = entry.path().string();

            if (knownImportedPdfs.find(canonicalPath) != knownImportedPdfs.end()) {
                continue;
            }

            // Ensure file is finished writing (not locked by print spooler / browser download)
            auto fileSize = std::filesystem::file_size(entry.path(), ec);
            if (ec || fileSize == 0) {
                continue; // Still spooling or 0 bytes
            }

            // Test if file can be opened for reading
            std::ifstream testStream(entry.path(), std::ios::binary | std::ios::in);
            if (!testStream.is_open()) {
                continue; // File locked by another process
            }
            testStream.close();

            // Register and launch import modal
            knownImportedPdfs.insert(canonicalPath);
            pdfImportModal.Open(entry.path().string(), &session);
            devTelemetry.LogEvent("Discovered incoming virtual print document: " + entry.path().filename().string(), LogCategory::System);
            return true;
        }
        return false;
    }

    /**
     * @brief Timestamp (SDL_GetTicks) of the last LRU working-set eviction check.
     * The check runs at most once per second to avoid overhead.
     */
    uint64_t lastLruCheckMs = 0;

    bool Init(const char* title = "FolioNote", int initialW = 1920, int initialH = 1080, const std::string& initialImportPath = "") {
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
        SDL_StartTextInput(window);

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

        float displayScale = SDL_GetWindowDisplayScale(window);
        if (displayScale <= 0.0f) displayScale = 1.0f;
        float displayDpi = 96.0f * displayScale;
        canvas.Init(initialW, initialH, displayDpi);
        session.SetEphemeralStrokeSink([this](BLPath path, BLRgba32 color, uint32_t durationMs) {
            canvas.AddEphemeralStroke(std::move(path), color, durationMs);
        });
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
        char* prefPath = SDL_GetPrefPath("UniversalFramework", "FolioNote");
        std::filesystem::path folioPath;
        if (prefPath) {
            folioPath = std::filesystem::path(prefPath);
            SDL_free(prefPath);
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

        // Centralize directory and disk logging initialization now that platform directories are live
        Folio::AppDirectories::SetAppRootDirectory(folioPath.string());
        Folio::FileLogger::Instance().Initialize(Folio::PathUtils::JoinPath(folioPath.string(), "logs"));

        // Initialize the SQLite session using this physical directory
        session.Init(folioPath.string());

        // Load persistent JSON settings
        SettingsManager::Instance().Load();

        // Restore last session position (Notebook, Section, Page)
        const auto& sm = SettingsManager::Instance();
        if (!sm.lastActiveNotebookGuid.empty() || !sm.lastActivePageGuid.empty()) {
            session.RestoreLastSession(sm.lastActiveNotebookGuid, sm.lastActiveSectionGuid, sm.lastActivePageGuid);
        }

        // Sync loaded settings to RibbonBar and InputStateMachine
        ribbon.drawWithTouch = SettingsManager::Instance().drawWithTouch;
        ribbon.rulerEnabled = SettingsManager::Instance().rulerEnabled;
        ribbon.autoShapesEnabled = SettingsManager::Instance().autoShapesEnabled;
        ribbon.isStrokeEraser = SettingsManager::Instance().isStrokeEraser;
        ribbon.isDynamicEraser = SettingsManager::Instance().isDynamicEraser;
        ribbon.eraserSizeMm = SettingsManager::Instance().eraserSizeMm;
        inputManager.stateMachine.isStrokeEraser = SettingsManager::Instance().isStrokeEraser;
        inputManager.stateMachine.isDynamicEraser = SettingsManager::Instance().isDynamicEraser;
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

        // ---------------------------------------------------------
        // CENTRALIZED UI ACTION REGISTRATION (Spotlight & Shortcuts)
        // ---------------------------------------------------------
        RegisterDefaultActions();

        // ---------------------------------------------------------
        // IMPORTS DIRECTORY & CLI STAGED DOCUMENT INITIALIZATION
        // ---------------------------------------------------------
        EnsureImportsDirectory(folioPath);
        if (!initialImportPath.empty()) {
            ImportExternalPdf(initialImportPath);
        }

        devTelemetry.LogEvent("FolioNote initialized.", LogCategory::System);
        return true;
    }

    /**
     * @brief Registers default application and canvas commands into the centralized UIActionRegistry.
     *
     * Enables command discovery and keyboard shortcuts across:
     * - Spotlight Action Command Palette (Ctrl+K / Ctrl+P)
     * - Ribbon Bar and Toolbars
     * - Global hotkey dispatching
     */
    void RegisterDefaultActions() {
        auto& reg = Folio::UIActionRegistry::Instance();

        // =========================================================================
        // 1. CANVAS VIEWPORT OPERATIONS
        // =========================================================================
        reg.RegisterAction(Folio::UIAction("Zoom In", "Ctrl++", "🔍", false, true, false, [this]() {
            canvas.transform.zoom = std::min(10.0, canvas.transform.zoom * 1.25);
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 10, false, "zoom_in", 0, "canvas.zoom_in", "Canvas"));

        reg.RegisterAction(Folio::UIAction("Zoom Out", "Ctrl+-", "🔍", false, true, false, [this]() {
            canvas.transform.zoom = std::max(0.1, canvas.transform.zoom / 1.25);
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 11, false, "zoom_out", 0, "canvas.zoom_out", "Canvas"));

        reg.RegisterAction(Folio::UIAction("Reset Zoom (100%)", "Ctrl+0", "🔍", false, true, false, [this]() {
            canvas.transform.zoom = 1.0;
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 12, false, "zoom_reset", 0, "canvas.zoom_reset", "Canvas"));

        reg.RegisterAction(Folio::UIAction("Reset View to Origin", "Home", "⌂", false, true, false, [this]() {
            canvas.transform.panXMm = 0.0;
            canvas.transform.panYMm = 0.0;
            canvas.transform.zoom = 1.0;
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 13, false, "home", 0, "canvas.reset_view", "Canvas"));

        reg.RegisterAction(Folio::UIAction("Toggle Dark / Inverted Canvas", "Ctrl+I", "🌓", false, true, false, [this]() {
            ribbon.isCanvasInverted = !ribbon.isCanvasInverted;
            if (ribbon.isCanvasInverted) {
                canvas.canvasBgColor = BLRgba32(0x1E, 0x20, 0x26);
                canvas.gridLineColor = BLRgba32(0x34, 0x38, 0x44);
                canvas.inkColorInverted = true;
            } else {
                canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
                canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
                canvas.inkColorInverted = false;
            }
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 14, false, "contrast", 0, "canvas.invert", "Canvas"));

        // =========================================================================
        // 2. SELECTION, EDITING & CLIPBOARD OPERATIONS
        // =========================================================================
        reg.RegisterAction(Folio::UIAction("Select All", "Ctrl+A", "⬚", false, true, false, [this]() {
            canvas.SelectAll(&session);
        }, 20, false, "select_all", 0, "edit.select_all", "Edit"));

        reg.RegisterAction(Folio::UIAction("Clear Selection", "Esc", "✕", false, true, false, [this]() {
            canvas.selectionGizmo.ClearSelection();
            canvas.isDirty = true;
        }, 21, false, "clear_selection", 0, "edit.clear_selection", "Edit"));

        reg.RegisterAction(Folio::UIAction("Undo", "Ctrl+Z", "↶", false, true, false, [this]() {
            session.Undo(&canvas);
        }, 22, false, "undo", 0, "edit.undo", "Edit"));

        reg.RegisterAction(Folio::UIAction("Redo", "Ctrl+Y", "↷", false, true, false, [this]() {
            session.Redo(&canvas);
        }, 23, false, "redo", 0, "edit.redo", "Edit"));

        reg.RegisterAction(Folio::UIAction("Copy", "Ctrl+C", "📋", false, true, false, [this]() {
            auto selected = session.GetSelectedObjects();
            if (!selected.empty()) {
                Folio::ClipboardManager::Instance().CopyObjects(selected);
            }
        }, 24, false, "copy", 0, "edit.copy", "Edit"));

        reg.RegisterAction(Folio::UIAction("Cut", "Ctrl+X", "✂", false, true, false, [this]() {
            auto selected = session.GetSelectedObjects();
            if (!selected.empty()) {
                Folio::ClipboardManager::Instance().CutObjects(selected, session);
                canvas.selectionGizmo.ClearSelection();
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
        }, 25, false, "cut", 0, "edit.cut", "Edit"));

        reg.RegisterAction(Folio::UIAction("Paste", "Ctrl+V", "📋", false, true, false, [this]() {
            Point2D centerWorld = canvas.transform.ScreenToWorld(
                static_cast<float>(canvas.viewportW) * 0.5f,
                static_cast<float>(canvas.viewportH) * 0.5f
            );
            Folio::ClipboardManager::Instance().Paste(session, canvas, centerWorld.x, centerWorld.y);
        }, 26, false, "paste", 0, "edit.paste", "Edit"));

        reg.RegisterAction(Folio::UIAction("Duplicate Selection", "Ctrl+D", "📄", false, true, false, [this]() {
            session.DuplicateSelection(10.0);
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 27, false, "duplicate", 0, "edit.duplicate", "Edit"));

        reg.RegisterAction(Folio::UIAction("Delete Selection", "Del", "🗑", false, true, true, [this]() {
            canvas.DeleteSelectedObjects(&session);
        }, 28, false, "trash", 0, "edit.delete", "Edit"));

        // =========================================================================
        // 3. PRIMARY CANVAS TOOLS
        // =========================================================================
        reg.RegisterAction(Folio::UIAction("Pen Tool", "P", "✏", false, true, false, [this]() {
            inputManager.stateMachine.SetToolForDevice(inputManager.stateMachine.ActiveDevice, InteractionState::Inking);
            inputManager.stateMachine.palette.activePen.penType = PenType::Pen;
        }, 30, false, "pen", 0, "tool.pen", "Tools"));

        reg.RegisterAction(Folio::UIAction("Highlighter Tool", "H", "🖍", false, true, false, [this]() {
            inputManager.stateMachine.SetToolForDevice(inputManager.stateMachine.ActiveDevice, InteractionState::Inking);
            inputManager.stateMachine.palette.activePen.penType = PenType::Highlighter;
        }, 31, false, "highlighter", 0, "tool.highlighter", "Tools"));

        reg.RegisterAction(Folio::UIAction("Pencil Tool", "", "✏", false, true, false, [this]() {
            inputManager.stateMachine.SetToolForDevice(inputManager.stateMachine.ActiveDevice, InteractionState::Inking);
            inputManager.stateMachine.palette.activePen.penType = PenType::Pencil;
        }, 32, false, "pencil", 0, "tool.pencil", "Tools"));

        reg.RegisterAction(Folio::UIAction("Eraser Tool", "E", "🧹", false, true, false, [this]() {
            inputManager.stateMachine.SetToolForDevice(inputManager.stateMachine.ActiveDevice, InteractionState::Eraser);
        }, 33, false, "eraser", 0, "tool.eraser", "Tools"));

        reg.RegisterAction(Folio::UIAction("Lasso Selection Tool", "S", "⬚", false, true, false, [this]() {
            inputManager.stateMachine.SetToolForDevice(inputManager.stateMachine.ActiveDevice, InteractionState::Selecting);
        }, 34, false, "select", 0, "tool.select", "Tools"));

        reg.RegisterAction(Folio::UIAction("Text Box (Click-to-Type)", "T", "🔤", false, true, false, [this]() {
            inputManager.stateMachine.SetToolForDevice(inputManager.stateMachine.ActiveDevice, InteractionState::Text);
        }, 35, false, "text", 0, "tool.text", "Tools"));

        reg.RegisterAction(Folio::UIAction("Hand / Pan Viewport", "Space", "✋", false, true, false, [this]() {
            inputManager.stateMachine.SetToolForDevice(inputManager.stateMachine.ActiveDevice, InteractionState::Panning);
        }, 36, false, "hand", 0, "tool.pan", "Tools"));

        // =========================================================================
        // 4. DOCUMENT & PAGE HIERARCHY OPERATIONS
        // =========================================================================
        reg.RegisterAction(Folio::UIAction("New Page", "Ctrl+N", "➕", false, true, false, [this]() {
            session.CreateNewPage();
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 40, false, "add_page", 0, "page.new", "Document"));

        reg.RegisterAction(Folio::UIAction("Duplicate Active Page", "", "📄", false, true, false, [this]() {
            session.DuplicateActivePage();
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 41, false, "duplicate_page", 0, "page.duplicate", "Document"));

        reg.RegisterAction(Folio::UIAction("Delete Active Page", "Ctrl+Shift+Del", "🗑", false, true, true, [this]() {
            session.DeleteActivePage(true);
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 42, false, "trash", 0, "page.delete", "Document"));

        reg.RegisterAction(Folio::UIAction("Next Page", "PageDown", "⏩", false, true, false, [this]() {
            session.NextPage();
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 43, false, "next_page", 0, "page.next", "Document"));

        reg.RegisterAction(Folio::UIAction("Previous Page", "PageUp", "⏪", false, true, false, [this]() {
            session.PreviousPage();
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }, 44, false, "prev_page", 0, "page.prev", "Document"));

        reg.RegisterAction(Folio::UIAction("Save Notebook", "Ctrl+S", "💾", false, true, false, [this]() {
            session.SaveAllModifiedPages();
        }, 45, false, "save", 0, "doc.save", "Document"));

        // =========================================================================
        // 5. VIEW & WINDOW MODES
        // =========================================================================
        reg.RegisterAction(Folio::UIAction("Open Notebook Hub", "Ctrl+H", "📚", false, true, false, [this]() {
            currentView = AppViewMode::NotebookHub;
        }, 50, false, "hub", 0, "view.notebook_hub", "View"));

        reg.RegisterAction(Folio::UIAction("Switch to Canvas Workspace", "", "📐", false, true, false, [this]() {
            currentView = AppViewMode::CanvasWorkspace;
        }, 51, false, "canvas", 0, "view.canvas_workspace", "View"));

        reg.RegisterAction(Folio::UIAction("Toggle Fullscreen", "F11", "⛶", false, true, false, [this]() {
            bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(window, !fs);
        }, 52, false, "fullscreen", 0, "view.toggle_fullscreen", "View"));

        reg.RegisterAction(Folio::UIAction("Toggle Ribbon Collapse", "Ctrl+F1", "📑", false, true, false, [this]() {
            ribbon.ToggleCollapse();
        }, 53, false, "ribbon", 0, "view.toggle_ribbon", "View"));

        reg.RegisterAction(Folio::UIAction("Cycle Ribbon Display Mode", "", "🔄", false, true, false, [this]() {
            ribbon.CycleDisplayMode();
        }, 54, false, "cycle", 0, "view.cycle_ribbon_mode", "View"));

        // =========================================================================
        // 6. TOOLS, MODALS & DIAGNOSTIC OVERLAYS
        // =========================================================================
        reg.RegisterAction(Folio::UIAction("Spotlight Command Palette", "Ctrl+K", "🔍", false, true, false, [this]() {
            overlayManager.ToggleCommandPalette();
        }, 60, false, "search", 0, "tools.command_palette", "Tools"));

        reg.RegisterAction(Folio::UIAction("Toggle Diagnostics Telemetry", "F3", "📊", false, true, false, [this]() {
            devTelemetry.isVisible = !devTelemetry.isVisible;
        }, 61, false, "telemetry", 0, "tools.telemetry", "Tools"));

        reg.RegisterAction(Folio::UIAction("Toggle Inking Tuning Studio", "F5", "⚙", false, true, false, [this]() {
            tuningStudio.isVisible = !tuningStudio.isVisible;
        }, 62, false, "tuning", 0, "tools.tuning", "Tools"));

        reg.RegisterAction(Folio::UIAction("Toggle Toolbar Components Demo", "F6", "🎨", false, true, false, [this]() {
            toolbarDemo.isVisible = !toolbarDemo.isVisible;
        }, 63, false, "demo", 0, "tools.toolbar_demo", "Tools"));
    }

    /**
     * @brief Dispatches global keyboard shortcuts cleanly through UIActionRegistry.
     *
     * Evaluates keydown combinations when ImGui keyboard capture is not asserted.
     * Prevents duplicate shortcut handling logic across multiple frame event polling loops.
     *
     * @param event The active SDL event of type SDL_EVENT_KEY_DOWN.
     * @return true if an action or shortcut was recognized and handled; false otherwise.
     */
    bool HandleGlobalShortcut(const SDL_Event& event) {
        if (event.type != SDL_EVENT_KEY_DOWN) return false;

        const SDL_Keymod mod = SDL_GetModState();
        const bool isCtrl = (mod & SDL_KMOD_CTRL) != 0;
        const bool isShift = (mod & SDL_KMOD_SHIFT) != 0;
        const auto key = event.key.key;
        auto& reg = Folio::UIActionRegistry::Instance();

        // Spotlight Command Palette (Ctrl+K or Ctrl+P) - allowed even when ImGui has focus
        if (isCtrl && (key == SDLK_K || key == SDLK_P)) {
            reg.Execute("tools.command_palette");
            return true;
        }

        // Developer overlays and diagnostic toggles
        if (key == SDLK_F3) { reg.Execute("tools.telemetry"); return true; }
        if (key == SDLK_F4) { canvas.devMode = !canvas.devMode; canvas.isDirty = true; return true; }
        if (key == SDLK_F5) { reg.Execute("tools.tuning"); return true; }
        if (key == SDLK_F6) { reg.Execute("tools.toolbar_demo"); return true; }
        if (key == SDLK_F11) { reg.Execute("view.toggle_fullscreen"); return true; }
        if (isCtrl && key == SDLK_F1) { reg.Execute("view.cycle_ribbon_mode"); return true; }

        // If Dear ImGui active widgets (like text boxes) want keyboard focus, bypass general editor hotkeys
        if (ImGui::GetIO().WantCaptureKeyboard) {
            return false;
        }

        // Clipboard & Selection hotkeys
        if (isCtrl && key == SDLK_C) { return reg.Execute("edit.copy"); }
        if (isCtrl && key == SDLK_X) { return reg.Execute("edit.cut"); }
        if (isCtrl && key == SDLK_V) { return reg.Execute("edit.paste"); }
        if (isCtrl && key == SDLK_D) { return reg.Execute("edit.duplicate"); }
        if (isCtrl && key == SDLK_A) { return reg.Execute("edit.select_all"); }
        if (key == SDLK_ESCAPE)      { return reg.Execute("edit.clear_selection"); }
        if (key == SDLK_DELETE)      { return reg.Execute("edit.delete"); }

        // Undo / Redo
        if (isCtrl && key == SDLK_Z) {
            return isShift ? reg.Execute("edit.redo") : reg.Execute("edit.undo");
        }
        if (isCtrl && key == SDLK_Y) { return reg.Execute("edit.redo"); }

        // Document & Navigation hotkeys
        if (isCtrl && key == SDLK_N) { return reg.Execute("page.new"); }
        if (isCtrl && key == SDLK_S) { return reg.Execute("doc.save"); }
        if (isCtrl && key == SDLK_H) { return reg.Execute("view.notebook_hub"); }
        if (isCtrl && key == SDLK_I) { return reg.Execute("canvas.invert"); }
        if (key == SDLK_PAGEDOWN)    { return reg.Execute("page.next"); }
        if (key == SDLK_PAGEUP)      { return reg.Execute("page.prev"); }
        if (key == SDLK_HOME)        { return reg.Execute("canvas.reset_view"); }

        // Canvas Viewport hotkeys
        if (isCtrl && (key == SDLK_PLUS || key == SDLK_EQUALS)) { return reg.Execute("canvas.zoom_in"); }
        if (isCtrl && key == SDLK_MINUS) { return reg.Execute("canvas.zoom_out"); }
        if (isCtrl && key == SDLK_0)     { return reg.Execute("canvas.zoom_reset"); }

        return false;
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
                                    canvas.layerCompositor.GetLiveInteractionLayer().HasActiveInteraction() ||
                                    (modernNav.animatedTotalWidth != modernNav.GetTargetWidth()); // sidebar glide

            // 1. DRAIN HARDWARE INPUT: Process all pending input packets immediately at full digitizer speed (240Hz/360Hz/480Hz).
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    HandleGlobalShortcut(event);
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
                            ImportExternalPdf(droppedPath);
                        } else if (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov" || ext == ".avi" || 
                                   ext == ".ts" || ext == ".m2ts" || ext == ".mpg" || ext == ".mpeg" || ext == ".wmv" || 
                                   ext == ".flv" || ext == ".3gp" || ext == ".m4v" || ext == ".ogv") {
                            canvas.InsertVideoFromFile(droppedPath, &session);
                        } else if (ext == ".mp3" || ext == ".wav" || ext == ".m4a" || ext == ".flac" || 
                                   ext == ".ogg" || ext == ".aac" || ext == ".opus" || ext == ".wma" || ext == ".aiff") {
                            canvas.InsertAudioFromFile(droppedPath, &session);
                        }
                    }
                }
                else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                    CheckImportFolder();
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
                            HandleGlobalShortcut(event);
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

            // Periodically check the user Imports directory for documents from "Print to FolioNote"
            uint64_t currentTicksMs = SDL_GetTicks();
            if (currentTicksMs - lastImportScanTicksMs >= 2000) {
                lastImportScanTicksMs = currentTicksMs;
                CheckImportFolder();
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

            Folio::ShellLayoutMetrics layout = Folio::AppShell::ComputeLayout(
                screenW,
                screenH,
                isFullscreen,
                customTitleBar.isVisible,
                CustomTitleBar::TITLEBAR_HEIGHT,
                ribbon.GetAnimatedHeight(),
                modernNav.GetTotalWidth()
            );

            float titleBarH = layout.titleBarH;

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
                float ribbonH = layout.ribbonH;
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
                if (layout.showRibbonPullTab) {
                    ImGui::SetNextWindowPos(ImVec2(layout.pullTabX, layout.pullTabY));
                    ImGui::SetNextWindowSize(ImVec2(layout.pullTabW, layout.pullTabH));
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
                float contentY = layout.navY;
                float contentH = layout.navH;

                modernNav.Render(0.0f, contentY, contentH, session, canvas, themeManager, &currentView);

                // 3. CANVAS WORKSPACE OR DEDICATED PDF VIEWER (FILLS EXACT REMAINDER)
                float navW = layout.navW;
                float canvasX = layout.viewportX;
                float canvasW = layout.viewportW;

                // =========================================================
                // CANVAS VIEWPORT PRESERVATION ON PAGE SWITCH (CONTINUITY)
                // =========================================================
                // While the application session is active, each page preserves
                // its in-memory pan and zoom transform across all page transitions
                // without arbitrary time-based expiration.
                // Upon cold restart of the application, each page's in-memory
                // viewport starts default (hasCustomViewport = false), resetting
                // to homed (0, 0, 1.0) viewports as desired.
                auto activePg = session.GetActivePage();
                if (activePg && !activePg->isDedicatedPdf) {
                    std::string newGuid = activePg->guid;
                    if (newGuid != lastActivePageGuid) {
                        uint64_t nowMs = SDL_GetTicks();

                        // --- Save outgoing page viewport ---
                        if (!lastActivePageGuid.empty()) {
                            if (auto outPg = session.workspace.FindPageByGuid(lastActivePageGuid)) {
                                outPg->inMemoryViewport.panXMm            = canvas.transform.panXMm;
                                outPg->inMemoryViewport.panYMm            = canvas.transform.panYMm;
                                outPg->inMemoryViewport.zoom              = canvas.transform.zoom;
                                outPg->inMemoryViewport.hasCustomViewport = true;
                                outPg->inMemoryViewport.lastViewportAccessMs = nowMs;

                                // Persist active canvas template and layout settings back to outgoing page
                                outPg->infinityMode    = canvas.infinityMode;
                                outPg->paperStyle      = canvas.currentPaperStyle;
                                outPg->gridSpacingMm   = canvas.gridSpacingMm;
                                outPg->pageSizeFormat  = canvas.pageSizeFormat;
                                outPg->pageIsLandscape = canvas.pageIsLandscape;
                                outPg->pageWidthMm     = canvas.customPageWidthMm;
                                outPg->pageHeightMm    = canvas.customPageHeightMm;
                                outPg->showPageBorder  = canvas.showPageBorder;
                                outPg->pageBorderStyle = canvas.pageBorderStyle;
                                outPg->pageBorderWidth = canvas.pageBorderWidth;
                                outPg->pageBorderType  = canvas.pageBorderType;
                            }
                        }

                        // --- Restore per-page template, layout, and border properties ---
                        canvas.infinityMode           = activePg->infinityMode;
                        canvas.transform.infinityMode = activePg->infinityMode;
                        canvas.currentPaperStyle      = activePg->paperStyle;
                        canvas.gridSpacingMm          = activePg->gridSpacingMm;
                        canvas.pageSizeFormat         = activePg->pageSizeFormat;
                        canvas.pageIsLandscape        = activePg->pageIsLandscape;
                        canvas.customPageWidthMm      = activePg->pageWidthMm;
                        canvas.customPageHeightMm     = activePg->pageHeightMm;
                        canvas.showPageBorder         = activePg->showPageBorder;
                        canvas.pageBorderStyle        = activePg->pageBorderStyle;
                        canvas.pageBorderWidth        = activePg->pageBorderWidth;
                        canvas.pageBorderType         = activePg->pageBorderType;

                        // --- Restore or home incoming page viewport (Session Continuity) ---
                        auto& vp = activePg->inMemoryViewport;
                        if (vp.hasCustomViewport) {
                            // Viewport Continuity: restore user's exact pan and zoom across page switches
                            canvas.transform.panXMm = vp.panXMm;
                            canvas.transform.panYMm = vp.panYMm;
                            canvas.transform.zoom   = vp.zoom;
                        } else {
                            // Clean startup: home to nicely centered page with comfortable zoom
                            canvas.HomeOrCenterPage();
                            vp.panXMm = canvas.transform.panXMm;
                            vp.panYMm = canvas.transform.panYMm;
                            vp.zoom   = canvas.transform.zoom;
                            vp.hasCustomViewport = true;
                        }
                        vp.lastViewportAccessMs = nowMs;

                        lastActivePageGuid = newGuid;

                        // Persist active document resumption state to SettingsManager
                        auto activeNb = session.workspace.GetActiveNotebook();
                        if (activeNb) {
                            SettingsManager::Instance().lastActiveNotebookGuid = activeNb->guid;
                            auto sec = activeNb->GetActiveSection();
                            if (sec) {
                                SettingsManager::Instance().lastActiveSectionGuid = sec->guid;
                                SettingsManager::Instance().lastActivePageGuid = newGuid;
                            }
                        }

                        canvas.needsFullRebake = true;
                    }
                }

                // Periodic LRU eviction: enforce TTL and capacity cap (~1/s)
                {
                    uint64_t nowMs = SDL_GetTicks();
                    if (nowMs - lastLruCheckMs >= 1000) {
                        lastLruCheckMs = nowMs;
                        auto& sm = SettingsManager::Instance();
                        session.workspace.MaintainWorkingSetLRU(sm.lruInactivityTimeoutMs, sm.maxLoadedPages);
                    }
                }

                // Dedicated continuous PDF viewer
                if (activePg && activePg->isDedicatedPdf) {
                    pdfViewer.Render(canvasX, canvasW, screenH, titleBarH, ribbonH, session, inputManager.stateMachine, themeManager, canvas.inkColorInverted);
                    inputManager.wasCanvasImageHovered = pdfViewer.isPdfContentHovered;
                    inputManager.stateMachine.isCanvasHovered = false; // Prevent background canvas marquee selection
                    inputManager.stateMachine.isPdfCanvasHovered = pdfViewer.isPdfContentHovered;
                    inputManager.stateMachine.canvasOriginX = canvasX;
                    inputManager.stateMachine.canvasOriginY = contentY;
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
                    const bool isCanvasCapturingMouse = canvas.layerCompositor.GetLiveInteractionLayer().HasActiveInteraction() || 
                                                        canvas.selectionGizmo.isDragging || 
                                                        canvas.marqueeBox.isActive || 
                                                        (canvas.selectionMode == CanvasEngine::SelectionMode::Lasso && sm.mouse.leftButton) ||
                                                        (sm.currentAction == InteractionState::Panning && sm.mouse.leftButton);

                    if (!isHovered && !isCanvasCapturingMouse && sm.ActiveDevice == DeviceType::Mouse) {
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
                        canvas.Update(io.DeltaTime, &session);
                        std::vector<std::shared_ptr<CanvasObject>> visibleObjects = session.QueryVisible(canvas.GetViewport());
                        canvas.Render(visibleObjects, &session, io.DeltaTime);
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
                        inputManager.stateMachine.pointerIcons.Apply(inputManager.wasCanvasImageHovered, 
                            ImGui::GetIO().WantCaptureMouse && !inputManager.wasCanvasImageHovered);
                    } else {
                        inputManager.wasCanvasImageHovered = false;
                        inputManager.stateMachine.isCanvasHovered = false;
                        inputManager.stateMachine.pointerIcons.Apply(false, ImGui::GetIO().WantCaptureMouse);
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
                        
                        std::shared_ptr<CanvasObject> hitObj = nullptr;
                        if (auto pg = session.GetActivePage()) {
                            double hitRadius = 2.0 / canvas.transform.zoom; // 2mm world hit radius
                            hitObj = pg->HitTestForContextMenu(generalContextMenuWorldPos.x, generalContextMenuWorldPos.y, hitRadius);
                        }

                        if (hitObj) {
                            contextMenuManager.OpenForObject(hitObj, generalContextMenuWorldPos, session, canvas);
                        } else {
                            contextMenuManager.OpenForBackground(generalContextMenuWorldPos, session, canvas);
                        }
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
                    // =========================================================================
                    // GLOBAL UNIFIED CONTEXT MENU (Canvas Background & Canvas Objects)
                    // =========================================================================
                    contextMenuManager.Render(themeManager);
                }
                ImGui::End();
                ImGui::PopStyleVar();
                } // End of if (activePg && activePg->isDedicatedPdf) else

                // Floating Audio Controller Capsule Overlay
                overlayManager.RenderCanvasFloatingHUDs(
                    canvas, session,
                    ImVec2(inputManager.stateMachine.canvasOriginX, inputManager.stateMachine.canvasOriginY)
                );

                // 4. ADVANCED DOCUMENT OPTIONS SLIDING PANEL
                // Floats over canvas from right side when View tab -> Adv. Options is toggled.
                ribbon.RenderAdvancedOptionsPanel(screenW, screenH, titleBarH, ribbonH, canvas, themeManager);

                // 5. FLOATING COLLAPSED RIBBON OVERLAY
                // When ribbon is in collapsed mode and a tab is selected, this floats over canvas
                // without shifting/moving canvas, with pure shelf background (no orange), and auto-hides on canvas input.
                ribbon.RenderCollapsedPopup(screenW, titleBarH, canvas, inputManager.stateMachine, themeManager, &session);

            }

            // =========================================================
            // DIAGNOSTICS & MODAL OVERLAYS (Z-INDEX TOP PASS)
            // =========================================================
            if (ribbon.showDemoOverlay) {
                toolbarDemo.isVisible = true;
                ribbon.showDemoOverlay = false;
            }
            overlayManager.RenderModalsAndOverlays(canvas, inputManager.stateMachine, windowSM, session, themeManager);

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
        SettingsManager::Instance().isDynamicEraser = ribbon.isDynamicEraser;
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

        // Save active notebook, section, and page GUIDs for last-session resumption
        auto activeNb = session.workspace.GetActiveNotebook();
        if (activeNb) {
            SettingsManager::Instance().lastActiveNotebookGuid = activeNb->guid;
            auto sec = activeNb->GetActiveSection();
            if (sec) {
                SettingsManager::Instance().lastActiveSectionGuid = sec->guid;
                auto page = sec->GetActivePage();
                if (page) {
                    SettingsManager::Instance().lastActivePageGuid = page->guid;
                }
            }
        }

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