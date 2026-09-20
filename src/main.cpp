/**
 * @file main.cpp
 * @brief Application entry point and CLI orchestration for FolioNote.
 *
 * GENERAL WORKING PROCESS & ARCHITECTURE:
 * ---------------------------------------
 * 1. CLI Dispatch & Headless Utility Mode:
 *    FolioNote supports headless command-line execution for administrative system
 *    integrations (such as virtual printer installation and query) as well as document
 *    import dispatching.
 *    - Headless printer configuration: Invoked by installers or automated scripts via
 *      `--install-printer`, `--uninstall-printer`, or `--check-printer`. These exit
 *      immediately with process code 0 (success) or 1 (failure) without initializing
 *      the graphical subsystem.
 *    - Direct document import: When launched via Explorer context menu ("Import into FolioNote"),
 *      SendTo shortcut, or direct shell command (`--import <file.pdf>` or bare `.pdf` path),
 *      the path is captured and forwarded into the GUI application lifecycle.
 *
 * 2. GUI Engine Initialization:
 *    If no headless termination flags were requested, the program instantiates the core
 *    `Application` instance and executes `app.Init(title, width, height, initialImportPath)`.
 *    - Initializes SDL3 video, custom modern window framing, and OpenGL 3.3 / GLES 3.0 context.
 *    - Connects database storage, themes, input devices, and canvas subsystems.
 *    - If an initial import path was provided, copies/stages the document into the dedicated
 *      user imports directory (`Documents/FolioNote/Imports`) and immediately presents the
 *      interactive `PdfImportModal` prompting the user where to place it in the notebook.
 *
 * 3. Event Loop Execution:
 *    Delegates runtime execution to `app.Run()`, maintaining 120Hz/60Hz adaptive frame pacing
 *    and continuous input state machine evaluation until the application terminates.
 */

#include "app/app.hpp"
#include <SDL3/SDL_main.h>

#include "utils/printer_installer.hpp"
#include <iostream>
#include <string>
#include <string_view>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "FolioNoteNative"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ANDROID: SDLActivity.java loads libmain.so and looks up the symbol "SDL_main"
// using dlsym() at runtime. If the function is compiled as plain C++ without
// extern "C", the compiler applies C++ name mangling (e.g. "_Z8SDL_mainiPPc")
// and dlsym() can't find it — causing an immediate silent exit.
// __attribute__((visibility("default"))) ensures the symbol isn't stripped by
// the linker even if -fvisibility=hidden is set globally in CMakeLists.txt.
extern "C" __attribute__((visibility("default"))) 
int SDL_main(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
#if defined(__ANDROID__)
    ALOG("FolioNote started successfully on Android!");
#endif

    // Initial PDF import target requested via command-line or shell integration
    std::string initialImportPath = "";

    // -------------------------------------------------------------------------
    // CLI ARGUMENT PARSING & SYSTEM COMMAND DISPATCH
    // -------------------------------------------------------------------------
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            std::cout << "FolioNote Modern Digital Notebook\n"
                      << "Usage: FolioNote [options] [document.pdf]\n\n"
                      << "Options:\n"
                      << "  --import <path>       Import a PDF document into FolioNote\n"
                      << "  --install-printer     Register 'Print to FolioNote' virtual printer (requires Admin UAC)\n"
                      << "  --uninstall-printer   Remove 'Print to FolioNote' virtual printer (requires Admin UAC)\n"
                      << "  --check-printer       Verify if 'Print to FolioNote' virtual printer is installed\n"
                      << "  -h, --help            Display this command-line help message\n"
                      << std::endl;
            return 0;
        } else if (arg == "--install-printer") {
            std::cout << "[FolioNote] Installing 'Print to FolioNote' virtual printer (requesting admin access)..." << std::endl;
            bool ok = Folio::PrinterInstaller::InstallPrinterElevated();
            std::cout << "[FolioNote] Virtual printer installation " << (ok ? "succeeded." : "failed or was cancelled.") << std::endl;
            return ok ? 0 : 1;
        } else if (arg == "--uninstall-printer") {
            std::cout << "[FolioNote] Removing 'Print to FolioNote' virtual printer (requesting admin access)..." << std::endl;
            bool ok = Folio::PrinterInstaller::UninstallPrinterElevated();
            std::cout << "[FolioNote] Virtual printer removal " << (ok ? "succeeded." : "failed or was cancelled.") << std::endl;
            return ok ? 0 : 1;
        } else if (arg == "--check-printer") {
            bool installed = Folio::PrinterInstaller::IsPrinterInstalled();
            std::cout << "[FolioNote] 'Print to FolioNote' printer is " << (installed ? "installed." : "not installed.") << std::endl;
            return installed ? 0 : 1;
        } else if (arg == "--import" && i + 1 < argc) {
            initialImportPath = argv[++i];
        } else if (arg.size() >= 4 && (arg.substr(arg.size() - 4) == ".pdf" || arg.substr(arg.size() - 4) == ".PDF")) {
            initialImportPath = std::string(arg);
        }
    }

#if defined(_WIN32)
    // -------------------------------------------------------------------------
    // SINGLE INSTANCE ENFORCEMENT & RUNNING INSTANCE ACTIVATION
    // -------------------------------------------------------------------------
    // Create or check the process mutex. If another instance already owns this mutex,
    // we hand off any requested import files directly to the running instance's
    // dedicated Imports directory and bring its existing window to the foreground.
    HANDLE hSingleInstanceMutex = CreateMutexW(NULL, FALSE, L"FolioNote_SingleInstance_Mutex_Global");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Forward document if an import path was specified
        if (!initialImportPath.empty()) {
            std::error_code ec;
            std::filesystem::path srcPath = std::filesystem::u8path(initialImportPath);
            if (std::filesystem::exists(srcPath, ec)) {
                PWSTR docsPathW = NULL;
                if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &docsPathW))) {
                    std::filesystem::path importsDir = std::filesystem::path(docsPathW) / "FolioNote" / "Imports";
                    CoTaskMemFree(docsPathW);
                    std::filesystem::create_directories(importsDir, ec);

                    std::filesystem::path destPath = importsDir / srcPath.filename();
                    int counter = 1;
                    while (std::filesystem::exists(destPath, ec)) {
                        std::string stem = srcPath.stem().string() + "_" + std::to_string(counter++);
                        destPath = importsDir / (stem + srcPath.extension().string());
                    }
                    std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
        }

        // Activate and bring the primary running FolioNote window to the foreground
        HWND hwnd = FindWindowW(NULL, L"FolioNote");
        if (hwnd) {
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            } else {
                ShowWindow(hwnd, SW_SHOW);
            }
            SetForegroundWindow(hwnd);
        }

        if (hSingleInstanceMutex) {
            CloseHandle(hSingleInstanceMutex);
        }
        return 0; // Seamlessly hand off and terminate secondary process
    }
#endif

    // -------------------------------------------------------------------------
    // APPLICATION LIFECYCLE INITIALIZATION
    // -------------------------------------------------------------------------
    Application app;
    if (!app.Init("FolioNote", 1920, 1080, initialImportPath)) {
#if defined(_WIN32)
        if (hSingleInstanceMutex) {
            CloseHandle(hSingleInstanceMutex);
        }
#endif
        return -1;
    }

    app.Run();
    app.Shutdown();

#if defined(_WIN32)
    if (hSingleInstanceMutex) {
        CloseHandle(hSingleInstanceMutex);
    }
#endif
    return 0;
}