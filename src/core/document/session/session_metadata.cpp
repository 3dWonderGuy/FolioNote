/**
 * =========================================================================================
 * @file session_metadata.cpp
 * @brief Implementation of DocumentSession page metadata, styling facade, autosave persistence,
 *        and observer notification dispatchers.
 * =========================================================================================
 *
 * ARCHITECTURAL PROCESS:
 * - Manages active page styling (paper grids, rules, page borders, canvas infinity modes).
 * - Coordinates asynchronous SQLite page storage flushing and dirty-state queries.
 * - Broadcasts re-entrancy guarded notifications to registered UI and subsystem observers.
 */

#include "core/document/document_session.hpp"
#include "app/settings_manager.hpp"
#include <algorithm>

// -----------------------------------------------------------------------------
// Page Metadata & Styling Facade
// -----------------------------------------------------------------------------

/**
 * @brief Returns a structured metadata snapshot of the currently active CanvasPage.
 * @return PageMetadataDTO struct populated from active page properties.
 */
DocumentSession::PageMetadataDTO DocumentSession::GetActivePageMetadata() const {
    PageMetadataDTO dto;
    auto activePage = GetActivePage();
    if (!activePage) return dto;

    dto.guid = activePage->guid;
    dto.title = activePage->title;
    dto.createdDateStr = activePage->createdDateStr;
    dto.createdTimeStr = activePage->createdTimeStr;
    dto.nestingLevel = activePage->nestingLevel;
    dto.sortOrder = activePage->sortOrder;
    dto.paperStyle = activePage->paperStyle;
    dto.gridSpacingMm = activePage->gridSpacingMm;
    dto.pageSizeFormat = activePage->pageSizeFormat;
    dto.pageIsLandscape = activePage->pageIsLandscape;
    dto.pageWidthMm = activePage->pageWidthMm;
    dto.pageHeightMm = activePage->pageHeightMm;
    dto.showPageBorder = activePage->showPageBorder;
    dto.pageBorderType = activePage->pageBorderType;
    dto.pageBorderStyle = activePage->pageBorderStyle;
    dto.pageBorderWidth = activePage->pageBorderWidth;
    dto.infinityMode = activePage->infinityMode;
    dto.objectCount = activePage->objects.size();
    dto.isModified = activePage->isModified;
    return dto;
}

/**
 * @brief Renames the active CanvasPage and flags dirty for SQLite persistence.
 * @param newTitle New human-readable display title.
 */
void DocumentSession::SetPageTitle(std::string newTitle) {
    auto activePage = GetActivePage();
    if (!activePage) return;
    activePage->title = std::move(newTitle);
    activePage->Touch();
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

/**
 * @brief Updates the paper rule style (Grid, Ruled, Blank, Dotted, Cornell, etc.) for the active page.
 */
void DocumentSession::SetPaperStyle(PaperStyle style) {
    auto activePage = GetActivePage();
    if (!activePage) return;
    activePage->paperStyle = style;
    activePage->Touch();
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

/**
 * @brief Updates physical rule or grid spacing in millimeters for the active page.
 * @param spacingMm Spacing in millimeters (standard: 5.0 mm).
 */
void DocumentSession::SetGridSpacingMm(double spacingMm) {
    auto activePage = GetActivePage();
    if (!activePage) return;
    activePage->gridSpacingMm = std::max(1.0, spacingMm);
    activePage->Touch();
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

/**
 * @brief Configures page border demarcation settings on the active page.
 */
void DocumentSession::SetPageBorderSettings(bool show, PageBorderType type, PageBorderStyle style, double width) {
    auto activePage = GetActivePage();
    if (!activePage) return;
    activePage->showPageBorder = show;
    activePage->pageBorderType = type;
    activePage->pageBorderStyle = style;
    activePage->pageBorderWidth = width;
    activePage->Touch();
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

/**
 * @brief Configures physical sheet dimensions and orientation for the active page.
 */
void DocumentSession::SetPageSizeFormat(PageSizeFormat format, bool isLandscape, double customW, double customH) {
    auto activePage = GetActivePage();
    if (!activePage) return;
    activePage->pageSizeFormat = format;
    activePage->pageIsLandscape = isLandscape;
    activePage->pageWidthMm = customW;
    activePage->pageHeightMm = customH;
    activePage->Touch();
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

/**
 * @brief Configures canvas infinity boundary mode (SemiInfinity, FullInfinity, etc.) for the active page.
 */
void DocumentSession::SetCanvasInfinityMode(CanvasInfinityMode mode) {
    auto activePage = GetActivePage();
    if (!activePage) return;
    activePage->infinityMode = mode;
    activePage->Touch();
    activePage->isModified = true;
    NotifyPageModified(activePage);
}

// -----------------------------------------------------------------------------
// Persistence Facade & Autosave
// -----------------------------------------------------------------------------

/**
 * @brief Checks whether the active page or any resident pages have unsaved modifications.
 * @return true if unsaved changes exist in RAM, false otherwise.
 */
bool DocumentSession::HasUnsavedChanges() const {
    auto activePage = GetActivePage();
    if (activePage && activePage->isModified) return true;
    auto nb = workspace.GetActiveNotebook();
    if (!nb) return false;
    for (const auto& s : nb->sections) {
        if (!s) continue;
        for (const auto& p : s->pages) {
            if (p && p->isLoaded && p->isModified) return true;
        }
    }
    for (const auto& g : nb->sectionGroups) {
        if (!g) continue;
        for (const auto& s : g->sections) {
            if (!s) continue;
            for (const auto& p : s->pages) {
                if (p && p->isLoaded && p->isModified) return true;
            }
        }
    }
    return false;
}

/**
 * @brief Flushes the active CanvasPage asynchronously to disk via atomic .ink staging.
 */
std::future<bool> DocumentSession::SaveActivePageAsync() {
    auto activePage = GetActivePage();
    if (!activePage) {
        std::promise<bool> p;
        p.set_value(false);
        return p.get_future();
    }
    auto nb = workspace.GetActiveNotebook();
    std::string secGuid;
    int32_t sortOrder = activePage->sortOrder;
    if (nb) {
        auto sec = nb->GetActiveSection();
        if (sec) secGuid = sec->guid;
    }
    return workspace.repository.SavePageAsync(activePage, secGuid, sortOrder);
}

/**
 * @brief Flushes all modified pages across the active notebook to disk asynchronously.
 */
void DocumentSession::SaveAllModifiedPages() {
    workspace.FlushActiveNotebookAsync();
}

// -----------------------------------------------------------------------------
// Observer / Event Notification Subsystem
// -----------------------------------------------------------------------------

/**
 * @brief Registers an observer to receive document session mutation events.
 * @param observer Pointer to concrete observer implementation.
 */
void DocumentSession::AddObserver(Folio::IDocumentSessionObserver* observer) {
    if (!observer) return;
    if (std::find(observers.begin(), observers.end(), observer) == observers.end()) {
        observers.push_back(observer);
    }
}

/**
 * @brief Unregisters an observer from receiving document session events.
 * @param observer Pointer to previously registered observer.
 */
void DocumentSession::RemoveObserver(Folio::IDocumentSessionObserver* observer) {
    auto it = std::find(observers.begin(), observers.end(), observer);
    if (it != observers.end()) {
        observers.erase(it);
    }
}

/**
 * @brief Broadcasts an active page change event to all registered observers.
 * Uses defensive snapshotting to guard against re-entrancy and iterator invalidation.
 */
void DocumentSession::NotifyActivePageChanged(const std::shared_ptr<CanvasPage>& newPage,
                                             const std::shared_ptr<CanvasPage>& oldPage) {
    auto snapshot = observers;
    for (auto* obs : snapshot) {
        if (obs) obs->OnActivePageChanged(newPage, oldPage);
    }
}

/**
 * @brief Broadcasts an active section change event to all registered observers.
 */
void DocumentSession::NotifyActiveSectionChanged(const std::shared_ptr<Section>& newSec,
                                                const std::shared_ptr<Section>& oldSec) {
    auto snapshot = observers;
    for (auto* obs : snapshot) {
        if (obs) obs->OnActiveSectionChanged(newSec, oldSec);
    }
}

/**
 * @brief Broadcasts an active notebook change event to all registered observers.
 */
void DocumentSession::NotifyActiveNotebookChanged(const std::shared_ptr<Notebook>& newNb,
                                                 const std::shared_ptr<Notebook>& oldNb) {
    auto snapshot = observers;
    for (auto* obs : snapshot) {
        if (obs) obs->OnActiveNotebookChanged(newNb, oldNb);
    }
}

/**
 * @brief Broadcasts an undo/redo stack availability transition event.
 */
void DocumentSession::NotifyHistoryChanged() {
    bool undoAvail = CanUndo();
    bool redoAvail = CanRedo();
    auto snapshot = observers;
    for (auto* obs : snapshot) {
        if (obs) obs->OnHistoryChanged(undoAvail, redoAvail);
    }
}

/**
 * @brief Broadcasts a page dirty/modified state event.
 *
 * AUTOMATIC SESSION VERSIONING:
 * Tracks user page interactions in the current session. On the first modification to a page
 * during this application run, captures an initial session checkpoint and enforces the
 * configured retention cap (`maxPageVersionsToKeep`, where 0 = unlimited).
 */
void DocumentSession::NotifyPageModified(const std::shared_ptr<CanvasPage>& page) {
    if (page && !page->guid.empty()) {
        if (sessionInteractedPages.find(page->guid) == sessionInteractedPages.end()) {
            sessionInteractedPages.insert(page->guid);
            auto activeNb = GetActiveNotebook();
            if (activeNb && !activeNb->filePath.empty()) {
                Folio::PageVersionManager::CreateRevision(*page, activeNb->filePath, "Session Autosave", false);
                int cap = SettingsManager::Instance().maxPageVersionsToKeep;
                Folio::PageVersionManager::PruneRevisions(activeNb->filePath, page->guid, static_cast<size_t>(cap));
            }
        }
    }

    auto snapshot = observers;
    for (auto* obs : snapshot) {
        if (obs) obs->OnPageModified(page);
    }
}

/**
 * @brief Broadcasts a new page creation event.
 */
void DocumentSession::NotifyPageCreated(const std::shared_ptr<CanvasPage>& page) {
    auto snapshot = observers;
    for (auto* obs : snapshot) {
        if (obs) obs->OnPageCreated(page);
    }
}

/**
 * @brief Broadcasts a page deletion/soft-deletion event.
 */
void DocumentSession::NotifyPageDeleted(const std::string& pageGuid) {
    auto snapshot = observers;
    for (auto* obs : snapshot) {
        if (obs) obs->OnPageDeleted(pageGuid);
    }
}

// -----------------------------------------------------------------------------
// 6. Page Revision History & Rollback (Google Docs-Style)
// -----------------------------------------------------------------------------

/**
 * @brief Captures a point-in-time revision snapshot of the active page.
 *
 * WORKING PROCESS:
 * 1. Resolves active page and containing notebook bundle path.
 * 2. Delegates serialization to PageVersionManager.
 * 3. Enforces unnamed revision retention cap from SettingsManager (0 = unlimited).
 *
 * @param versionName Optional descriptive milestone label (e.g. "Draft 1", "Before Midterm").
 * @param isNamed If true, marks version as permanent milestone immune to auto-pruning.
 * @return true on success; false if active page is invalid.
 */
bool DocumentSession::CreateActivePageRevision(const std::string& versionName, bool isNamed) {
    auto activePage = GetActivePage();
    auto activeNb = GetActiveNotebook();
    if (!activePage || !activeNb || activeNb->filePath.empty()) return false;

    Folio::PageRevisionInfo info;
    bool success = Folio::PageVersionManager::CreateRevision(
        *activePage, activeNb->filePath, versionName, isNamed, &info
    );

    if (success && !isNamed) {
        int cap = SettingsManager::Instance().maxPageVersionsToKeep;
        Folio::PageVersionManager::PruneRevisions(activeNb->filePath, activePage->guid, static_cast<size_t>(cap));
    }
    return success;
}

/**
 * @brief Discovers and lists all historical revisions for the active page, sorted newest first.
 */
std::vector<Folio::PageRevisionInfo> DocumentSession::GetActivePageRevisions() const {
    auto activePage = GetActivePage();
    auto activeNb = GetActiveNotebook();
    if (!activePage || !activeNb || activeNb->filePath.empty()) return {};

    return Folio::PageVersionManager::ListRevisions(activeNb->filePath, activePage->guid);
}

/**
 * @brief Restores the active page to a specific historical revision.
 *
 * NON-DESTRUCTIVE ROLLBACK:
 * If `createSafetySnapshot` is true, automatically captures a safety snapshot of current
 * page state prior to overwriting it with the chosen historical revision.
 */
bool DocumentSession::RestoreActivePageRevision(const std::string& versionId, bool createSafetySnapshot) {
    auto activePage = GetActivePage();
    auto activeNb = GetActiveNotebook();
    if (!activePage || !activeNb || activeNb->filePath.empty()) return false;

    bool success = Folio::PageVersionManager::RestoreRevision(
        *activePage, activeNb->filePath, versionId, createSafetySnapshot
    );

    if (success) {
        NotifyPageModified(activePage);
        NotifyHistoryChanged();
    }
    return success;
}

/**
 * @brief Renames a historical revision and pins it as a named milestone.
 */
bool DocumentSession::NameActivePageRevision(const std::string& versionId, const std::string& newName) {
    auto activePage = GetActivePage();
    auto activeNb = GetActiveNotebook();
    if (!activePage || !activeNb || activeNb->filePath.empty()) return false;

    return Folio::PageVersionManager::RenameRevision(
        activeNb->filePath, activePage->guid, versionId, newName
    );
}
