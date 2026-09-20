/**
 * =========================================================================================
 * @file session_lifecycle.cpp
 * @brief Implementation of DocumentSession lifecycle, hierarchy, and notebook facade.
 * =========================================================================================
 *
 * ARCHITECTURAL PROCESS:
 * - Manages top-level workspace initialization, notebook discovery, and document activation.
 * - Handles last-session resumption restoring user state across application cold launches.
 * - Coordinates dirty-buffer flushing prior to notebook switching to prevent data loss.
 */

#include "core/document/document_session.hpp"
#include "core/backup/backup_manager.hpp"
#include "utils/logger.hpp"
#include <filesystem>

// -----------------------------------------------------------------------------
// Session Initialization & Restoration
// -----------------------------------------------------------------------------

/**
 * @brief Initializes the document session by scanning or creating the workspace directory.
 * @param workspaceDirectory Path to directory containing notebook packages (e.g. "FolioNote").
 */
void DocumentSession::Init(const std::string& workspaceDirectory) {
    LOG_INFO(DocumentSession, "Initializing DocumentSession at workspace root: " + workspaceDirectory);
    workspace.LoadWorkspace(workspaceDirectory);

    // Non-blocking background evaluation of scheduled multi-ring backups (Daily, Weekly, Monthly)
    Folio::BackupManager::CheckAndRunScheduledBackupsAsync(workspaceDirectory);
}

/**
 * @brief Restores the active document position from a previously persisted session.
 *
 * MATHEMATICAL & ARCHITECTURAL PROCESS:
 * - Restores active notebook, section, and page GUIDs saved in SettingsManager.
 * - Camera viewports are intentionally left at (0, 0, 1.0) so the application opens with
 *   a clean, predictable view while maintaining document position continuity.
 *
 * @param notebookGuid Persistent GUID of notebook last opened.
 * @param sectionGuid Persistent GUID of section last opened.
 * @param pageGuid Persistent GUID of page last viewed.
 * @return true if session was successfully restored to the target document.
 */
bool DocumentSession::RestoreLastSession(const std::string& notebookGuid,
                                        const std::string& sectionGuid,
                                        const std::string& pageGuid) {
    LOG_INFO(DocumentSession, "Restoring last session: Notebook=" + notebookGuid +
             ", Section=" + sectionGuid + ", Page=" + pageGuid);

    if (!notebookGuid.empty()) {
        OpenNotebook(notebookGuid);
    }
    if (!sectionGuid.empty()) {
        NavigateToSection(sectionGuid);
    }
    if (!pageGuid.empty()) {
        NavigateToPage(pageGuid);
    }
    return GetActivePage() != nullptr;
}

// -----------------------------------------------------------------------------
// Active Hierarchy Resolution
// -----------------------------------------------------------------------------

/**
 * @brief Resolves the currently active CanvasPage from the workspace hierarchy.
 * Triggers on-demand lazy loading from SQLite if the page is not in RAM.
 * @return Shared pointer to active CanvasPage, or nullptr if no page active.
 */
std::shared_ptr<CanvasPage> DocumentSession::GetActivePage() const {
    return workspace.GetActivePage();
}

/**
 * @brief Resolves the currently active Section from the active notebook.
 * @return Shared pointer to active Section, or nullptr if none.
 */
std::shared_ptr<Section> DocumentSession::GetActiveSection() const {
    return workspace.GetActiveSection();
}

/**
 * @brief Resolves the currently active Notebook package in this workspace.
 * @return Shared pointer to active Notebook, or nullptr if none.
 */
std::shared_ptr<Notebook> DocumentSession::GetActiveNotebook() const {
    return workspace.GetActiveNotebook();
}

/**
 * @brief Returns a copy of the list of all currently loaded notebooks in this workspace.
 * @return Vector of shared pointers to resident notebooks.
 */
std::vector<std::shared_ptr<Notebook>> DocumentSession::GetNotebooks() const {
    return workspace.notebooks;
}

// -----------------------------------------------------------------------------
// Notebook Lifecycle Facade
// -----------------------------------------------------------------------------

/**
 * @brief Opens or activates a notebook by its persistent GUID or package directory path.
 *
 * ARCHITECTURAL PROCESS:
 * 1. Flushes dirty state of outgoing active notebook asynchronously to prevent data loss.
 * 2. Locates the notebook in workspace.notebooks (or loads it on demand if a disk path was provided).
 * 3. Updates workspace.activeNotebookIndex.
 * 4. Broadcasts NotifyActiveNotebookChanged, NotifyActiveSectionChanged, and NotifyActivePageChanged.
 *
 * @param guidOrPath Persistent UUID v4 of notebook or filesystem path to .notebook directory.
 * @return true if notebook was found and activated.
 */
bool DocumentSession::OpenNotebook(const std::string& guidOrPath) {
    if (guidOrPath.empty()) return false;
    auto oldNb = GetActiveNotebook();
    if (oldNb && (oldNb->guid == guidOrPath || oldNb->filePath == guidOrPath)) {
        return true; // Already active
    }

    // Save unsaved changes before switching notebooks
    if (oldNb) {
        workspace.FlushActiveNotebookAsync();
    }

    for (size_t i = 0; i < workspace.notebooks.size(); ++i) {
        const auto& nb = workspace.notebooks[i];
        if (nb && (nb->guid == guidOrPath || nb->filePath == guidOrPath)) {
            workspace.activeNotebookIndex = i;
            auto newNb = nb;
            NotifyActiveNotebookChanged(newNb, oldNb);
            auto newSec = newNb->GetActiveSection();
            NotifyActiveSectionChanged(newSec, nullptr);
            auto newPage = newNb->GetActivePage();
            NotifyActivePageChanged(newPage, nullptr);
            NotifyHistoryChanged();
            LOG_INFO(DocumentSession, "OpenNotebook: Activated notebook '" + newNb->name + "' [" + newNb->guid + "]");
            return true;
        }
    }

    // If not in resident list, check if it's a valid filesystem path to a notebook
    std::error_code ec;
    if (std::filesystem::exists(guidOrPath, ec) && std::filesystem::is_directory(guidOrPath, ec)) {
        if (auto loaded = workspace.repository.LoadNotebookHierarchy(guidOrPath)) {
            workspace.notebooks.push_back(loaded);
            workspace.activeNotebookIndex = workspace.notebooks.size() - 1;
            NotifyActiveNotebookChanged(loaded, oldNb);
            NotifyActiveSectionChanged(loaded->GetActiveSection(), nullptr);
            NotifyActivePageChanged(loaded->GetActivePage(), nullptr);
            NotifyHistoryChanged();
            LOG_INFO(DocumentSession, "OpenNotebook: Loaded and activated notebook from path: " + guidOrPath);
            return true;
        }
    }

    LOG_WARN(DocumentSession, "OpenNotebook failed: Could not find or load notebook for: " + guidOrPath);
    return false;
}

/**
 * @brief Creates a new notebook package inside a target library and activates it.
 *
 * ARCHITECTURAL PROCESS:
 * 1. Formulates target directory path with collision resolution.
 * 2. Flushes any currently active notebook.
 * 3. Instantiates Notebook with initial default "General" section and "New Untitled Page".
 * 4. Schedules initial persistence and registers inside workspace.notebooks.
 * 5. Notifies observers.
 *
 * @param name Display title of the new notebook.
 * @param libraryPath Target library directory path (defaults to Default library).
 * @return Shared pointer to the newly created Notebook, or nullptr on failure.
 */
std::shared_ptr<Notebook> DocumentSession::CreateNotebook(const std::string& name, const std::string& libraryPath) {
    std::string libDir = libraryPath;
    if (libDir.empty()) {
        std::filesystem::path appRoot(workspace.workspaceDirectory);
        libDir = (appRoot / "Libraries" / "Default").string();
    }

    std::string safeName = name.empty() ? "Untitled Notebook" : name;
    std::filesystem::path targetDir = std::filesystem::path(libDir) / (safeName + ".notebook");

    // Disambiguate directory name if collision
    std::error_code ec;
    int counter = 1;
    while (std::filesystem::exists(targetDir, ec)) {
        targetDir = std::filesystem::path(libDir) / (safeName + " (" + std::to_string(counter++) + ").notebook");
    }

    auto oldNb = GetActiveNotebook();
    if (oldNb) {
        workspace.FlushActiveNotebookAsync();
    }

    auto newNb = std::make_shared<Notebook>(safeName);
    newNb->filePath = targetDir.string();

    // Create default initial section and page
    auto defaultSec = std::make_shared<Section>("General");
    defaultSec->notebookGuid = newNb->guid;
    auto defaultPage = std::make_shared<CanvasPage>("New Untitled Page");
    defaultSec->AddPage(defaultPage);
    newNb->AddSection(defaultSec);

    // Persist structure to disk
    workspace.repository.SaveNotebookAsync(newNb);

    workspace.notebooks.push_back(newNb);
    workspace.activeNotebookIndex = workspace.notebooks.size() - 1;

    NotifyActiveNotebookChanged(newNb, oldNb);
    NotifyActiveSectionChanged(defaultSec, nullptr);
    NotifyActivePageChanged(defaultPage, nullptr);
    NotifyHistoryChanged();
    LOG_INFO(DocumentSession, "CreateNotebook: Created and activated new notebook '" + safeName + "' at: " + targetDir.string());
    return newNb;
}

/**
 * @brief Closes the currently active notebook, persisting changes and switching to another notebook if available.
 * @return true if closed successfully.
 */
bool DocumentSession::CloseActiveNotebook() {
    auto activeNb = GetActiveNotebook();
    if (!activeNb) return false;

    workspace.FlushActiveNotebookAsync();
    std::string closedGuid = activeNb->guid;

    // Remove from resident notebooks if more than 1 notebook is open
    if (workspace.notebooks.size() > 1) {
        workspace.notebooks.erase(workspace.notebooks.begin() + workspace.activeNotebookIndex);
        if (workspace.activeNotebookIndex >= workspace.notebooks.size()) {
            workspace.activeNotebookIndex = workspace.notebooks.size() - 1;
        }
        auto nextNb = GetActiveNotebook();
        NotifyActiveNotebookChanged(nextNb, activeNb);
        if (nextNb) {
            NotifyActiveSectionChanged(nextNb->GetActiveSection(), nullptr);
            NotifyActivePageChanged(nextNb->GetActivePage(), nullptr);
        }
    }
    LOG_INFO(DocumentSession, "CloseActiveNotebook: Closed notebook [" + closedGuid + "]");
    return true;
}
