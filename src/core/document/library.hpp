#pragma once
/**
 * =========================================================================================
 * @file library.hpp
 * @brief Manages FolioNote Library Bundles (.foliolib), Filesystem Discovery, and Notebook Routing
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DESIGN RATIONALE:
 * The Library subsystem is responsible for organizational domain logic:
 * - Tracking and indexing registered libraries and their child .notebook packages.
 * - Enforcing the core architectural invariant: all notebooks must reside inside a library.
 * - Handling dual-mode library discovery (packaged .foliolib bundles vs unpacked folders).
 * - Routing new notebooks, moving packages between libraries, and notebook duplication.
 *
 * DECOUPLING & MODULARITY GUARANTEE:
 * All physical disk interactions, path normalization, platform directory queries, atomic file
 * persistence, and cross-volume operations are delegated to `Folio::FileManager`.
 * `LibraryManager` does not execute direct OS filesystem calls (`std::filesystem` / `std::fstream`),
 * allowing the underlying storage layer to be tested, adapted, or swapped without impacting
 * library organization logic.
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
 */

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>
#include <sstream>

#include "imgui.h"
#include "core/document/notebook.hpp"
#include "core/storage/page_repository.hpp"
#include "app/settings_manager.hpp"
#include "utils/file_manager.hpp"
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
        std::string parent = FileManager::GetParentPath(nbPath);
        return FileManager::AreEquivalent(parent, rootPath);
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
     * When a user removes the ".foliolib" extension from a folder in the OS file explorer,
     * this marker file ensures that FolioNote continues to recognize the folder as a library.
     *
     * @param folderPath Directory path of the library.
     * @param libraryName Display title of the library.
     */
    static void WriteLibraryMarker(const std::string& folderPath, const std::string& libraryName) {
        if (!FileManager::IsDirectory(folderPath)) {
            LOG_WARN(LibraryManager, "WriteLibraryMarker skipped: Target directory does not exist: " + folderPath);
            return;
        }

        std::string markerPath = FileManager::JoinPath(folderPath, FOLIO_LIBRARY_MARKER_FILE);
        std::string markerContent = 
            "# FolioNote Library Metadata\n"
            "format=FolioNoteLibrary\n"
            "version=1\n"
            "name=" + (libraryName.empty() ? "Library" : libraryName) + "\n"
            "timestamp=" + std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())) + "\n";

        if (FileManager::WriteTextAtomic(markerPath, markerContent)) {
            LOG_INFO(LibraryManager, "Verified library marker at: " + markerPath);
        } else {
            LOG_ERROR(LibraryManager, "Failed to write library marker file at: " + markerPath);
        }
    }

    /**
     * @brief Determines whether a given filesystem path is a recognized FolioNote library folder.
     *
     * DUAL-MODE RECOGNITION ARCHITECTURE:
     * 1. Packaged Mode (.foliolib bundle):
     *    Path ends with ".foliolib". Provides a clean one-click document package feel in the OS.
     * 2. Unpacked / Plain Folder Mode (User removed .foliolib extension):
     *    Recognized if:
     *      a) It contains the self-identifying "library.meta" marker, OR
     *      b) It directly contains one or more child ".notebook" packages.
     *
     * @param path Target filesystem directory path to evaluate.
     * @return true if path represents an existing directory matching library criteria.
     */
    static bool IsLibraryFolder(const std::string& path) {
        if (path.empty() || !FileManager::IsDirectory(path)) {
            return false;
        }

        // 1. Packaged bundle check (.foliolib extension)
        if (FileManager::HasExtension(path, FOLIO_LIBRARY_EXTENSION)) {
            return true;
        }

        // 2. Self-identifying marker check (library.meta)
        std::string markerPath = FileManager::JoinPath(path, FOLIO_LIBRARY_MARKER_FILE);
        if (FileManager::Exists(markerPath)) {
            return true;
        }

        // 3. Child .notebook packages check
        auto entries = FileManager::ListEntries(path, FOLIO_NOTEBOOK_EXTENSION);
        for (const auto& entry : entries) {
            if (entry.type == FileType::Directory) {
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
        return !path.empty() && FileManager::IsDirectory(path) && FileManager::HasExtension(path, FOLIO_NOTEBOOK_EXTENSION);
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
        if (declaredPath.empty()) return declaredPath;

        // Case 1: Path exists directly as declared
        if (FileManager::IsDirectory(declaredPath)) {
            return declaredPath;
        }

        std::string parent = FileManager::GetParentPath(declaredPath);

        // Case 2: Declared path had .foliolib, but user stripped it in Explorer/Finder
        if (FileManager::HasExtension(declaredPath, FOLIO_LIBRARY_EXTENSION)) {
            std::string stripped = FileManager::JoinPath(parent, FileManager::GetStem(declaredPath));
            if (FileManager::IsDirectory(stripped) && IsLibraryFolder(stripped)) {
                LOG_INFO(LibraryManager, "Auto-detected unpacked library folder (user removed .foliolib): " + stripped);
                return stripped;
            }
        }

        // Case 3: Declared path was plain, but user appended .foliolib to package it
        std::string withExt = FileManager::JoinPath(parent, FileManager::GetFileName(declaredPath) + FOLIO_LIBRARY_EXTENSION);
        if (FileManager::IsDirectory(withExt) && IsLibraryFolder(withExt)) {
            LOG_INFO(LibraryManager, "Auto-detected packaged library bundle (user added .foliolib): " + withExt);
            return withExt;
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
        std::string stem = FileManager::GetStem(bundlePath);
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
        std::string p = inputPath;

        // 1. Climb out of any .notebook packages to prevent recursion errors
        while (!p.empty() && FileManager::HasExtension(p, FOLIO_NOTEBOOK_EXTENSION)) {
            p = FileManager::GetParentPath(p);
        }

        // 2. If empty or relative dot, place default bundle in defaultParentDir
        if (p.empty() || p == "." || p == "FolioNote") {
            return FileManager::JoinPath(defaultParentDir, defaultBundleName + FOLIO_LIBRARY_EXTENSION);
        }

        // 3. If path points to an existing directory on disk, accept it as-is
        if (FileManager::IsDirectory(p)) {
            return FileManager::NormalizeSeparators(p);
        }

        // 4. Default for brand new libraries: use .foliolib extension for clean bundle feel
        if (!FileManager::HasExtension(p, FOLIO_LIBRARY_EXTENSION)) {
            std::string filename = FileManager::GetFileName(p);
            if (filename.empty()) filename = defaultBundleName;
            p = FileManager::JoinPath(FileManager::GetParentPath(p), filename + FOLIO_LIBRARY_EXTENSION);
        }

        return FileManager::NormalizeSeparators(p);
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
        LOG_INFO(LibraryManager, "Initializing LibraryManager. Input root: '" + globalAppRoot + "'");

        // 1. Resolve Global Application Document Root via FileManager
        globalAppFolder = FileManager::GetAppRootDirectory(globalAppRoot);

        // 2. Ensure application subdirectories exist
        FileManager::CreateDirectories(FileManager::GetLibrariesDirectory());
        FileManager::CreateDirectories(FileManager::GetConfigDirectory());
        FileManager::CreateDirectories(FileManager::GetCacheDirectory());
        FileManager::CreateDirectories(FileManager::GetExportsDirectory());

        // 3. Configure Default Library Bundle inside Libraries/
        defaultLibraryPath = FileManager::JoinPath(FileManager::GetLibrariesDirectory(), std::string("Default") + FOLIO_LIBRARY_EXTENSION);
        if (FileManager::CreateDirectories(defaultLibraryPath)) {
            WriteLibraryMarker(defaultLibraryPath, "Default Library");
        } else {
            LOG_ERROR(LibraryManager, "Failed to create Default Library bundle: " + defaultLibraryPath);
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
     * @param lib Reference to the LibraryInfo struct to populate.
     */
    void ScanLibraryNotebooks(LibraryInfo& lib) {
        lib.notebookPaths.clear();

        // 1. Dynamically resolve actual directory in case user added or removed .foliolib
        std::string actualPath = ResolveActualLibraryPath(lib.rootPath);
        if (actualPath != lib.rootPath) {
            LOG_INFO(LibraryManager, "Library path auto-adapted: '" + lib.rootPath + "' -> '" + actualPath + "'");
            lib.rootPath = actualPath;
        }

        if (!FileManager::IsDirectory(lib.rootPath)) {
            lib.isAccessible = false;
            LOG_WARN(LibraryManager, "Library folder missing or inaccessible: '" + lib.rootPath + "'");
            return;
        }

        lib.isAccessible = true;

        // Ensure self-identifying marker exists so folder remains a library even without .foliolib
        WriteLibraryMarker(lib.rootPath, lib.name);

        auto entries = FileManager::ListEntries(lib.rootPath, FOLIO_NOTEBOOK_EXTENSION);
        for (const auto& entry : entries) {
            if (entry.type == FileType::Directory) {
                lib.notebookPaths.push_back(entry.fullPath);
            }
        }

        // Sort notebooks alphabetically by folder name for consistent UI display
        std::sort(lib.notebookPaths.begin(), lib.notebookPaths.end(), [](const std::string& a, const std::string& b) {
            return FileManager::GetStem(a) < FileManager::GetStem(b);
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
     * @param name Display label for this library (e.g. "School", "Work").
     * @param path Filesystem directory path.
     * @return true on successful registration; false if arguments are invalid or duplicate.
     */
    bool AddLibrary(const std::string& name, const std::string& path) {
        if (name.empty() || path.empty()) {
            LOG_ERROR(LibraryManager, "AddLibrary rejected: Empty library name or path provided.");
            return false;
        }

        std::string bundlePath = path;
        if (!FileManager::Exists(bundlePath) && !FileManager::HasExtension(bundlePath, FOLIO_LIBRARY_EXTENSION)) {
            bundlePath += FOLIO_LIBRARY_EXTENSION;
        }

        // Check for duplicates
        for (const auto& existing : libraries) {
            if (FileManager::AreEquivalent(existing.rootPath, bundlePath)) {
                LOG_WARN(LibraryManager, "AddLibrary rejected: Library already registered at: " + bundlePath);
                return false;
            }
        }

        // Ensure physical directory exists on disk
        if (!FileManager::CreateDirectories(bundlePath)) {
            LOG_ERROR(LibraryManager, "AddLibrary failed: Cannot create directory '" + bundlePath + "'");
            return false;
        }

        // Ensure self-identifying marker is written
        WriteLibraryMarker(bundlePath, name);

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
        for (auto& lib : libraries) {
            if (FileManager::AreEquivalent(lib.rootPath, bundlePath)) {
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
        // 1. Resolve destination library directory
        std::string libDir = targetLibraryPath;
        if (libDir.empty() || !IsLibraryFolder(libDir)) {
            LOG_WARN(LibraryManager, "CreateNewNotebook: Target library invalid or empty ('" + targetLibraryPath + 
                     "'); routing to default library: " + defaultLibraryPath);
            libDir = defaultLibraryPath;
        }

        FileManager::CreateDirectories(libDir);

        // 2. Disambiguate notebook filename to prevent overwriting existing packages
        std::string safeName = name.empty() ? "Untitled Notebook" : name;
        std::string pkgPath = FileManager::DisambiguatePath(libDir, safeName, FOLIO_NOTEBOOK_EXTENSION);

        // 3. Instantiate in-memory Notebook model
        auto nb = std::make_shared<Notebook>(safeName, color, icon);
        nb->filePath = pkgPath;

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
        // 1. Validation
        if (!IsNotebookPackage(sourceNotebookPath)) {
            LOG_ERROR(LibraryManager, "MoveNotebook failed: Source is not a valid .notebook: " + sourceNotebookPath);
            return false;
        }

        if (!IsLibraryFolder(targetLibraryPath)) {
            LOG_ERROR(LibraryManager, "MoveNotebook failed: Target is not a valid library directory: " + targetLibraryPath);
            return false;
        }

        // If source is already in destination library, no move needed
        if (FileManager::AreEquivalent(FileManager::GetParentPath(sourceNotebookPath), targetLibraryPath)) {
            LOG_INFO(LibraryManager, "MoveNotebook: Notebook already resides in target library: " + targetLibraryPath);
            if (outNewNotebookPath) *outNewNotebookPath = sourceNotebookPath;
            return true;
        }

        // 2. Disambiguate destination package path
        std::string baseStem = FileManager::GetStem(sourceNotebookPath);
        std::string dstPkg = FileManager::DisambiguatePath(targetLibraryPath, baseStem, FOLIO_NOTEBOOK_EXTENSION);

        // 3. Perform move via FileManager (atomic rename with automatic cross-volume fallback)
        if (!FileManager::Move(sourceNotebookPath, dstPkg)) {
            LOG_ERROR(LibraryManager, "MoveNotebook failed to move package: " + sourceNotebookPath + " -> " + dstPkg);
            return false;
        }

        if (outNewNotebookPath) {
            *outNewNotebookPath = dstPkg;
        }

        RefreshAll();
        LOG_INFO(LibraryManager, "Successfully moved notebook from '" + sourceNotebookPath + "' to '" + dstPkg + "'");
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

        std::string destLib = targetLibraryPath;
        if (destLib.empty() || !IsLibraryFolder(destLib)) {
            if (!sourceNb->filePath.empty()) {
                std::string srcParent = FileManager::GetParentPath(sourceNb->filePath);
                if (IsLibraryFolder(srcParent)) {
                    destLib = srcParent;
                }
            }
            if (destLib.empty()) destLib = defaultLibraryPath;
        }

        FileManager::CreateDirectories(destLib);

        std::string safeName = newName.empty() ? (sourceNb->name + " - Copy") : newName;
        std::string newPkgPath = FileManager::DisambiguatePath(destLib, safeName, FOLIO_NOTEBOOK_EXTENSION);

        // Deep copy the notebook hierarchy structure
        auto copyNb = std::make_shared<Notebook>(safeName, sourceNb->colorTag, sourceNb->iconFile);
        copyNb->filePath = newPkgPath;
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
            std::string srcPagesDir = FileManager::JoinPath(sourceNb->filePath, "pages");
            std::string dstPagesDir = FileManager::JoinPath(newPkgPath, "pages");
            if (FileManager::IsDirectory(srcPagesDir)) {
                FileManager::CreateDirectories(dstPagesDir);
                auto pageEntries = FileManager::ListEntries(srcPagesDir, ".ink");
                for (const auto& entry : pageEntries) {
                    std::string dstInk = FileManager::JoinPath(dstPagesDir, entry.fileName);
                    FileManager::CopySingleFile(entry.fullPath, dstInk, true);
                }
            }
        }

        RefreshAll();
        LOG_INFO(LibraryManager, "Successfully duplicated notebook '" + sourceNb->name + "' to: " + newPkgPath);
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
        size_t restoredCount = 0;

        for (const auto& entry : sm.registeredLibraries) {
            // Avoid duplicating the primary default library
            if (entry.isDefault || entry.rootPath == defaultLibraryPath) continue;

            std::string bundlePath = entry.rootPath;

            // Check if already in memory
            bool alreadyLoaded = false;
            for (const auto& existing : libraries) {
                if (FileManager::AreEquivalent(existing.rootPath, bundlePath)) {
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
            lib.isAccessible = FileManager::IsDirectory(bundlePath);

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
        if (!FileManager::IsDirectory(globalAppFolder)) return;

        auto entries = FileManager::ListEntries(globalAppFolder, FOLIO_NOTEBOOK_EXTENSION);
        for (const auto& entry : entries) {
            if (entry.type == FileType::Directory) {
                std::string targetPkg = FileManager::JoinPath(defaultLibraryPath, entry.fileName);
                if (!FileManager::Exists(targetPkg)) {
                    if (FileManager::Move(entry.fullPath, targetPkg)) {
                        LOG_INFO(LibraryManager, "Auto-migrated loose notebook into Default library: " + entry.fileName);
                    } else {
                        LOG_ERROR(LibraryManager, "Failed to migrate loose notebook '" + entry.fileName + "'");
                    }
                }
            }
        }
    }

    /**
     * @brief One-time migration of legacy `config/libraries.cfg` into `config/settings.json`.
     */
    void ImportLegacyConfig() {
        std::string cfgPath = FileManager::JoinPath(FileManager::GetConfigDirectory(), "libraries.cfg");
        if (!FileManager::Exists(cfgPath)) return;

        std::string content;
        if (!FileManager::ReadText(cfgPath, content)) {
            LOG_WARN(LibraryManager, "Could not read legacy libraries.cfg for migration.");
            return;
        }

        std::istringstream in(content);
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

        if (importedAny) {
            LOG_INFO(LibraryManager, "Imported legacy libraries.cfg entries into settings.json");
            SaveConfig();
        }

        // Clean up legacy text file
        FileManager::RemoveFile(cfgPath);
    }
};

} // namespace Folio

// Global namespace type aliases for backwards compatibility with UI components
using Folio::LibraryInfo;
using Folio::LibraryManager;
using Folio::FOLIO_LIBRARY_EXTENSION;
using Folio::FOLIO_NOTEBOOK_EXTENSION;
using Folio::FOLIO_LIBRARY_MARKER_FILE;
