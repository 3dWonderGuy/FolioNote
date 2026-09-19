#pragma once
/**
 * =========================================================================================
 * @file library.hpp
 * @brief Public API for the FolioNote Library Subsystem (Bundles, Discovery, Routing, Trash)
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DESIGN RATIONALE:
 * The Library subsystem is responsible for organizational domain logic in FolioNote:
 * 1. Tracking, indexing, and cataloging registered library directory bundles (.foliolib)
 *    and their child encapsulated notebook packages (.notebook).
 * 2. Enforcing the core architectural invariant: all notebooks must reside inside a library.
 * 3. Handling dual-mode library discovery (packaged .foliolib bundles vs unpacked folders
 *    identified by `library.meta` marker files).
 * 4. Path sanitization, collision-free routing, and cross-volume package migration.
 * 5. Quarantine and recovery of discarded notebooks in isolated `.trash/` directories.
 * 6. High-fidelity package cloning without unneeded stroke deserialization.
 *
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "imgui.h"
#include "core/document/notebook.hpp"

namespace Folio {

/// Extension appended to all library folder bundles for OS file association and identification.
inline constexpr const char* FOLIO_LIBRARY_EXTENSION = ".foliolib";

/// Self-identifying metadata marker file written inside all library folders.
/// Enables FolioNote to recognize the folder as a library even if the user strips the .foliolib extension.
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
     * @brief Checks whether an evaluated .notebook package path belongs directly to this library bundle.
     *
     * ALGORITHM:
     * 1. Extracts parent directory of `nbPath`.
     * 2. Compares parent path against `rootPath` using `FileManager::AreEquivalent` to normalize
     *    for trailing slashes and case sensitivity.
     *
     * @param nbPath Absolute path to a .notebook package directory.
     * @return true if nbPath resides directly within this library's rootPath.
     */
    [[nodiscard]] bool ContainsNotebook(const std::string& nbPath) const;
};

/**
 * =========================================================================================
 * @class NotebookCloner
 * @brief Package initialization and high-fidelity binary ink replicator.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Encapsulates low-level database operations (`PageRepository`) and direct binary vector file
 * cloning away from `LibraryManager`.
 */
class NotebookCloner {
public:
    /**
     * @brief Synchronously initializes a physical .notebook package directory and its SQLite schema.
     *
     * PROCESS:
     * 1. Creates `<notebook->filePath>/` and `<notebook->filePath>/pages/` subdirectories.
     * 2. Initializes the SQLite database schema (`pages.db`) using `PageRepository`.
     * 3. Persists initial notebook metadata and section hierarchy.
     *
     * @param notebook Shared pointer to the in-memory Notebook model.
     * @return true if the package was successfully initialized on disk; false otherwise.
     */
    static bool InitializePackage(const std::shared_ptr<Notebook>& notebook);

    /**
     * @brief Duplicates an existing notebook package with 100% vector fidelity ("Save As Copy").
     *
     * PREVENTING LAZY-LOAD STROKE WIPING:
     * Unrendered pages in memory contain empty stroke collections. Saving via memory serialization
     * would overwrite on-disk vector data. NotebookCloner clones section hierarchy and metadata,
     * initializes SQLite structure, and directly copies all raw `.ink` files byte-for-byte from
     * `<srcPkg>/pages/` to `<dstPkg>/pages/`.
     *
     * @param sourceNb Shared pointer to the source notebook being duplicated.
     * @param newName Proposed title for the duplicate notebook.
     * @param targetLibraryPath Destination library directory (falls back to fallbackLibraryPath).
     * @param fallbackLibraryPath Default library fallback if target is empty or invalid.
     * @return Shared pointer to newly cloned Notebook object, or nullptr on failure.
     */
    static std::shared_ptr<Notebook> CloneNotebook(
        const std::shared_ptr<Notebook>& sourceNb,
        const std::string& newName,
        const std::string& targetLibraryPath,
        const std::string& fallbackLibraryPath
    );

    /**
     * @brief Directly copies raw `.ink` binary vector files between package `pages/` directories.
     *
     * @param srcPackagePath Absolute filesystem path to source .notebook package directory.
     * @param dstPackagePath Absolute filesystem path to destination .notebook package directory.
     * @return Count of `.ink` page stroke files copied.
     */
    static size_t CopyPageInkFiles(
        const std::string& srcPackagePath,
        const std::string& dstPackagePath
    );
};

/**
 * =========================================================================================
 * @class LibraryManager
 * @brief Central controller for discovering, indexing, routing, and managing library bundles.
 * =========================================================================================
 */
class LibraryManager {
public:
    std::vector<LibraryInfo> libraries;   ///< All currently recognized library folders/bundles
    std::string globalAppFolder;          ///< Global application directory root (e.g. Documents/FolioNote)
    std::string defaultLibraryPath;       ///< Path to the default library bundle (e.g. FolioNote/Libraries/Default.foliolib)

    /// Callback fired whenever a library is added, removed, or refreshed, enabling app settings sync without coupling.
    std::function<void()> onLibrariesChanged;

    // =====================================================================================
    // PATH RESOLUTION & DUAL-MODE RECOGNITION HELPERS
    // =====================================================================================

    /**
     * @brief Writes or refreshes self-identifying marker file (library.meta).
     * @param folderPath Directory path of the library.
     * @param libraryName Display title of the library.
     */
    static void WriteLibraryMarker(const std::string& folderPath, const std::string& libraryName);

    /**
     * @brief Determines whether a given filesystem path is a recognized FolioNote library folder.
     *
     * DUAL-MODE HEURISTIC:
     * 1. Packaged Mode: ends with ".foliolib".
     * 2. Unpacked Mode: contains "library.meta" or child ".notebook" packages.
     *
     * @param path Target filesystem directory path to evaluate.
     * @return true if path represents an existing directory matching library criteria.
     */
    static bool IsLibraryFolder(const std::string& path);

    /**
     * @brief Determines whether a given filesystem path is a valid .notebook package bundle.
     * @param path Target filesystem path to evaluate.
     * @return true if path represents an existing directory ending with ".notebook".
     */
    static bool IsNotebookPackage(const std::string& path);

    /**
     * @brief Resolves a library root path, tolerating addition or removal of the .foliolib extension.
     * @param declaredPath Path currently stored in configuration or memory.
     * @return The actual existing path on disk, or declaredPath if neither exists.
     */
    static std::string ResolveActualLibraryPath(const std::string& declaredPath);

    /**
     * @brief Extracts a user-friendly display name from a library bundle folder path.
     * @param bundlePath Filesystem path to the library folder.
     * @return Cleaned string without the .foliolib extension.
     */
    static std::string ExtractLibraryNameFromPath(const std::string& bundlePath);

    /**
     * @brief Normalizes and cleans a library path into a valid, canonical directory path.
     *
     * SANITIZATION PROCESS:
     * 1. Climbs out of any nested `.notebook` package paths to prevent recursive nesting.
     * 2. Resolves empty/relative paths to `<defaultParentDir>/<defaultBundleName>.foliolib`.
     * 3. Preserves existing folders or appends `.foliolib` to new library bundle paths.
     * 4. Normalizes path separators to platform-standard format.
     *
     * @param inputPath User-supplied or configuration directory path.
     * @param defaultParentDir Fallback parent directory if input is empty or relative.
     * @param defaultBundleName Fallback bundle name (defaults to "Default").
     * @return Canonical, normalized absolute filesystem path to a library directory.
     */
    static std::string SanitizeLibraryRoot(
        const std::string& inputPath,
        const std::string& defaultParentDir,
        const std::string& defaultBundleName = "Default"
    );

    // =====================================================================================
    // INITIALIZATION & DIRECTORY SCANNING
    // =====================================================================================

    /**
     * @brief Bootstraps the LibraryManager subsystem on application startup.
     *
     * BOOTSTRAP PIPELINE:
     * 1. Resolves global app directory root and creates subdirectories (Libraries/, config/, cache/, exports/).
     * 2. Configures and registers primary default library bundle (Libraries/Default.foliolib).
     * 3. Auto-discovers any physical `.foliolib` library bundles inside the default `Libraries/` directory.
     * 4. Scans disk directories and refreshes notebook catalogs for all registered libraries.
     *
     * @param globalAppRoot Absolute path to the global application directory (e.g. Documents/FolioNote).
     */
    void Init(const std::string& globalAppRoot);

    /**
     * @brief Scans a specific library bundle directory and catalogs all contained .notebook packages.
     * @param lib Reference to the LibraryInfo struct to populate.
     */
    void ScanLibraryNotebooks(LibraryInfo& lib);

    /**
     * @brief Scans and refreshes the notebook catalog across all registered libraries.
     */
    void RefreshAll();

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
    bool AddLibrary(const std::string& name, const std::string& path);

    /**
     * @brief Unregisters an external custom library from the application.
     *
     * Safety Invariant:
     * - Protects all internal libraries residing inside the default `FolioNote/Libraries/` folder from being unlinked.
     * - Only unlinks external custom libraries stored outside the default library directory.
     *
     * @param index Zero-based index into the `libraries` vector.
     * @return true if successfully unregistered; false if index is out of bounds or points to an internal library.
     */
    bool RemoveLibrary(size_t index);

    /**
     * @brief Resolves a LibraryInfo pointer by its unique ID.
     * @param libId Target library identifier.
     * @return Pointer to LibraryInfo if found, or nullptr.
     */
    [[nodiscard]] LibraryInfo* FindLibraryById(const std::string& libId);
    [[nodiscard]] const LibraryInfo* FindLibraryById(const std::string& libId) const;

    /**
     * @brief Resolves a LibraryInfo pointer by matching its root directory path.
     * @param bundlePath Target filesystem path.
     * @return Pointer to LibraryInfo if found, or nullptr.
     */
    [[nodiscard]] LibraryInfo* FindLibraryByPath(const std::string& bundlePath);
    [[nodiscard]] const LibraryInfo* FindLibraryByPath(const std::string& bundlePath) const;

    // =====================================================================================
    // NOTEBOOK ROUTING & CREATION (Enforcing Notebooks-in-Libraries Invariant)
    // =====================================================================================

    /**
     * @brief Creates a brand new .notebook package inside a designated library folder.
     * Delegates physical package directory and SQLite initialization to `NotebookCloner::InitializePackage`.
     *
     * @param name Display title of the notebook (e.g. "Physics Lecture 1").
     * @param targetLibraryPath Absolute path to the destination library directory.
     * @param color UI accent color tag (defaults to random preset color).
     * @param icon SVG icon name.
     * @return Shared pointer to initialized Notebook object, or nullptr on failure.
     */
    std::shared_ptr<Notebook> CreateNewNotebook(
        const std::string& name,
        const std::string& targetLibraryPath = "",
        ImVec4 color = GetRandomSectionColor(),
        const std::string& icon = ""
    );

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
    );

    /**
     * @brief Duplicates an existing notebook ("Save As Copy") into a designated library folder.
     * Delegates full cloning and binary `.ink` copying to `NotebookCloner::CloneNotebook`.
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
    );

    // =====================================================================================
    // RECYCLE BIN / TRASH MANAGEMENT (.trash/ directories)
    // =====================================================================================

    /**
     * @brief Moves a .notebook package into its library's .trash/ directory for 30-day recovery.
     *
     * Invariants:
     * - The folder is moved without data destruction.
     * - If destination inside .trash already exists, appends a unique timestamp suffix.
     * - The library catalog is immediately refreshed to remove the notebook from active views.
     *
     * @param notebookPath Absolute path to the .notebook folder to trash.
     * @return true on successful move; false if notebook not found or disk error.
     */
    bool MoveNotebookToTrash(const std::string& notebookPath);

    /**
     * @brief Restores a notebook from .trash/ back to an active library directory.
     *
     * @param trashNotebookPath Absolute path to the trashed .notebook folder.
     * @param targetLibraryPath Optional destination library path (defaults to owning library).
     * @return true on successful restoration; false on failure.
     */
    bool RestoreNotebookFromTrash(const std::string& trashNotebookPath, const std::string& targetLibraryPath = "");

    /**
     * @brief Discovers all trashed .notebook folders inside a library's .trash/ subfolder.
     * @param libraryPath Absolute path to the library bundle.
     * @return List of absolute paths to trashed .notebook packages.
     */
    [[nodiscard]] std::vector<std::string> GetTrashNotebooks(const std::string& libraryPath) const;

    /**
     * @brief Permanently purges a trashed .notebook package from disk.
     *
     * @param trashNotebookPath Absolute path to the trashed notebook.
     * @return true if permanently deleted; false otherwise.
     */
    bool PermanentlyDeleteNotebookFromTrash(const std::string& trashNotebookPath);

    /**
     * @brief Permanently purges all contents of a library's .trash/ directory.
     * @param libraryPath Absolute path to the library bundle.
     * @return Count of trashed notebooks permanently erased.
     */
    size_t EmptyLibraryTrash(const std::string& libraryPath);
};

} // namespace Folio
