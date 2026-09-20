/**
 * =========================================================================================
 * @file library.cpp
 * @brief Lifecycle, Registration, Catalog Refreshing, and Organizational Routing
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Manages runtime library registration and cataloging for FolioNote:
 * 1. Subsystem Bootstrapping: Ensures required system directories exist and establishes
 *    the non-removable default library bundle (`FolioNote/Libraries/Default.foliolib`).
 * 2. Automatic Discovery: Detects any physical `.foliolib` folders stored inside `Libraries/`.
 * 3. In-Memory Catalog: Populates and sorts child `.notebook` paths across all libraries.
 * 4. High-Level Routing Delegation:
 *    - Creation: Routes destination directory and delegates physical package initialization
 *      to `NotebookCloner::InitializePackage`.
 *    - Duplication: Routes destination directory and delegates binary deep copy to
 *      `NotebookCloner::CloneNotebook`.
 *    - Movement: Validates and moves packages across library boundaries via `FileManager::Move`.
 *
 * MODULARITY & DECOUPLING:
 * Does NOT interact with `PageRepository` or SQLite directly.
 * Does NOT depend on `SettingsManager` or serialize application JSON. Fires `onLibrariesChanged`
 * to notify external subscribers when membership changes.
 */

#include "core/document/library/library.hpp"

#include <chrono>
#include <algorithm>
#include <string>

#include "utils/file_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

// =========================================================================================
// LibraryManager: Initialization & Directory Scanning
// =========================================================================================

/**
 * @brief Bootstraps the LibraryManager using the global application document directory.
 *
 * BOOTSTRAP PIPELINE:
 * 1. Resolves `globalAppFolder` via `FileManager::GetAppRootDirectory`.
 * 2. Creates core system subdirectories (`config/`, `Libraries/`, `cache/`, `exports/`).
 * 3. Configures and registers primary default library bundle: `FolioNote/Libraries/Default.foliolib`.
 * 4. Writes self-identifying marker (`library.meta`) in default library bundle.
 * 5. Scans default library to catalog contained `.notebook` packages.
 * 6. Auto-discovers any physical `.foliolib` bundles inside the default `Libraries/` folder.
 * 7. Refreshes catalogs for all registered libraries.
 *
 * @param globalAppRoot User document root or customized root directory path.
 */
void LibraryManager::Init(const std::string& globalAppRoot) {
    LOG_INFO(LibraryManager, "Initializing LibraryManager. Input root: '" + globalAppRoot + "'");

    // 1. Resolve Global Application Document Root via FileManager
    globalAppFolder = FileManager::GetAppRootDirectory(globalAppRoot);

    // 2. Ensure application subdirectories exist
    FileManager::CreateDirectories(FileManager::GetLibrariesDirectory());
    FileManager::CreateDirectories(FileManager::GetConfigDirectory());
    FileManager::CreateDirectories(FileManager::GetCacheDirectory());
    FileManager::CreateDirectories(FileManager::GetExportsDirectory());

    // 3. Configure Default Library Bundle inside Libraries/
    std::string defaultLibPlain = FileManager::JoinPath(FileManager::GetLibrariesDirectory(), "Default");
    std::string defaultLibWithExt = FileManager::JoinPath(FileManager::GetLibrariesDirectory(), std::string("Default") + FOLIO_LIBRARY_EXTENSION);
    if (FileManager::IsDirectory(defaultLibPlain)) {
        defaultLibraryPath = defaultLibPlain;
    } else if (FileManager::IsDirectory(defaultLibWithExt)) {
        defaultLibraryPath = defaultLibWithExt;
    } else {
        defaultLibraryPath = defaultLibPlain;
    }

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

    // 5. Auto-discover any physical library bundles inside the app's default Libraries/ folder (with or without .foliolib)
    std::string librariesDir = FileManager::GetLibrariesDirectory();
    auto libEntries = FileManager::ListEntries(librariesDir);
    for (const auto& entry : libEntries) {
        if (entry.type == FileType::Directory && IsLibraryFolder(entry.fullPath)) {
            bool alreadyRegistered = false;
            for (const auto& existing : libraries) {
                if (FileManager::AreEquivalent(existing.rootPath, entry.fullPath)) {
                    alreadyRegistered = true;
                    break;
                }
            }
            if (!alreadyRegistered) {
                LibraryInfo autoLib;
                autoLib.id = "lib_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                autoLib.name = ExtractLibraryNameFromPath(entry.fullPath);
                autoLib.rootPath = entry.fullPath;
                autoLib.isDefault = false;
                autoLib.isAccessible = true;
                ScanLibraryNotebooks(autoLib);
                libraries.push_back(autoLib);
                LOG_INFO(LibraryManager, "Auto-discovered library folder in app root: '" + entry.fullPath + "'");
            }
        }
    }

    // 6. Refresh all library catalogs
    RefreshAll();

    LOG_INFO(LibraryManager, "LibraryManager initialization complete. Default library: '" + defaultLibraryPath +
             "'. Total registered libraries: " + std::to_string(libraries.size()));
}

/**
 * @brief Scans a library bundle directory and catalogs all contained .notebook packages.
 *
 * SCAN PROCESS:
 * 1. Clears existing in-memory `notebookPaths` list.
 * 2. Checks for folder renaming via `ResolveActualLibraryPath`.
 * 3. Verifies directory accessibility. Marks `isAccessible = false` if unreachable.
 * 4. Ensures `library.meta` marker is present.
 * 5. Queries filesystem for all directory entries ending with `.notebook`.
 * 6. Sorts notebook paths lexicographically by package stem name for deterministic UI ordering.
 *
 * @param lib In-out reference to the LibraryInfo model to update.
 */
void LibraryManager::ScanLibraryNotebooks(LibraryInfo& lib) {
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
void LibraryManager::RefreshAll() {
    LOG_INFO(LibraryManager, "Refreshing all registered libraries (count: " + std::to_string(libraries.size()) + ")");
    for (auto& lib : libraries) {
        ScanLibraryNotebooks(lib);
    }
}

// =========================================================================================
// LibraryManager: Library Membership Management (CRUD)
// =========================================================================================

/**
 * @brief Registers an existing or new folder bundle as a managed library.
 *
 * VALIDATION & DEDUPLICATION:
 * 1. Rejects empty names or paths.
 * 2. Appends `.foliolib` if the path does not exist and has no library extension.
 * 3. Compares against all registered library paths using `FileManager::AreEquivalent`.
 * 4. Ensures directory exists on disk via `FileManager::CreateDirectories`.
 * 5. Writes `library.meta` marker file.
 * 6. Appends new `LibraryInfo` to `libraries` vector and fires `onLibrariesChanged`.
 *
 * @param name Display label for this library (e.g. "Work", "Mathematics").
 * @param path Filesystem directory path.
 * @return bool True on successful registration; false on duplicate or error.
 */
bool LibraryManager::AddLibrary(const std::string& name, const std::string& path) {
    if (name.empty() || path.empty()) {
        LOG_ERROR(LibraryManager, "AddLibrary rejected: Empty library name or path provided.");
        return false;
    }

    std::string bundlePath = FileManager::NormalizeSeparators(path);

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

    WriteLibraryMarker(bundlePath, name);

    LibraryInfo lib;
    lib.id = "lib_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    lib.name = name;
    lib.rootPath = bundlePath;
    lib.isDefault = false;
    lib.isAccessible = true;

    ScanLibraryNotebooks(lib);
    libraries.push_back(lib);

    if (onLibrariesChanged) {
        onLibrariesChanged();
    }

    LOG_INFO(LibraryManager, "Successfully registered library '" + name + "' at: " + bundlePath);
    return true;
}

/**
 * @brief Unregisters an external custom library from the application.
 *
 * SAFETY INVARIANTS:
 * 1. Default Library Protection: The primary default library (index 0 / isDefault == true) can NEVER be removed.
 * 2. Internal Bundle Protection: Libraries residing directly inside the default `FolioNote/Libraries/`
 *    folder cannot be unlinked.
 *
 * @param index Zero-based index into the `libraries` vector.
 * @return bool True if successfully unregistered; false on invariant violation or out-of-bounds.
 */
bool LibraryManager::RemoveLibrary(size_t index) {
    if (index >= libraries.size()) {
        LOG_ERROR(LibraryManager, "RemoveLibrary rejected: Index " + std::to_string(index) +
                  " is out of bounds (size: " + std::to_string(libraries.size()) + ")");
        return false;
    }

    if (libraries[index].isDefault) {
        LOG_WARN(LibraryManager, "RemoveLibrary rejected: Cannot unregister the default primary library.");
        return false;
    }

    // Invariant: Do not unlink internal libraries residing inside default Libraries folder
    std::string libParent = FileManager::GetParentPath(libraries[index].rootPath);
    std::string defaultLibDir = FileManager::GetLibrariesDirectory();
    if (FileManager::AreEquivalent(libParent, defaultLibDir)) {
        LOG_WARN(LibraryManager, "RemoveLibrary rejected: Cannot unregister internal library inside default Libraries directory: '" + libraries[index].rootPath + "'");
        return false;
    }

    std::string removedName = libraries[index].name;
    std::string removedPath = libraries[index].rootPath;
    libraries.erase(libraries.begin() + index);

    if (onLibrariesChanged) {
        onLibrariesChanged();
    }

    LOG_INFO(LibraryManager, "Successfully unregistered library '" + removedName + "' (" + removedPath + ")");
    return true;
}

/**
 * @brief Resolves a LibraryInfo pointer by its unique ID.
 * @param libId Target library identifier.
 * @return LibraryInfo* Pointer to matching struct, or nullptr if not found.
 */
LibraryInfo* LibraryManager::FindLibraryById(const std::string& libId) {
    for (auto& lib : libraries) {
        if (lib.id == libId) return &lib;
    }
    LOG_WARN(LibraryManager, "FindLibraryById: No library found with ID: " + libId);
    return nullptr;
}

const LibraryInfo* LibraryManager::FindLibraryById(const std::string& libId) const {
    for (const auto& lib : libraries) {
        if (lib.id == libId) return &lib;
    }
    return nullptr;
}

/**
 * @brief Resolves a LibraryInfo pointer by matching its root directory path.
 * @param bundlePath Target filesystem path.
 * @return LibraryInfo* Pointer to matching struct, or nullptr if not found.
 */
LibraryInfo* LibraryManager::FindLibraryByPath(const std::string& bundlePath) {
    for (auto& lib : libraries) {
        if (FileManager::AreEquivalent(lib.rootPath, bundlePath)) {
            return &lib;
        }
    }
    LOG_WARN(LibraryManager, "FindLibraryByPath: No library found matching path: " + bundlePath);
    return nullptr;
}

const LibraryInfo* LibraryManager::FindLibraryByPath(const std::string& bundlePath) const {
    for (const auto& lib : libraries) {
        if (FileManager::AreEquivalent(lib.rootPath, bundlePath)) {
            return &lib;
        }
    }
    return nullptr;
}

// =========================================================================================
// LibraryManager: Notebook Routing & Creation
// =========================================================================================

/**
 * @brief Creates a brand new .notebook package inside a designated library folder.
 *
 * INVARIANTS & GUARANTEES:
 * 1. Library Invariant: Every notebook MUST reside inside a library. If `targetLibraryPath`
 *    is invalid or empty, routing automatically falls back to `defaultLibraryPath`.
 * 2. Collision Avoidance: Disambiguates notebook package names via `FileManager::DisambiguatePath`.
 * 3. Package Initialization: Delegates physical folder and SQLite schema creation to
 *    `NotebookCloner::InitializePackage(nb)`.
 *
 * @param name Display title of the notebook.
 * @param targetLibraryPath Absolute path to destination library (or empty for default).
 * @param color UI accent color tag (defaults to random preset color via GetRandomSectionColor()).
 * @param icon SVG icon name.
 * @return std::shared_ptr<Notebook> Initialized Notebook object, or nullptr on failure.
 */
std::shared_ptr<Notebook> LibraryManager::CreateNewNotebook(
    const std::string& name,
    const std::string& targetLibraryPath,
    ImVec4 color,
    const std::string& icon
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
    if (nb->sections.empty()) {
        nb->sections.push_back(std::make_shared<Section>("New Section"));
    }
    nb->activeSectionIndex = 0;

    // 4. Create physical package directory and child pages folder
    if (!NotebookCloner::InitializePackage(nb)) {
        LOG_ERROR_CODE(LibraryManager, FolioErrorCode::DocNotebookCreateFailed, 
                       "CreateNewNotebook failed: Could not create package container at: " + nb->filePath);
        return nullptr;
    }

    RefreshAll();
    LOG_INFO(LibraryManager, "Successfully created new notebook '" + safeName + "' at: " + nb->filePath);
    return nb;
}

/**
 * @brief Moves an existing .notebook package from one library into another.
 *
 * ATOMIC RENAMING & CROSS-VOLUME MECHANICS:
 * 1. Verifies source is a valid `.notebook` package and target is a valid library directory.
 * 2. Checks if package is already in destination (no-op).
 * 3. Generates a collision-free target package path via `FileManager::DisambiguatePath`.
 * 4. Executes movement via `FileManager::Move`. If moving across filesystem drives / volumes,
 *    `FileManager::Move` handles recursive copy and source deletion transparently.
 * 5. Refreshes all library indices.
 *
 * @param sourceNotebookPath Absolute path to the source notebook bundle.
 * @param targetLibraryPath Absolute path to the destination library.
 * @param outNewNotebookPath Optional output pointer receiving the final new package path.
 * @return bool True on success, false on validation failure or disk error.
 */
bool LibraryManager::MoveNotebookToLibrary(
    const std::string& sourceNotebookPath,
    const std::string& targetLibraryPath,
    std::string* outNewNotebookPath
) {
    // 1. Validation
    if (!IsNotebookPackage(sourceNotebookPath)) {
        LOG_ERROR_CODE(LibraryManager, FolioErrorCode::DocNotebookNotFound, 
                       "MoveNotebook failed: Source is not a valid .notebook: " + sourceNotebookPath);
        return false;
    }

    if (!IsLibraryFolder(targetLibraryPath)) {
        LOG_ERROR_CODE(LibraryManager, FolioErrorCode::DocLibraryNotFound, 
                       "MoveNotebook failed: Target is not a valid library directory: " + targetLibraryPath);
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
 * Delegates structure cloning and vector ink preservation to `NotebookCloner::CloneNotebook`.
 *
 * @param sourceNb Shared pointer to the notebook being duplicated.
 * @param newName Display name for the duplicated notebook.
 * @param targetLibraryPath Destination library path (defaults to source's library or default).
 * @return std::shared_ptr<Notebook> Cloned Notebook object, or nullptr on failure.
 */
std::shared_ptr<Notebook> LibraryManager::SaveAsCopy(
    const std::shared_ptr<Notebook>& sourceNb,
    const std::string& newName,
    const std::string& targetLibraryPath
) {
    auto copyNb = NotebookCloner::CloneNotebook(sourceNb, newName, targetLibraryPath, defaultLibraryPath);
    if (copyNb) {
        RefreshAll();
    }
    return copyNb;
}

} // namespace Folio
