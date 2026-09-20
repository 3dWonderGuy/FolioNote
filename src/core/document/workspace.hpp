#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <filesystem>
#include "core/document/notebook.hpp"
#include "core/document/library/library.hpp"
#include "core/storage/page_repository.hpp"
#include "utils/logger.hpp"

/**
 * =========================================================================================
 * @file workspace.hpp
 * @brief Represents the user's root workspace, managing open Notebooks and storage.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * - The Workspace is the top-level container for all open Notebooks in the application.
 * - It owns the primary `Folio::PageRepository` instance, acting as the gateway to notebook
 *   directory storage (SQLite `structure.db` hierarchy and self-contained `.ink` page BLOBs).
 *
 * CORE LIFECYCLE RESPONSIBILITIES:
 * 1. Workspace Discovery & Initialization (`LoadWorkspace`):
 *    - Scans the designated root directory (e.g. `FolioNote/`) and library directories for `.notebook` folders.
 *    - Automatically deserializes notebook hierarchies, sections, and page metadata via `PageRepository`.
 *    - If no notebooks exist on disk, it auto-generates a `DemoBook.notebook` directory with default schema.
 * 2. On-Demand Lazy Loading:
 *    - In `GetActivePage()`, if the requested page is marked `!isLoaded` (its vector strokes, paper settings,
 *      and canvas objects have not yet been read from its `.ink` file or were evicted to save memory), the
 *      Workspace loads it on-demand from disk via `repository.LoadPage(page)`.
 * 3. Asynchronous Persistence (`FlushActiveNotebookAsync`):
 *    - Dispatches save tasks to background worker threads via `PageRepository::SaveNotebookAsync`,
 *      preventing UI hitching while serializing self-contained `.ink` page files and committing
 *      hierarchy metadata to SQLite.
 * 4. Dual-Axis LRU Working-Set Cache Eviction (`MaintainWorkingSetLRU`):
 *    - Periodically called to evict in-memory strokes of inactive pages that haven't been accessed
 *      for a configurable timeout or when resident pages exceed the capacity cap, keeping RAM usage
 *      strictly bounded regardless of notebook library size.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Two-Tier Discovery vs. Open Working Set (OneNote Model):
 *   - Separate the Library Catalog (`LibraryManager` / Notebook Hub discovery) from the Active
 *     Working Set (`Workspace::notebooks`).
 *   - Instead of auto-mounting every discovered notebook on disk into the active sidebar on startup
 *     (which can bloat UI and memory if a user has 50+ notebooks), maintain a persistent list of
 *     "Opened Notebooks" in session state.
 *   - Provide a "Close Notebook" action that unmounts a notebook from `Workspace::notebooks`
 *     (hiding it from the active sidebar without deleting it from disk) and an "Open More Notebooks"
 *     dialog to mount notebooks from the library catalog into the working set on demand.
 * - Session State Persistence: Remember which notebook, section, and page was open on last exit.
 * - Multi-Notebook Tabs: Switch between multiple notebooks in separate UI tabs or split-views.
 * - Cloud / Sync Bridge: Check for file updates or cloud sync notifications on active notebooks.
 * - Background Auto-Save Timer: Automatically trigger `FlushActiveNotebookAsync` on a periodic schedule.
 */
class Workspace {
public:
    // -------------------------------------------------------------------------
    // Notebooks & Storage Repository
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<Notebook>> notebooks;   ///< All currently open notebooks in this workspace
    size_t activeNotebookIndex = 0;                      ///< Index of the currently active/viewed notebook
    mutable Folio::PageRepository repository;            ///< Notebook storage repository (structure.db hierarchy + .ink files) and async queue
    std::string workspaceDirectory;                      ///< Root directory containing notebooks (e.g. Documents/FolioNote)

    // -------------------------------------------------------------------------
    // Workspace Loading & Initialization
    // -------------------------------------------------------------------------

    /**
     * @brief Discovers and opens notebooks residing within library folders.
     * 
     * INVARIANT ENFORCEMENT:
     * - The global application folder (e.g. Documents/FolioNote) is NOT a library itself.
     * - All notebooks reside inside a library folder (e.g. Libraries/Default).
     * - If no notebooks exist, generates a default DemoBook inside the default library.
     * - If any legacy notebooks exist loose at the root, migrates them into the default library.
     *
     * @param directoryPath Global application document directory path (e.g. Documents/FolioNote).
     */
    void LoadWorkspace(const std::string& directoryPath) {
        notebooks.clear();
        bool foundAny = false;

        std::filesystem::path dir(directoryPath);
        while (!dir.empty() && (dir.extension() == ".notebook" || dir.extension() == ".foliolib" || dir.filename().string().find(".notebook") != std::string::npos)) {
            dir = dir.parent_path();
        }
        workspaceDirectory = dir.empty() ? directoryPath : dir.string();
        LOG_INFO(General, "Workspace: Initializing root directory at: " + workspaceDirectory);

        std::error_code ec;
        std::filesystem::path appRoot(workspaceDirectory);
        std::filesystem::path librariesDir = appRoot / "Libraries";
        std::filesystem::path defaultLib = librariesDir / "Default";
        if (!std::filesystem::exists(defaultLib, ec) && std::filesystem::exists(librariesDir / "Default.foliolib", ec)) {
            defaultLib = librariesDir / "Default.foliolib";
        }

        std::filesystem::create_directories(defaultLib, ec);

        // Helper lambda to scan a library folder for .notebook directories
        auto scanLibraryFolder = [this, &foundAny, &ec](const std::filesystem::path& libPath) {
            if (!std::filesystem::exists(libPath, ec) || !std::filesystem::is_directory(libPath, ec)) return;
            LOG_INFO(General, "Workspace: Scanning library folder: " + libPath.string());
            for (const auto& entry : std::filesystem::directory_iterator(libPath, ec)) {
                if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == ".notebook") {
                    if (auto nb = repository.LoadNotebookHierarchy(entry.path().string())) {
                        LOG_INFO(Notebook, "Workspace: Loaded notebook '" + nb->name + "' [" + nb->guid + "] from: " + entry.path().string());
                        notebooks.push_back(nb);
                        foundAny = true;
                    } else {
                        LOG_ERROR_CODE(Notebook, FolioErrorCode::DocNotebookLoadFailed, 
                                       "Failed to load notebook hierarchy from: " + entry.path().string());
                    }
                }
            }
        };

        // 1. Scan the default library: FolioNote/Libraries/Default (or legacy Default.foliolib)
        scanLibraryFolder(defaultLib);

        // 2. Scan any other library folders inside FolioNote/Libraries/
        if (std::filesystem::exists(librariesDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(librariesDir, ec)) {
                if (std::filesystem::is_directory(entry.status())) {
                    if (entry.path().extension() == ".foliolib" || 
                        std::filesystem::exists(entry.path() / "library.meta", ec) ||
                        Folio::LibraryManager::IsLibraryFolder(entry.path().string())) {
                        if (entry.path() != defaultLib) {
                            scanLibraryFolder(entry.path());
                        }
                    }
                }
            }
        }

        // 3. Scan any libraries residing directly in appRoot
        if (std::filesystem::exists(appRoot, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(appRoot, ec)) {
                if (std::filesystem::is_directory(entry.status())) {
                    if (entry.path().extension() == ".foliolib" || 
                        std::filesystem::exists(entry.path() / "library.meta", ec) ||
                        Folio::LibraryManager::IsLibraryFolder(entry.path().string())) {
                        scanLibraryFolder(entry.path());
                    }
                }
            }
        }

        // 4. Migration: If any legacy .notebook was stored loose directly in appRoot, move it into the default library
        if (std::filesystem::exists(appRoot, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(appRoot, ec)) {
                if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == ".notebook") {
                    std::filesystem::path migratedPath = defaultLib / entry.path().filename();
                    if (!std::filesystem::exists(migratedPath, ec)) {
                        LOG_INFO(General, "Workspace: Migrating loose notebook into default library: " + entry.path().string() + " -> " + migratedPath.string());
                        std::filesystem::rename(entry.path(), migratedPath, ec);
                        if (!ec) {
                            if (auto nb = repository.LoadNotebookHierarchy(migratedPath.string())) {
                                LOG_INFO(Notebook, "Workspace: Loaded migrated notebook '" + nb->name + "' [" + nb->guid + "]");
                                notebooks.push_back(nb);
                                foundAny = true;
                            }
                        }
                    }
                }
            }
        }

        // 5. Auto-generate default DemoBook inside default library if no notebooks exist anywhere
        if (!foundAny) {
            std::string demoPath = (defaultLib / "DemoBook.notebook").string();
            LOG_INFO(Notebook, "Workspace: No existing notebooks found. Creating initial DemoBook at: " + demoPath);
            
            auto demoNb = std::make_shared<Notebook>("DemoBook", ImVec4(0.20f, 0.48f, 0.92f, 1.0f));
            demoNb->filePath = demoPath;

            if (repository.OpenNotebookPackage(demoPath)) {
                repository.SaveNotebookAsync(demoNb);
            }

            notebooks.push_back(demoNb);
        }

        activeNotebookIndex = 0;
        LOG_INFO(General, "Workspace: Discovery complete. Total notebooks loaded: " + std::to_string(notebooks.size()));
    }

    // -------------------------------------------------------------------------
    // Active Item Access & Lazy Loading
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the currently active Notebook, or nullptr if none.
     */
    [[nodiscard]] std::shared_ptr<Notebook> GetActiveNotebook() const {
        if (activeNotebookIndex < notebooks.size()) {
            return notebooks[activeNotebookIndex];
        }
        return nullptr;
    }

    /**
     * @brief Resolves the active CanvasPage and triggers lazy loading if not yet loaded into RAM.
     * @return Shared pointer to loaded CanvasPage, or nullptr.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> GetActivePage() const {
        auto nb = GetActiveNotebook();
        if (!nb) return nullptr;
        auto page = nb->GetActivePage();
        if (page && !page->isLoaded) {
            LOG_INFO(CanvasPage, "Workspace: Lazy-loading evicted page '" + page->title + "' [" + page->guid + "] into RAM");
            repository.LoadPage(page);
        }
        return page;
    }

    // -------------------------------------------------------------------------
    // Persistence & Memory Management
    // -------------------------------------------------------------------------

    /**
     * @brief Asynchronously writes the active notebook's dirty pages (.ink files) and hierarchy metadata (structure.db) to disk.
     */
    void FlushActiveNotebookAsync() {
        auto nb = GetActiveNotebook();
        if (nb) {
            repository.SaveNotebookAsync(nb);
        } else {
            LOG_WARN(Notebook, "Workspace::FlushActiveNotebookAsync skipped: No active notebook present.");
        }
    }

    /**
     * @brief Evaluates all inactive pages in the working set and evicts those exceeding timeoutMs or capacity cap.
     *
     * DUAL-AXIS LRU EVICTION ALGORITHM & WORKING PROCESS:
     * 1. Active Page Immunity: The currently viewed page is pinned in RAM and never evicted.
     * 2. Axis 1 (Inactivity Timeout): Inactive resident pages unaccessed for longer than `timeoutMs`
     *    qualify for eviction.
     * 3. Axis 2 (Capacity Cap): When total loaded pages exceed `maxLoadedPages`, the oldest unaccessed
     *    pages are evicted first.
     * 4. Zero Data Loss: Any dirty page is persisted to its .ink file before its RAM objects are cleared.
     *
     * Mathematical Bound:
     * Total resident page RAM is strictly bounded by O(K * avgPageSize) where K = maxLoadedPages,
     * guaranteeing predictable memory footprint regardless of library or notebook size.
     *
     * @param timeoutMs Milliseconds of inactivity before in-memory strokes are unloaded (default: 60s).
     * @param maxLoadedPages Maximum resident pages allowed in RAM (default: 10).
     */
    void MaintainWorkingSetLRU(uint64_t timeoutMs = 60000, uint32_t maxLoadedPages = 10) {
        auto nb = GetActiveNotebook();
        if (nb) {
            repository.MaintainLRUCache(nb, timeoutMs, maxLoadedPages);
        }
    }

    // -------------------------------------------------------------------------
    // Notebook Helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Finds an open notebook by its GUID.
     */
    [[nodiscard]] std::shared_ptr<Notebook> FindNotebookByGuid(const std::string& guid) const {
        for (const auto& nb : notebooks) {
            if (nb && nb->guid == guid) {
                return nb;
            }
        }
        LOG_WARN_CODE(Notebook, FolioErrorCode::DocNotebookNotFound, 
                      "Workspace::FindNotebookByGuid: No open notebook matching GUID: " + guid);
        return nullptr;
    }

    /**
     * @brief Closes a notebook by GUID and adjusts activeNotebookIndex.
     * @return True if notebook was found and closed.
     */
    bool CloseNotebook(const std::string& guid) {
        auto it = std::find_if(notebooks.begin(), notebooks.end(), [&](const std::shared_ptr<Notebook>& nb) {
            return nb && nb->guid == guid;
        });

        if (it != notebooks.end()) {
            std::string nbName = (*it)->name;
            size_t index = std::distance(notebooks.begin(), it);
            notebooks.erase(it);

            if (notebooks.empty()) {
                activeNotebookIndex = 0;
            } else if (activeNotebookIndex >= notebooks.size() || activeNotebookIndex == index) {
                activeNotebookIndex = (notebooks.size() > 0) ? std::min(index, notebooks.size() - 1) : 0;
            }
            LOG_INFO(Notebook, "Workspace: Closed notebook '" + nbName + "' [" + guid + "]. Remaining open: " + std::to_string(notebooks.size()));
            return true;
        }
        LOG_WARN_CODE(Notebook, FolioErrorCode::DocNotebookNotFound, 
                      "Workspace::CloseNotebook failed: Notebook GUID not found: " + guid);
        return false;
    }

    /**
     * @brief Opens a notebook directory into the workspace's repository and activates it.
     *
     * WORKING PROCESS & LIFECYCLE:
     * 1. Validates the notebook pointer and non-empty directory path.
     * 2. Opens the notebook storage via repository.OpenNotebookPackage(filePath).
     * 3. Dispatches initial metadata persistence via repository.SaveNotebookAsync(notebook).
     * 4. Adds notebook to workspace's active notebooks vector and sets activeNotebookIndex.
     *
     * @param notebook Shared pointer to Notebook model.
     * @return true if successfully opened and activated; false otherwise.
     */
    bool OpenAndActivateNotebook(const std::shared_ptr<Notebook>& notebook) {
        if (!notebook || notebook->filePath.empty()) {
            LOG_ERROR_CODE(Notebook, FolioErrorCode::DocNotebookLoadFailed,
                           "Workspace::OpenAndActivateNotebook rejected: Null notebook pointer or empty file path.");
            return false;
        }
        if (repository.OpenNotebookPackage(notebook->filePath)) {
            repository.SaveNotebookAsync(notebook);
            notebooks.push_back(notebook);
            activeNotebookIndex = notebooks.size() - 1;
            LOG_INFO(Notebook, "Workspace: Opened and activated notebook '" + notebook->name + "' [" + notebook->guid + "] at: " + notebook->filePath);
            return true;
        }
        LOG_ERROR_CODE(Notebook, FolioErrorCode::DocNotebookLoadFailed,
                       "Workspace::OpenAndActivateNotebook: Failed to open notebook at: " + notebook->filePath);
        return false;
    }

    /**
     * @brief Returns the number of open notebooks in this workspace.
     */
    [[nodiscard]] size_t GetNotebookCount() const noexcept {
        return notebooks.size();
    }
};