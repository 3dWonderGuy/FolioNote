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

#include "imgui.h"
#include "core/document/notebook.hpp"

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
     * @brief Quick check who owns what notebooks (if this notebook belongs to this library bundle)
     * @param nbPath Absolute path to a .notebook package.
     * @return true if nbPath resides directly within this library's rootPath.
     */
    [[nodiscard]] bool ContainsNotebook(const std::string& nbPath) const;
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
     * @brief Writes or refreshes self-identifying marker file (library.meta).
     *
     * @param folderPath Directory path of the library.
     * @param libraryName Display title of the library.
     */
    static void WriteLibraryMarker(const std::string& folderPath, const std::string& libraryName);

    /**
     * @brief Determines whether a given filesystem path is a recognized FolioNote library folder.
     *
     * Dual-mode check:
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
     *
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
     * @brief Normalizes and cleans a library path into a valid, canonical `.foliolib` directory path.
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
     * 3. Restores registered custom libraries from `config/settings.json`.
     * 4. Auto-discovers any physical `.foliolib` library bundles inside the app's default `Libraries/` directory.
     * 5. Scans disk directories and refreshes notebook catalogs for all registered libraries.
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
    // SETTINGS PERSISTENCE (config/settings.json via SettingsManager)
    // =====================================================================================

    /**
     * @brief Restores custom library registrations from the unified `config/settings.json`.
     */
    void LoadConfig();

    /**
     * @brief Persists all registered custom libraries into `config/settings.json`.
     */
    void SaveConfig();
};

} // namespace Folio

