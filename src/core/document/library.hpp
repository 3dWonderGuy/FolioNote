#pragma once
/**
 * =========================================================================================
 * @file library.hpp
 * @brief Manages FolioNote Library Bundles (.foliolib), Filesystem Discovery, and Notebook Routing
 * =========================================================================================
 *
 * =========================================================================================
 * ARCHITECTURAL MODEL & DISK HIERARCHY
 * =========================================================================================
 *
 * 1. THE GLOBAL APPLICATION DOCUMENT ROOT ("FolioNote/"):
 *    - Located at OS Documents/FolioNote (or sandboxed storage on mobile platforms).
 *    - IMPORTANT ARCHITECTURAL INVARIANT:
 *      The global "FolioNote" folder IS NOT a library itself! It is the top-level application
 *      container housing configuration, caches, background templates, exports, and libraries.
 *    - NO .notebook package is permitted to float loose inside the global application folder.
 *
 * 2. DUAL-MODE LIBRARIES (PACKAGED BUNDLES VS. UNPACKED FOLDERS):
 *    - Mode A (Packaged Bundle - ".foliolib"):
 *      A library directory ending with the ".foliolib" extension.
 *      Provides a clean, one-click document package feel in the OS:
 *        * Windows Explorer / macOS: Associated with FolioNote; double-clicking launches the app.
 *    - Mode B (Unpacked Folder - User stripped extension):
 *      If a user removes ".foliolib" in Explorer to inspect or manage its contents, the app
 *      continues to recognize the folder as a library and operates seamlessly without missing
 *      a beat. Identification is guaranteed by:
 *        1. The self-identifying internal marker file "library.meta"
 *        2. Or the presence of child ".notebook" packages
 *        3. Or an explicit registration entry in config/settings.json
 *
 * 3. NOTEBOOK PACKAGES (".notebook"):
 *    - Each notebook is an atomic directory bundle ending with ".notebook", residing strictly
 *      as a child of a library directory.
 *    - Notebooks can be dynamically moved between different libraries using `MoveNotebookToLibrary()`.
 *
 * PHYSICAL DIRECTORY LAYOUT:
 * ┌────────────────────────────────────────────────────────────────────────────────────────┐
 * │ <Documents>/FolioNote/                              (Global App Root Directory)        │
 * │  ├── config/                                        (Application configuration)        │
 * │  │    └── settings.json                             (User settings & registered libs)  │
 * │  ├── cache/                                         (Thumbnails & temp raster buffers) │
 * │  ├── exports/                                       (Rendered PDF & bitmap exports)    │
 * │  └── Libraries/                                     (Libraries Parent Directory)       │
 * │       ├── Default.foliolib/                         (Primary Default Library Bundle)   │
 * │       │    ├── library.meta                         (Self-identifying format marker)   │
 * │       │    ├── Biology 101.notebook/                (Encapsulated Notebook Package)    │
 * │       │    │    ├── structure.db                    (SQLite schema, metadata & FTS5)   │
 * │       │    │    └── pages/                          (Compressed .ink vector files)     │
 * │       │    └── Calculus.notebook/                                                      │
 * │       └── Personal/                                 (Unpacked Library Folder)          │
 * │            ├── library.meta                         (Retains library identity)         │
 * │            └── Journal.notebook/                                                       │
 * └────────────────────────────────────────────────────────────────────────────────────────┘
 *
 * 4. ERROR-HANDLING & LOGGING ARCHITECTURE:
 *    - All operations route diagnostics through Folio::LogSource::LibraryManager via:
 *        * LOG_INFO(LibraryManager, msg)  - Normal state transitions & operations
 *        * LOG_WARN(LibraryManager, msg)  - Recoverable issues, missing paths, fallbacks
 *        * LOG_ERROR(LibraryManager, msg) - I/O failures, access denials, corrupted state
 *    - Standard library std::error_code is captured across every filesystem call and logged.
 */

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <system_error>
#include <SDL3/SDL.h>

#include "imgui.h"
#include "core/document/notebook.hpp"
#include "core/storage/page_repository.hpp"
#include "app/settings_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

/// Extension appended to all library folder bundles for OS file association and identification.
inline constexpr const char* FOLIO_LIBRARY_EXTENSION = ".foliolib";

/// Self-identifying metadata marker file written inside all library folders.
/// Enables FolioNote to recognize the folder as a library even if the user removes the .foliolib extension.
inline constexpr const char* FOLIO_LIBRARY_MARKER_FILE = "library.meta";

/// Extension identifying individual notebook package bundles on disk.
inline constexpr const char* FOLIO_NOTEBOOK_EXTENSION = ".notebook";

/**
 * =========================================================================================
 * @struct LibraryInfo
 * @brief In-memory runtime telemetry and metadata for a registered library directory bundle.
 * =========================================================================================
 */
struct LibraryInfo {
    std::string id;                         ///< Unique identifier (e.g., "default_main", "lib_1726640000")
    std::string name;                       ///< User-visible display label (e.g., "Main Library", "School Notes")
    std::string rootPath;                   ///< Absolute filesystem path to the library directory
    bool isDefault = false;                 ///< True if this is the non-removable default primary library
    bool isAccessible = true;               ///< True if directory exists and is readable on the filesystem
    std::vector<std::string> notebookPaths; ///< Absolute paths to all discovered .notebook packages inside this bundle

    /**
     * @brief Checks if a given notebook path belongs to this library bundle.
     * @param nbPath Absolute path to a .notebook package.
     * @return true if nbPath resides directly within this library's rootPath.
     */
    [[nodiscard]] bool ContainsNotebook(const std::string& nbPath) const {
        std::error_code ec;
        std::filesystem::path p(nbPath);
        std::filesystem::path parent = p.parent_path();
        bool equiv = std::filesystem::equivalent(parent, std::filesystem::path(rootPath), ec);
        if (ec) {
            LOG_WARN(LibraryManager, "ContainsNotebook check error on path '" + nbPath + "': " + ec.message());
            return false;
        }
        return equiv;
    }
};

/**
 * =========================================================================================
 * @class LibraryManager
 * @brief Central controller for discovering, loading, creating, and migrating library bundles.
 * =========================================================================================
 */
class LibraryManager {
public:
    std::vector<LibraryInfo> libraries;   ///< All currently recognized library folders/bundles
    std::string globalAppFolder;          ///< Global application directory root (e.g. Documents/FolioNote)
    std::string defaultLibraryPath;       ///< Path to the default library bundle (e.g. FolioNote/Libraries/Default.foliolib)

    // =====================================================================================
    // PATH RESOLUTION & DUAL-MODE RECOGNITION HELPERS
    // =====================================================================================

    /**
     * @brief Writes or refreshes the internal self-identifying marker file (library.meta).
     * 
     * GUARANTEE OF ZERO BREAKAGE ON EXTENSION REMOVAL:
     * When a user removes the ".foliolib" extension from a folder to explore or inspect its
     * contents in Windows Explorer or macOS Finder, this marker file ensures that FolioNote
     * continues to recognize the folder as a library and operates seamlessly without missing a beat.
     *
     * @param folderPath Directory path of the library.
     * @param libraryName Display title of the library.
     */
    static void WriteLibraryMarker(const std::filesystem::path& folderPath, const std::string& libraryName) {
        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec)) {
            LOG_WARN(LibraryManager, "WriteLibraryMarker skipped: Target directory does not exist: " + folderPath.string());
            return;
        }

        std::filesystem::path markerPath = folderPath / FOLIO_LIBRARY_MARKER_FILE;
        std::ofstream out(markerPath);
        if (!out) {
            LOG_ERROR(LibraryManager, "Failed to write library marker file at: " + markerPath.string() + " (Permission denied or I/O failure)");
            return;
        }

        out << "# FolioNote Library Metadata\n";
        out << "format=FolioNoteLibrary\n";
        out << "version=1\n";
        out << "name=" << (libraryName.empty() ? "Library" : libraryName) << "\n";
        out << "timestamp=" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n";
        out.close();

        if (out.fail()) {
            LOG_ERROR(LibraryManager, "Failed while closing library marker stream at: " + markerPath.string());
        } else {
            LOG_INFO(LibraryManager, "Verified library marker at: " + markerPath.string());
        }
    }

    /**
     * @brief Determines whether a given filesystem path is a recognized FolioNote library folder.
     * 
     * DUAL-MODE RECOGNITION ARCHITECTURE:
     * 1. Packaged Mode (.foliolib bundle):
     *    Path ends with ".foliolib". Provides a clean one-click document package feel in the OS.
     * 2. Unpacked / Plain Folder Mode (User removed .foliolib extension):
     *    If the user removes the extension to explore contents, the app recognizes the directory if:
     *      a) It contains the self-identifying "library.meta" marker, OR
     *      b) It directly contains one or more child ".notebook" packages.
     *
     * @param path Target filesystem directory path to evaluate.
     * @return true if path represents an existing directory matching library criteria.
     */
    static bool IsLibraryFolder(const std::string& path) {
        if (path.empty()) return false;
        std::error_code ec;
        std::filesystem::path p(path);
        if (!std::filesystem::is_directory(p, ec)) {
            return false;
        }

        // 1. Packaged bundle check (.foliolib extension)
        if (p.extension() == FOLIO_LIBRARY_EXTENSION) return true;

        // 2. Self-identifying marker check (library.meta)
        if (std::filesystem::exists(p / FOLIO_LIBRARY_MARKER_FILE, ec)) return true;

        // 3. Child .notebook packages check
        auto dirIter = std::filesystem::directory_iterator(p, ec);
        if (ec) {
            LOG_WARN(LibraryManager, "IsLibraryFolder directory iterator warning for '" + path + "': " + ec.message());
            return false;
        }

        for (const auto& entry : dirIter) {
            if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == FOLIO_NOTEBOOK_EXTENSION) {
                return true;
            }
        }

        return false;
    }

    /// Alias for backwards compatibility across older call sites
    static bool IsLibraryBundle(const std::string& path) {
        return IsLibraryFolder(path);
    }

    /**
     * @brief Determines whether a given filesystem path is a valid .notebook package bundle.
     * @param path Target filesystem path to evaluate.
     * @return true if path represents an existing directory ending with ".notebook".
     */
    static bool IsNotebookPackage(const std::string& path) {
        if (path.empty()) return false;
        std::error_code ec;
        std::filesystem::path p(path);
        return std::filesystem::is_directory(p, ec) && p.extension() == FOLIO_NOTEBOOK_EXTENSION;
    }

    /**
     * @brief Resolves a library root path, tolerating addition or removal of the .foliolib extension.
     * 
     * SEAMLESS ADAPTATION LOGIC:
     * - If declaredPath exists -> use it.
     * - If declaredPath ("Research.foliolib") was renamed by the user to "Research" (extension removed),
     *   detects "Research" and updates path seamlessly!
     * - If declaredPath ("Research") was renamed to "Research.foliolib" (extension added),
     *   detects "Research.foliolib" and updates path seamlessly!
     *
     * @param declaredPath Path currently stored in configuration or memory.
     * @return The actual existing path on disk, or declaredPath if neither exists.
     */
    static std::string ResolveActualLibraryPath(const std::string& declaredPath) {
        std::error_code ec;
        std::filesystem::path p(declaredPath);

        // Case 1: Path exists directly as declared
        if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec)) {
            return declaredPath;
        }

        // Case 2: Declared path had .foliolib, but user stripped it in Explorer/Finder
        if (p.extension() == FOLIO_LIBRARY_EXTENSION) {
            std::filesystem::path stripped = p.parent_path() / p.stem();
            if (std::filesystem::exists(stripped, ec) && IsLibraryFolder(stripped.string())) {
                LOG_INFO(LibraryManager, "Auto-detected unpacked library folder (user removed .foliolib): " + stripped.string());
                return stripped.string();
            }
        }

        // Case 3: Declared path was plain, but user appended .foliolib to package it
        std::filesystem::path withExt = p.parent_path() / (p.filename().string() + FOLIO_LIBRARY_EXTENSION);
        if (std::filesystem::exists(withExt, ec) && IsLibraryFolder(withExt.string())) {
            LOG_INFO(LibraryManager, "Auto-detected packaged library bundle (user added .foliolib): " + withExt.string());
            return withExt.string();
        }

        LOG_WARN(LibraryManager, "Library path unresolved or missing on disk: " + declaredPath);
        return declaredPath;
    }

    /**
     * @brief Extracts a user-friendly display name from a library bundle folder path.
     * 
     * Example:
     *   "C:/Notes/Libraries/Medical Research.foliolib" -> "Medical Research"
     *   "C:/Notes/Libraries/Medical Research"          -> "Medical Research"
     *
     * @param bundlePath Filesystem path to the library folder.
     * @return Cleaned string without the .foliolib extension.
     */
    static std::string ExtractLibraryNameFromPath(const std::string& bundlePath) {
        std::filesystem::path p(bundlePath);
        std::string stem = p.stem().string();
        return stem.empty() ? "Unnamed Library" : stem;
    }

    /**
     * @brief Sanitizes a library directory path, supporting both .foliolib bundles and plain folders.
     *
     * @param inputPath User-supplied or configuration directory path.
     * @param defaultParentDir Parent directory to place bundle in if input is a relative name.
     * @param defaultBundleName Fallback base name (e.g. "Default").
     * @return Cleaned absolute path to a library directory.
     */
    static std::string SanitizeLibraryRoot(
        const std::string& inputPath,
        const std::string& defaultParentDir,
        const std::string& defaultBundleName = "Default"
    ) {
        std::filesystem::path p(inputPath);

        // 1. Climb out of any .notebook packages to prevent recursion errors
        while (!p.empty() && p.extension() == FOLIO_NOTEBOOK_EXTENSION) {
            p = p.parent_path();
        }

        // 2. If empty or relative dot, place default bundle in defaultParentDir
        if (p.empty() || p == "." || p == "FolioNote") {
            p = std::filesystem::path(defaultParentDir) / (defaultBundleName + FOLIO_LIBRARY_EXTENSION);
            return p.string();
        }

        // 3. If path points to an existing directory on disk, accept it as-is
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec)) {
            return p.string();
        }

        // 4. Default for brand new libraries: use .foliolib extension for clean bundle feel
        if (p.extension() != FOLIO_LIBRARY_EXTENSION) {
            std::string filename = p.filename().string();
            if (filename.empty()) filename = defaultBundleName;
            p = p.parent_path() / (filename + FOLIO_LIBRARY_EXTENSION);
        }

        return p.string();
    }

    // =====================================================================================
    // INITIALIZATION & DIRECTORY SCANNING
    // =====================================================================================

    /**
     * @brief Bootstraps the LibraryManager using the global application document directory.
     * 
     * WORKFLOW:
     * 1. Sets up `globalAppFolder` (e.g. `Documents/FolioNote`).
     * 2. Ensures standard subdirectories exist (`config/`, `Libraries/`, `cache/`, `exports/`).
     * 3. Configures the default primary library bundle: `FolioNote/Libraries/Default.foliolib`.
     * 4. Migrates any legacy loose notebooks found directly in `FolioNote/` into `Default.foliolib`.
     * 5. Restores custom user libraries from `config/settings.json`.
     * 6. Scans all registered libraries on disk to catalog their .notebook packages.
     *
     * @param globalAppRoot Absolute path to the global application directory.
     */
    void Init(const std::string& globalAppRoot) {
        std::error_code ec;
        LOG_INFO(LibraryManager, "Initializing LibraryManager. Input root: '" + globalAppRoot + "'");

        // 1. Resolve Global Application Document Root
        if (!globalAppRoot.empty()) {
            std::filesystem::path r(globalAppRoot);
            while (!r.empty() && (r.extension() == FOLIO_NOTEBOOK_EXTENSION || r.extension() == FOLIO_LIBRARY_EXTENSION)) {
                r = r.parent_path();
            }
            globalAppFolder = r.string();
        }

        if (globalAppFolder.empty() || globalAppFolder == ".") {
            const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
            if (docs) {
                globalAppFolder = (std::filesystem::path(docs) / "FolioNote").string();
            } else {
                globalAppFolder = "FolioNote";
                LOG_WARN(LibraryManager, "SDL_GetUserFolder returned null; falling back to local './FolioNote'");
            }
        }

        // 2. Ensure application subdirectories exist
        std::filesystem::path appPath(globalAppFolder);
        std::filesystem::path librariesContainer = appPath / "Libraries";
        
        std::filesystem::create_directories(librariesContainer, ec);
        if (ec) LOG_ERROR(LibraryManager, "Failed to create Libraries container: " + librariesContainer.string() + " (" + ec.message() + ")");

        std::filesystem::create_directories(appPath / "config", ec);
        if (ec) LOG_ERROR(LibraryManager, "Failed to create config folder: " + (appPath / "config").string() + " (" + ec.message() + ")");

        std::filesystem::create_directories(appPath / "cache", ec);
        if (ec) LOG_ERROR(LibraryManager, "Failed to create cache folder: " + (appPath / "cache").string() + " (" + ec.message() + ")");

        std::filesystem::create_directories(appPath / "exports", ec);
        if (ec) LOG_ERROR(LibraryManager, "Failed to create exports folder: " + (appPath / "exports").string() + " (" + ec.message() + ")");

        // 3. Configure Default Library Bundle inside Libraries/
        std::filesystem::path defaultLibFolder = librariesContainer / (std::string("Default") + FOLIO_LIBRARY_EXTENSION);
        defaultLibraryPath = defaultLibFolder.string();
        std::filesystem::create_directories(defaultLibFolder, ec);
        if (ec) {
            LOG_ERROR(LibraryManager, "Failed to create Default Library bundle: " + defaultLibraryPath + " (" + ec.message() + ")");
        } else {
            WriteLibraryMarker(defaultLibFolder, "Default Library");
        }

        libraries.clear();

        // 4. Register the Default Primary Library
        LibraryInfo defaultLib;
        defaultLib.id = "default_main";
        defaultLib.name = "Default Library";
        defaultLib.rootPath = defaultLibraryPath;
        defaultLib.isDefault = true;
        defaultLib.isAccessible = true;
        ScanLibraryNotebooks(defaultLib);
        libraries.push_back(defaultLib);

        // 5. Automatic Migration: If any legacy notebooks exist directly loose in FolioNote/,
        // move them into Default.foliolib to enforce the "no notebook outside library" rule.
        MigrateLooseNotebooksToDefault();

        // 6. Restore custom libraries from config/settings.json
        LoadConfig();

        // 7. Refresh all library catalogs
        RefreshAll();

        LOG_INFO(LibraryManager, "LibraryManager initialization complete. Default library: '" + defaultLibraryPath + 
                 "'. Total registered libraries: " + std::to_string(libraries.size()));
    }

    /**
     * @brief Scans a library bundle directory and catalogs all contained .notebook packages.
     * 
     * Preconditions: `lib.rootPath` must be a valid directory path.
     * Postconditions: `lib.notebookPaths` populated with valid `.notebook` paths; `lib.isAccessible` updated.
     *
     * @param lib Reference to the LibraryInfo struct to populate.
     */
    void ScanLibraryNotebooks(LibraryInfo& lib) {
        lib.notebookPaths.clear();
        std::error_code ec;

        // 1. Dynamically resolve actual directory in case user added or removed .foliolib
        std::string actualPath = ResolveActualLibraryPath(lib.rootPath);
        if (actualPath != lib.rootPath) {
            LOG_INFO(LibraryManager, "Library path auto-adapted: '" + lib.rootPath + "' -> '" + actualPath + "'");
            lib.rootPath = actualPath;
        }

        if (!std::filesystem::exists(lib.rootPath, ec) || !std::filesystem::is_directory(lib.rootPath, ec)) {
            lib.isAccessible = false;
            LOG_WARN(LibraryManager, "Library folder missing or inaccessible: '" + lib.rootPath + "' (" + ec.message() + ")");
            return;
        }

        lib.isAccessible = true;

        // Ensure self-identifying marker exists so folder remains a library even without .foliolib
        WriteLibraryMarker(lib.rootPath, lib.name);

        auto dirIter = std::filesystem::directory_iterator(lib.rootPath, ec);
        if (ec) {
            LOG_ERROR(LibraryManager, "Failed to scan library directory '" + lib.rootPath + "': " + ec.message());
            return;
        }

        for (const auto& entry : dirIter) {
            if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == FOLIO_NOTEBOOK_EXTENSION) {
                std::string nbPath = entry.path().string();
                lib.notebookPaths.push_back(nbPath);
            }
        }

        // Sort notebooks alphabetically by folder name for consistent UI display
        std::sort(lib.notebookPaths.begin(), lib.notebookPaths.end(), [](const std::string& a, const std::string& b) {
            return std::filesystem::path(a).stem() < std::filesystem::path(b).stem();
        });

        LOG_INFO(LibraryManager, "Scanned library '" + lib.name + "' (" + lib.rootPath + "): Found " + 
                 std::to_string(lib.notebookPaths.size()) + " notebook(s).");
    }

    /**
     * @brief Scans and refreshes the notebook catalog across all registered libraries.
     */
    void RefreshAll() {
        LOG_INFO(LibraryManager, "Refreshing all registered libraries (count: " + std::to_string(libraries.size()) + ")");
        for (auto& lib : libraries) {
            ScanLibraryNotebooks(lib);
        }
    }

    // =====================================================================================
    // LIBRARY MEMBERSHIP MANAGEMENT (CRUD)
    // =====================================================================================

    /**
     * @brief Registers an existing or new folder bundle as a managed library.
     * 
     * GUARANTEES:
     * - Disallows duplicate registrations of the same path.
     * - Automatically creates the folder on disk if it does not already exist.
     * - Writes self-identifying library.meta marker.
     * - Immediately persists the updated library list to `config/settings.json`.
     *
     * @param name Display label for this library (e.g. "School", "Work").
     * @param path Filesystem directory path.
     * @return true on successful registration; false if arguments are invalid or duplicate.
     */
    bool AddLibrary(const std::string& name, const std::string& path) {
        if (name.empty() || path.empty()) {
            LOG_ERROR(LibraryManager, "AddLibrary rejected: Empty library name or path provided.");
            return false;
        }

        std::filesystem::path p(path);
        std::error_code ec;

        // If path does not exist on disk and lacks .foliolib, add the bundle extension
        if (!std::filesystem::exists(p, ec) && p.extension() != FOLIO_LIBRARY_EXTENSION) {
            p += FOLIO_LIBRARY_EXTENSION;
        }

        std::string bundlePath = p.string();

        // Check for duplicates
        for (const auto& existing : libraries) {
            if (std::filesystem::equivalent(std::filesystem::path(existing.rootPath), p, ec)) {
                LOG_WARN(LibraryManager, "AddLibrary rejected: Library already registered at: " + bundlePath);
                return false;
            }
        }

        // Ensure physical directory exists on disk
        if (!std::filesystem::exists(p, ec)) {
            std::filesystem::create_directories(p, ec);
            if (ec) {
                LOG_ERROR(LibraryManager, "AddLibrary failed: Cannot create directory '" + bundlePath + "': " + ec.message());
                return false;
            }
        }

        // Ensure self-identifying marker is written
        WriteLibraryMarker(p, name);

        LibraryInfo lib;
        lib.id = "lib_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        lib.name = name;
        lib.rootPath = bundlePath;
        lib.isDefault = false;
        lib.isAccessible = true;

        ScanLibraryNotebooks(lib);
        libraries.push_back(lib);

        SaveConfig();
        LOG_INFO(LibraryManager, "Successfully registered library '" + name + "' at: " + bundlePath);
        return true;
    }

    /**
     * @brief Unregisters a custom library from the application.
     * 
     * NOTE: This only removes the library registration from FolioNote and its settings JSON.
     * It DOES NOT delete any files or notebooks from the physical filesystem.
     * The primary default library (index 0 / isDefault == true) cannot be removed.
     *
     * @param index Zero-based index into the `libraries` vector.
     * @return true if successfully unregistered, false if index is out of bounds or is default.
     */
    bool RemoveLibrary(size_t index) {
        if (index >= libraries.size()) {
            LOG_ERROR(LibraryManager, "RemoveLibrary rejected: Index " + std::to_string(index) + 
                      " exceeds registered count (" + std::to_string(libraries.size()) + ")");
            return false;
        }

        if (libraries[index].isDefault) {
            LOG_WARN(LibraryManager, "RemoveLibrary rejected: Cannot unregister the default primary library.");
            return false;
        }

        std::string removedName = libraries[index].name;
        std::string removedPath = libraries[index].rootPath;
        libraries.erase(libraries.begin() + index);

        SaveConfig();
        LOG_INFO(LibraryManager, "Successfully unregistered library '" + removedName + "' (" + removedPath + ")");
        return true;
    }

    /**
     * @brief Resolves a LibraryInfo pointer by its unique ID.
     * @param libId Target library identifier.
     * @return Pointer to LibraryInfo if found, or nullptr.
     */
    [[nodiscard]] LibraryInfo* FindLibraryById(const std::string& libId) {
        for (auto& lib : libraries) {
            if (lib.id == libId) return &lib;
        }
        LOG_WARN(LibraryManager, "FindLibraryById: No library found with ID: " + libId);
        return nullptr;
    }

    /**
     * @brief Resolves a LibraryInfo pointer by matching its root directory path.
     * @param bundlePath Target filesystem path.
     * @return Pointer to LibraryInfo if found, or nullptr.
     */
    [[nodiscard]] LibraryInfo* FindLibraryByPath(const std::string& bundlePath) {
        std::error_code ec;
        std::filesystem::path target(bundlePath);
        for (auto& lib : libraries) {
            if (std::filesystem::equivalent(std::filesystem::path(lib.rootPath), target, ec)) {
                return &lib;
            }
        }
        LOG_WARN(LibraryManager, "FindLibraryByPath: No library found matching path: " + bundlePath);
        return nullptr;
    }

    // =====================================================================================
    // NOTEBOOK ROUTING & CREATION (Enforcing Notebooks-in-Libraries Invariant)
    // =====================================================================================

    /**
     * @brief Creates a brand new .notebook package inside a designated library folder.
     * 
     * INVARIANTS & GUARANTEES:
     * 1. Every notebook MUST reside inside a library. If `targetLibraryPath` is empty
     *    or invalid, it automatically falls back to `defaultLibraryPath`.
     * 2. Automatically disambiguates name collisions (e.g. "Notes (1).notebook").
     * 3. Synchronously bootstraps the physical folder and initial SQLite structure.db schema
     *    before returning, guaranteeing the package is immediately ready for reading/writing.
     *
     * @param name Display title of the notebook (e.g. "Physics Lecture 1").
     * @param targetLibraryPath Absolute path to the destination library directory.
     * @param color UI accent color tag.
     * @param icon SVG icon name.
     * @return Shared pointer to initialized Notebook object, or nullptr on failure.
     */
    std::shared_ptr<Notebook> CreateNewNotebook(
        const std::string& name,
        const std::string& targetLibraryPath = "",
        ImVec4 color = ImVec4(0.20f, 0.48f, 0.92f, 1.0f),
        const std::string& icon = ""
    ) {
        std::error_code ec;

        // 1. Resolve destination library directory
        std::string libDir = targetLibraryPath;
        if (libDir.empty() || !IsLibraryFolder(libDir)) {
            LOG_WARN(LibraryManager, "CreateNewNotebook: Target library invalid or empty ('" + targetLibraryPath + 
                     "'); routing to default library: " + defaultLibraryPath);
            libDir = defaultLibraryPath;
        }

        std::filesystem::path parentBundle(libDir);
        if (!std::filesystem::exists(parentBundle, ec)) {
            std::filesystem::create_directories(parentBundle, ec);
            if (ec) {
                LOG_ERROR(LibraryManager, "CreateNewNotebook failed: Cannot create library directory: " + parentBundle.string() + " (" + ec.message() + ")");
                return nullptr;
            }
        }

        // 2. Disambiguate notebook filename to prevent overwriting existing packages
        std::string safeName = name.empty() ? "Untitled Notebook" : name;
        std::filesystem::path pkgPath = parentBundle / (safeName + FOLIO_NOTEBOOK_EXTENSION);

        int counter = 1;
        while (std::filesystem::exists(pkgPath, ec)) {
            pkgPath = parentBundle / (safeName + " (" + std::to_string(counter++) + ")" + FOLIO_NOTEBOOK_EXTENSION);
        }

        // 3. Instantiate in-memory Notebook model
        auto nb = std::make_shared<Notebook>(safeName, color, icon);
        nb->filePath = pkgPath.string();

        // 4. Synchronously initialize physical directory and SQLite schema
        Folio::PageRepository repo;
        if (repo.OpenNotebookPackage(nb->filePath)) {
            repo.SaveNotebookAsync(nb);
        } else {
            LOG_ERROR(LibraryManager, "CreateNewNotebook failed: SQLite package initialization failed at: " + nb->filePath);
            return nullptr;
        }

        RefreshAll();
        LOG_INFO(LibraryManager, "Successfully created new notebook '" + safeName + "' at: " + nb->filePath);
        return nb;
    }

    /**
     * @brief Moves an existing .notebook package from one library into another.
     * 
     * WORKFLOW & MATHEMATICAL SAFEGUARDS:
     * 1. Validates that source path exists and is a .notebook package.
     * 2. Validates that destination directory exists and is a recognized library folder.
     * 3. Disambiguates filename if a notebook with identical name already exists in target.
     * 4. Attempts atomic filesystem rename (`std::filesystem::rename`).
     * 5. If crossing filesystem boundary (e.g. C: drive to D: drive, EXDEV error), executes
     *    a transactional recursive copy followed by source deletion to prevent data loss.
     * 6. Refreshes library catalogs and outputs the new destination path.
     *
     * @param sourceNotebookPath Absolute path to the source .notebook package directory.
     * @param targetLibraryPath Absolute path to the target library directory.
     * @param outNewNotebookPath Optional output pointer receiving the final new package path.
     * @return true on successful move; false on validation error or disk I/O failure.
     */
    bool MoveNotebookToLibrary(
        const std::string& sourceNotebookPath,
        const std::string& targetLibraryPath,
        std::string* outNewNotebookPath = nullptr
    ) {
        std::error_code ec;

        // 1. Validation
        std::filesystem::path src(sourceNotebookPath);
        if (!std::filesystem::exists(src, ec) || src.extension() != FOLIO_NOTEBOOK_EXTENSION) {
            LOG_ERROR(LibraryManager, "MoveNotebook failed: Source is not a valid .notebook: " + sourceNotebookPath);
            return false;
        }

        std::filesystem::path dstLib(targetLibraryPath);
        if (!std::filesystem::exists(dstLib, ec) || !IsLibraryFolder(targetLibraryPath)) {
            LOG_ERROR(LibraryManager, "MoveNotebook failed: Target is not a valid library directory: " + targetLibraryPath);
            return false;
        }

        // If source is already in destination library, no move needed
        if (std::filesystem::equivalent(src.parent_path(), dstLib, ec)) {
            LOG_INFO(LibraryManager, "MoveNotebook: Notebook already resides in target library: " + targetLibraryPath);
            if (outNewNotebookPath) *outNewNotebookPath = sourceNotebookPath;
            return true;
        }

        // 2. Disambiguate destination package path
        std::string pkgName = src.filename().string();
        std::string baseStem = src.stem().string();
        std::filesystem::path dstPkg = dstLib / pkgName;

        int counter = 1;
        while (std::filesystem::exists(dstPkg, ec)) {
            dstPkg = dstLib / (baseStem + " (" + std::to_string(counter++) + ")" + FOLIO_NOTEBOOK_EXTENSION);
        }

        // 3. Perform move (Atomic rename first, fallback to copy+delete for cross-volume moves)
        std::filesystem::rename(src, dstPkg, ec);
        if (ec) {
            LOG_INFO(LibraryManager, "MoveNotebook: Atomic rename across volumes returned '" + ec.message() + 
                     "'. Initiating transactional recursive copy...");
            ec.clear();
            std::filesystem::copy(src, dstPkg, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                std::filesystem::remove_all(src, ec);
                if (ec) {
                    LOG_WARN(LibraryManager, "MoveNotebook: Target copied successfully, but source cleanup had error: " + ec.message());
                }
            } else {
                LOG_ERROR(LibraryManager, "MoveNotebook cross-volume transfer failed during copy: " + ec.message());
                std::filesystem::remove_all(dstPkg, ec); // Rollback partial copy
                return false;
            }
        }

        if (outNewNotebookPath) {
            *outNewNotebookPath = dstPkg.string();
        }

        RefreshAll();
        LOG_INFO(LibraryManager, "Successfully moved notebook from '" + sourceNotebookPath + "' to '" + dstPkg.string() + "'");
        return true;
    }

    /**
     * @brief Duplicates an existing notebook ("Save As Copy") into a designated library folder.
     * 
     * RESOLVES LAZY-LOAD WIPING DEFECT:
     * In previous implementations, cloning an unloaded page and invoking SaveNotebookAsync wiped
     * the on-disk .ink vector payload because in-memory strokes were empty.
     * This implementation directly duplicates the physical .ink files for all unloaded pages,
     * guaranteeing 100% vector content fidelity.
     *
     * @param sourceNb Shared pointer to the notebook being duplicated.
     * @param newName Display name for the copied notebook.
     * @param targetLibraryPath Destination directory (defaults to source library or defaultLibraryPath).
     * @return Shared pointer to newly created cloned Notebook, or nullptr on failure.
     */
    std::shared_ptr<Notebook> SaveAsCopy(
        const std::shared_ptr<Notebook>& sourceNb,
        const std::string& newName,
        const std::string& targetLibraryPath = ""
    ) {
        if (!sourceNb) {
            LOG_ERROR(LibraryManager, "SaveAsCopy rejected: Source notebook pointer is null.");
            return nullptr;
        }

        std::error_code ec;
        std::string destLib = targetLibraryPath;
        if (destLib.empty() || !IsLibraryFolder(destLib)) {
            if (!sourceNb->filePath.empty()) {
                std::filesystem::path srcParent = std::filesystem::path(sourceNb->filePath).parent_path();
                if (IsLibraryFolder(srcParent.string())) {
                    destLib = srcParent.string();
                }
            }
            if (destLib.empty()) destLib = defaultLibraryPath;
        }

        std::filesystem::path dir(destLib);
        if (!std::filesystem::exists(dir, ec)) {
            std::filesystem::create_directories(dir, ec);
            if (ec) {
                LOG_ERROR(LibraryManager, "SaveAsCopy failed: Cannot create destination directory: " + dir.string() + " (" + ec.message() + ")");
                return nullptr;
            }
        }

        std::string safeName = newName.empty() ? (sourceNb->name + " - Copy") : newName;
        std::filesystem::path newPkgPath = dir / (safeName + FOLIO_NOTEBOOK_EXTENSION);

        int counter = 1;
        while (std::filesystem::exists(newPkgPath, ec)) {
            newPkgPath = dir / (safeName + " (" + std::to_string(counter++) + ")" + FOLIO_NOTEBOOK_EXTENSION);
        }

        // Deep copy the notebook hierarchy structure
        auto copyNb = std::make_shared<Notebook>(safeName, sourceNb->colorTag, sourceNb->iconFile);
        copyNb->filePath = newPkgPath.string();
        copyNb->sections.clear();
        copyNb->sectionGroups.clear();

        for (const auto& sec : sourceNb->sections) {
            if (sec) copyNb->sections.push_back(sec->Clone());
        }
        for (const auto& grp : sourceNb->sectionGroups) {
            if (grp) {
                auto newGrp = std::make_shared<SectionGroup>(grp->name);
                for (const auto& sec : grp->sections) {
                    if (sec) newGrp->AddSection(sec->Clone());
                }
                copyNb->sectionGroups.push_back(newGrp);
            }
        }

        if (copyNb->sections.empty() && copyNb->sectionGroups.empty()) {
            copyNb->sections.push_back(std::make_shared<Section>("New Section"));
        }
        copyNb->activeSectionIndex = 0;

        // Persist structure to new SQLite package
        Folio::PageRepository repo;
        if (repo.OpenNotebookPackage(copyNb->filePath)) {
            repo.SaveNotebookAsync(copyNb);
        } else {
            LOG_ERROR(LibraryManager, "SaveAsCopy failed: Could not open package at: " + copyNb->filePath);
            return nullptr;
        }

        // Direct binary file copy for .ink vector files (prevents lazy-load wipe)
        if (!sourceNb->filePath.empty()) {
            std::filesystem::path srcPagesDir = std::filesystem::path(sourceNb->filePath) / "pages";
            std::filesystem::path dstPagesDir = newPkgPath / "pages";
            if (std::filesystem::exists(srcPagesDir, ec)) {
                std::filesystem::create_directories(dstPagesDir, ec);
                auto pageIter = std::filesystem::directory_iterator(srcPagesDir, ec);
                if (!ec) {
                    for (const auto& entry : pageIter) {
                        if (entry.path().extension() == ".ink") {
                            std::filesystem::copy_file(
                                entry.path(), 
                                dstPagesDir / entry.path().filename(), 
                                std::filesystem::copy_options::overwrite_existing, 
                                ec
                            );
                            if (ec) {
                                LOG_ERROR(LibraryManager, "SaveAsCopy: Failed to copy .ink file '" + entry.path().filename().string() + "': " + ec.message());
                                ec.clear();
                            }
                        }
                    }
                }
            }
        }

        RefreshAll();
        LOG_INFO(LibraryManager, "Successfully duplicated notebook '" + sourceNb->name + "' to: " + newPkgPath.string());
        return copyNb;
    }

    // =====================================================================================
    // SETTINGS PERSISTENCE (config/settings.json via SettingsManager)
    // =====================================================================================

    /**
     * @brief Restores custom library registrations from the unified `config/settings.json`.
     */
    void LoadConfig() {
        auto& sm = SettingsManager::Instance();
        std::error_code ec;
        size_t restoredCount = 0;

        for (const auto& entry : sm.registeredLibraries) {
            // Avoid duplicating the primary default library
            if (entry.isDefault || entry.rootPath == defaultLibraryPath) continue;

            std::string bundlePath = entry.rootPath;

            // Check if already in memory
            bool alreadyLoaded = false;
            for (const auto& existing : libraries) {
                if (std::filesystem::equivalent(std::filesystem::path(existing.rootPath), std::filesystem::path(bundlePath), ec)) {
                    alreadyLoaded = true;
                    break;
                }
            }
            if (alreadyLoaded) continue;

            LibraryInfo lib;
            lib.id = entry.id.empty() ? ("lib_" + std::to_string(libraries.size())) : entry.id;
            lib.name = entry.name.empty() ? ExtractLibraryNameFromPath(bundlePath) : entry.name;
            lib.rootPath = bundlePath;
            lib.isDefault = false;
            lib.isAccessible = std::filesystem::exists(bundlePath, ec);

            ScanLibraryNotebooks(lib);
            libraries.push_back(lib);
            restoredCount++;
        }

        LOG_INFO(LibraryManager, "Loaded " + std::to_string(restoredCount) + " custom libraries from settings.json");

        // Backward compatibility: If legacy libraries.cfg exists, import it once and delete it
        ImportLegacyConfig();
    }

    /**
     * @brief Persists all registered custom libraries into `config/settings.json`.
     */
    void SaveConfig() {
        auto& sm = SettingsManager::Instance();
        sm.registeredLibraries.clear();
        sm.defaultLibraryFolder = defaultLibraryPath;

        for (const auto& lib : libraries) {
            if (!lib.isDefault) {
                LibrarySettingEntry entry;
                entry.id = lib.id;
                entry.name = lib.name;
                entry.rootPath = lib.rootPath;
                entry.isDefault = false;
                sm.registeredLibraries.push_back(entry);
            }
        }

        if (sm.Save()) {
            LOG_INFO(LibraryManager, "Saved " + std::to_string(sm.registeredLibraries.size()) + " custom libraries to settings.json");
        } else {
            LOG_ERROR(LibraryManager, "Failed to save registered libraries to settings.json");
        }
    }

private:
    /**
     * @brief Automatically moves any legacy loose notebooks found directly in FolioNote/ into Default.foliolib.
     */
    void MigrateLooseNotebooksToDefault() {
        if (globalAppFolder.empty() || defaultLibraryPath.empty()) return;
        std::error_code ec;

        std::filesystem::path appDir(globalAppFolder);
        std::filesystem::path defaultLib(defaultLibraryPath);

        if (!std::filesystem::exists(appDir, ec) || !std::filesystem::is_directory(appDir, ec)) return;

        auto dirIter = std::filesystem::directory_iterator(appDir, ec);
        if (ec) {
            LOG_WARN(LibraryManager, "MigrateLooseNotebooks iterator warning: " + ec.message());
            return;
        }

        for (const auto& entry : dirIter) {
            if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == FOLIO_NOTEBOOK_EXTENSION) {
                std::filesystem::path targetPkg = defaultLib / entry.path().filename();
                if (!std::filesystem::exists(targetPkg, ec)) {
                    std::filesystem::rename(entry.path(), targetPkg, ec);
                    if (!ec) {
                        LOG_INFO(LibraryManager, "Auto-migrated loose notebook into Default library: " + entry.path().filename().string());
                    } else {
                        LOG_ERROR(LibraryManager, "Failed to migrate loose notebook '" + entry.path().filename().string() + "': " + ec.message());
                    }
                }
            }
        }
    }

    /**
     * @brief One-time migration of legacy `config/libraries.cfg` into `config/settings.json`.
     */
    void ImportLegacyConfig() {
        std::error_code ec;
        std::filesystem::path cfgPath = "config/libraries.cfg";
        if (!std::filesystem::exists(cfgPath, ec)) return;

        std::ifstream in(cfgPath);
        if (!in) {
            LOG_WARN(LibraryManager, "Could not open legacy libraries.cfg for migration.");
            return;
        }

        std::string line;
        bool importedAny = false;

        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t sep = line.find('|');
            if (sep != std::string::npos) {
                std::string name = line.substr(0, sep);
                std::string path = line.substr(sep + 1);
                if (AddLibrary(name, path)) {
                    importedAny = true;
                }
            }
        }
        in.close();

        if (importedAny) {
            LOG_INFO(LibraryManager, "Imported legacy libraries.cfg entries into settings.json");
            SaveConfig();
        }

        // Clean up legacy text file
        std::filesystem::remove(cfgPath, ec);
        if (ec) {
            LOG_WARN(LibraryManager, "Failed to delete legacy libraries.cfg after migration: " + ec.message());
        }
    }
};

} // namespace Folio

// Global namespace type aliases for backwards compatibility with UI components
using Folio::LibraryInfo;
using Folio::LibraryManager;
using Folio::FOLIO_LIBRARY_EXTENSION;
using Folio::FOLIO_NOTEBOOK_EXTENSION;
using Folio::FOLIO_LIBRARY_MARKER_FILE;
