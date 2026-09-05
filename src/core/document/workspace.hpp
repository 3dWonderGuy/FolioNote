#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <filesystem>
#include "core/document/notebook.hpp"
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
 * - It owns the primary `Folio::PageRepository` instance, acting as the gateway to the
 *   underlying SQLite databases inside each `.notebook` package folder.
 *
 * CORE LIFECYCLE RESPONSIBILITIES:
 * 1. Workspace Discovery & Initialization (`LoadWorkspace`):
 *    - Scans the designated root directory (e.g. `FolioNote/`) for `.notebook` package directories.
 *    - Automatically deserializes notebook hierarchies, sections, and page metadata via `PageRepository`.
 *    - If no notebooks exist on disk, it auto-generates a `DemoBook.notebook` package with default schema.
 * 2. On-Demand Lazy Loading:
 *    - In `GetActivePage()`, if the requested page is marked `!isLoaded` (its vector strokes have not yet
 *      been read from SQLite or were evicted to save memory), the Workspace loads it from disk via
 *      `repository.LoadPage(page)`.
 * 3. Asynchronous Persistence (`FlushActiveNotebookAsync`):
 *    - Dispatches save tasks to background worker threads via `PageRepository::SaveNotebookAsync`,
 *      preventing UI hitching while serializing strokes and metadata to SQLite.
 * 4. LRU Working-Set Cache Eviction (`MaintainWorkingSetLRU`):
 *    - Periodically called to evict in-memory strokes of inactive pages that haven't been accessed
 *      for a configurable timeout (e.g. 60 seconds), keeping RAM usage constant even with massive libraries.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Session State Persistence: Remember which notebook, section, and page was open on last exit.
 * - Multi-Notebook Tabs: Switch between multiple notebooks in separate UI tabs or split-views.
 * - Cloud / Sync Bridge: Check for file updates or cloud sync notifications on active packages.
 * - Background Auto-Save Timer: Automatically trigger `FlushActiveNotebookAsync` on a periodic schedule.
 */
class Workspace {
public:
    // -------------------------------------------------------------------------
    // Notebooks & Storage Repository
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<Notebook>> notebooks;   ///< All currently open notebooks in this workspace
    size_t activeNotebookIndex = 0;                      ///< Index of the currently active/viewed notebook
    mutable Folio::PageRepository repository;            ///< SQLite storage repository and async queue

    // -------------------------------------------------------------------------
    // Workspace Loading & Initialization
    // -------------------------------------------------------------------------

    /**
     * @brief Scans a directory for existing .notebook folders or creates a default demo notebook.
     * @param directoryPath Target directory on disk containing notebook packages.
     */
    void LoadWorkspace(const std::string& directoryPath) {
        notebooks.clear();
        bool foundAny = false;

        std::error_code ec;
        if (std::filesystem::exists(directoryPath, ec) && std::filesystem::is_directory(directoryPath, ec)) {
            // 1. Scan the directory for existing .notebook folders
            for (const auto& entry : std::filesystem::directory_iterator(directoryPath, ec)) {
                if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == ".notebook") {
                    if (auto nb = repository.LoadNotebookHierarchy(entry.path().string())) {
                        notebooks.push_back(nb);
                        foundAny = true;
                    }
                }
            }
        }

        // 2. Auto-generate default notebook if none exist
        if (!foundAny) {
            std::filesystem::create_directories(directoryPath, ec);
            std::string demoPath = (std::filesystem::path(directoryPath) / "DemoBook.notebook").string();
            
            // Create in memory
            auto demoNb = std::make_shared<Notebook>("DemoBook", ImVec4(0.20f, 0.48f, 0.92f, 1.0f));
            demoNb->filePath = demoPath;

            // Immediately create physical package folder + SQLite file and write initial schema
            if (repository.OpenNotebookPackage(demoPath)) {
                repository.SaveNotebookAsync(demoNb); // Writes metadata + default section + blank page
            }

            notebooks.push_back(demoNb);
        }

        activeNotebookIndex = 0;
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
            repository.LoadPage(page);
        }
        return page;
    }

    // -------------------------------------------------------------------------
    // Persistence & Memory Management
    // -------------------------------------------------------------------------

    /**
     * @brief Asynchronously writes the active notebook's dirty pages and metadata to SQLite.
     */
    void FlushActiveNotebookAsync() {
        auto nb = GetActiveNotebook();
        if (nb) {
            repository.SaveNotebookAsync(nb);
        }
    }

    /**
     * @brief Evaluates all inactive pages in the working set and evicts those exceeding timeoutMs.
     * @param timeoutMs Milliseconds of inactivity before in-memory strokes are unloaded (default: 60s).
     */
    void MaintainWorkingSetLRU(uint64_t timeoutMs = 60000) {
        auto nb = GetActiveNotebook();
        if (nb) {
            repository.MaintainLRUCache(nb, timeoutMs);
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
            size_t index = std::distance(notebooks.begin(), it);
            notebooks.erase(it);

            if (notebooks.empty()) {
                activeNotebookIndex = 0;
            } else if (activeNotebookIndex >= notebooks.size() || activeNotebookIndex == index) {
                activeNotebookIndex = (notebooks.size() > 0) ? std::min(index, notebooks.size() - 1) : 0;
            }
            return true;
        }
        return false;
    }

    /**
     * @brief Returns the number of open notebooks in this workspace.
     */
    [[nodiscard]] size_t GetNotebookCount() const noexcept {
        return notebooks.size();
    }
};