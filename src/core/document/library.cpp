/**
 * =========================================================================================
 * @file library.cpp
 * @brief Implementation of Library Subsystem: Bundles (.foliolib), Discovery, and Routing
 * =========================================================================================
 *
 * ARCHITECTURAL OVERVIEW & ROLES:
 * This translation unit implements the organizational domain layer of FolioNote:
 * 1. Physical Library & Notebook Package Discovery:
 *    Dual-mode detection supporting packaged directory bundles (*.foliolib) and unpacked
 *    marker-identified folders (library.meta or containing *.notebook bundles).
 * 2. Invariant Enforcement:
 *    Ensures that every notebook strictly belongs to a parent library container.
 * 3. Atomic State Persistence & Configuration Synchronization:
 *    Synchronizes registered custom libraries with the unified application settings
 *    (`config/settings.json`) via SettingsManager.
 * 4. Content-Safe Notebook Operations:
 *    Provides safe creation, cross-library movement (with fallback across filesystem volumes),
 *    and vector-fidelity notebook duplication (duplicating raw .ink files to avoid lazy-load wipes).
 */

#include "core/document/library.hpp"

#include <chrono>
#include <algorithm>
#include <sstream>

#include "core/document/notebook.hpp"
#include "core/storage/page_repository.hpp"
#include "app/settings_manager.hpp"
#include "utils/file_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

// =========================================================================================
// LibraryInfo Implementation
// =========================================================================================

/**
 * @brief Checks if a given notebook path belongs directly to this library bundle.
 *
 * GENERAL WORKING PROCESS & ALGORITHM:
 * 1. Takes the child notebook package path (e.g. "C:/Notes/Default.foliolib/Math.notebook").
 * 2. Extracts the parent directory path via `FileManager::GetParentPath`.
 * 3. Normalizes both paths (resolving trailing slashes, uppercase/lowercase on case-insensitive
 *    filesystems, and directory separator variations like '/' vs '\\') using
 *    `FileManager::AreEquivalent`.
 *
 * @param nbPath Absolute or relative filesystem path to an evaluated .notebook package.
 * @return bool True if `nbPath` is an immediate child inside this library's `rootPath`.
 */
bool LibraryInfo::ContainsNotebook(const std::string& nbPath) const {
    std::string parent = FileManager::GetParentPath(nbPath);
    return FileManager::AreEquivalent(parent, rootPath);
}

// =========================================================================================
// LibraryManager: Path Resolution & Dual-Mode Recognition Helpers
// =========================================================================================

/**
 * @brief Writes or refreshes the self-identifying marker file (library.meta).
 *
 * ARCHITECTURAL PURPOSE & EXTENSION STRIPPING RESILIENCE:
 * When users view libraries in OS File Explorers, they may rename the directory and remove
 * the ".foliolib" extension. The `library.meta` file ensures that FolioNote can unambiguously
 * identify the folder as a library and restore its display title.
 *
 * ALGORITHMIC STEPS:
 * 1. Validate that the target path exists and is indeed a directory.
 * 2. Construct the absolute marker filepath: `<folderPath>/library.meta`.
 * 3. Format key-value metadata text including format identifier, schema version, name, and timestamp.
 * 4. Write to disk using `FileManager::WriteTextAtomic` (write to temp file then rename) to prevent
 *    file corruption during sudden shutdowns or power loss.
 *
 * @param folderPath Absolute filesystem directory path of the library bundle.
 * @param libraryName User-visible display label of the library.
 */
void LibraryManager::WriteLibraryMarker(const std::string& folderPath, const std::string& libraryName) {
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
 * DUAL-MODE DETECTION RULES & LOGIC:
 * A directory is classified as a valid library if it meets ANY of the following criteria:
 * 1. Extension Check: The directory path has the extension `.foliolib`.
 * 2. Metadata Marker Check: Contains a file named `library.meta`.
 * 3. Child Notebook Check: Contains at least one subfolder ending with `.notebook`.
 *
 * @param path Target filesystem path to evaluate.
 * @return bool True if the path represents an existing directory fulfilling library criteria.
 */
bool LibraryManager::IsLibraryFolder(const std::string& path) {
    if (path.empty() || !FileManager::IsDirectory(path)) {
        return false;
    }

    // Rule 1: Packaged bundle check (.foliolib extension)
    if (FileManager::HasExtension(path, FOLIO_LIBRARY_EXTENSION)) {
        return true;
    }

    // Rule 2: Self-identifying marker check (library.meta)
    std::string markerPath = FileManager::JoinPath(path, FOLIO_LIBRARY_MARKER_FILE);
    if (FileManager::Exists(markerPath)) {
        return true;
    }

    // Rule 3: Child .notebook packages check
    auto entries = FileManager::ListEntries(path, FOLIO_NOTEBOOK_EXTENSION);
    for (const auto& entry : entries) {
        if (entry.type == FileType::Directory) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Determines whether a given filesystem path is a valid .notebook package bundle.
 *
 * VALIDATION LOGIC:
 * Path must not be empty, must be a directory on disk, and must have the `.notebook` extension.
 *
 * @param path Filesystem path to evaluate.
 * @return bool True if directory is a `.notebook` bundle.
 */
bool LibraryManager::IsNotebookPackage(const std::string& path) {
    return !path.empty() && FileManager::IsDirectory(path) && FileManager::HasExtension(path, FOLIO_NOTEBOOK_EXTENSION);
}

/**
 * @brief Resolves a library root path on disk, tolerating user modification of the `.foliolib` extension.
 *
 * SEAMLESS HEURISTIC PROCESS:
 * - Case 1 (Nominal): The declared path exists as-is on the disk. Return immediately.
 * - Case 2 (Extension Stripped): The declared path ended with `.foliolib` (e.g. `Research.foliolib`),
 *   but the user removed the extension in the OS file explorer (renamed to `Research`). Check if
 *   `parent/stem` exists and satisfies `IsLibraryFolder`. If so, auto-adapt.
 * - Case 3 (Extension Added): The declared path was plain (e.g. `Research`), but the user renamed it
 *   to `Research.foliolib`. Check if `parent/filename + .foliolib` exists and satisfies `IsLibraryFolder`.
 * - Fallback: Return `declaredPath` unchanged if no alternate match is found.
 *
 * @param declaredPath Path currently stored in configuration or runtime memory.
 * @return std::string Resolved existing filesystem path, or `declaredPath` if neither variant exists.
 */
std::string LibraryManager::ResolveActualLibraryPath(const std::string& declaredPath) {
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
 * @brief Extracts a clean, user-friendly display name from a library bundle folder path.
 *
 * PROCESS:
 * Strips directory components and the `.foliolib` extension.
 * Example: `"C:/Notes/Libraries/Medical Research.foliolib"` -> `"Medical Research"`
 *
 * @param bundlePath Filesystem path to the library folder.
 * @return std::string Cleaned display string (fallback: "Unnamed Library").
 */
std::string LibraryManager::ExtractLibraryNameFromPath(const std::string& bundlePath) {
    std::string stem = FileManager::GetStem(bundlePath);
    return stem.empty() ? "Unnamed Library" : stem;
}

/**
 * @brief Sanitizes a library directory path, supporting both .foliolib bundles and plain folders.
 *
 * SANITIZATION ALGORITHM:
 * 1. Loop-strips any trailing `.notebook` package paths to prevent nested encapsulation errors.
 * 2. If the path is empty, ".", or matches "FolioNote", routes to `<defaultParentDir>/<defaultBundleName>.foliolib`.
 * 3. If the path already points to an existing directory on disk, normalizes slashes and accepts as-is.
 * 4. For new paths lacking the `.foliolib` extension, appends `.foliolib` to maintain bundle consistency.
 * 5. Normalizes all path separators to platform-canonical format.
 *
 * @param inputPath User-supplied or configuration directory path.
 * @param defaultParentDir Fallback parent directory to place bundle in.
 * @param defaultBundleName Fallback base name (defaults to "Default").
 * @return std::string Cleaned, canonical absolute path to a library directory.
 */
std::string LibraryManager::SanitizeLibraryRoot(
    const std::string& inputPath,
    const std::string& defaultParentDir,
    const std::string& defaultBundleName
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
 * 6. Migrates any legacy loose notebooks found in the root `FolioNote/` folder into `Default.foliolib`.
 * 7. Loads registered custom libraries from `config/settings.json`.
 * 8. Refreshes catalogs for all registered libraries.
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

    // 5. Restore custom libraries from config/settings.json
    LoadConfig();

    // 7. Refresh all library catalogs
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
 * 6. Appends new `LibraryInfo` to `libraries` vector and persists to configuration.
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
 * @brief Unregisters a custom library from the application runtime and settings.
 *
 * SAFETY INVARIANTS:
 * - Does NOT delete physical files or notebooks from disk.
 * - Cannot remove the default library (index 0 / `isDefault == true`).
 *
 * @param index Index into the `libraries` vector.
 * @return bool True if successfully unregistered; false on invalid index or default library.
 */
bool LibraryManager::RemoveLibrary(size_t index) {
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
 * @return LibraryInfo* Pointer to matching struct, or nullptr if not found.
 */
LibraryInfo* LibraryManager::FindLibraryById(const std::string& libId) {
    for (auto& lib : libraries) {
        if (lib.id == libId) return &lib;
    }
    LOG_WARN(LibraryManager, "FindLibraryById: No library found with ID: " + libId);
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

// =========================================================================================
// LibraryManager: Notebook Routing & Creation
// =========================================================================================

/**
 * @brief Creates a brand new .notebook package inside a designated library folder.
 *
 * INVARIANTS & GUARANTEES:
 * 1. Library Invariant: Every notebook MUST reside inside a library. If `targetLibraryPath`
 *    is invalid or empty, routing automatically falls back to `defaultLibraryPath`.
 * 2. Collision Avoidance: Automatically disambiguates collisions (e.g. "Notes (1).notebook").
 * 3. Immediate Usability: Synchronously initializes the SQLite `structure.db` schema via
 *    `PageRepository`, guaranteeing the package is immediately ready for reading/writing.
 *
 * @param name Display title of the notebook.
 * @param targetLibraryPath Absolute path to destination library (or empty for default).
 * @param color UI accent color tag.
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
 * VECTOR-CONTENT FIDELITY (PREVENTING LAZY-LOAD STROKE WIPING):
 * Pages in FolioNote are lazily loaded. If a notebook is cloned and its pages haven't been
 * paged into memory, saving them via the repository would overwrite disk ink strokes with empty
 * stroke collections.
 * To achieve 100% fidelity:
 * 1. Deep-clones section hierarchy and page metadata in-memory.
 * 2. Initializes the new SQLite structure schema in the destination bundle.
 * 3. Physically copies the raw `.ink` binary files directly from `<srcPkg>/pages/` into `<dstPkg>/pages/`.
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

// =========================================================================================
// LibraryManager: Settings Persistence
// =========================================================================================

/**
 * @brief Restores custom library registrations from the unified `config/settings.json`.
 */
void LibraryManager::LoadConfig() {
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
}

/**
 * @brief Persists all registered custom libraries into `config/settings.json`.
 */
void LibraryManager::SaveConfig() {
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


} // namespace Folio
