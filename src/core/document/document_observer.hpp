#pragma once
/**
 * =========================================================================================
 * @file document_observer.hpp
 * @brief Event notification listener interface for DocumentSession state changes
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & OBSERVER PATTERN:
 * Follows the classic Gang of Four (GoF) Observer Pattern.
 * Decouples DocumentSession mutations (page navigation, creation, deletion, dirty flag,
 * undo/redo changes) from presentation and rendering components (RibbonBar, Sidebar,
 * CanvasEngine, StatusHUD).
 *
 * All virtual methods provide empty default implementations so concrete listeners only
 * implement the events they care about.
 */

#include <memory>
#include <string>

class CanvasPage;
class Section;
class Notebook;

namespace Folio {

/**
 * @class IDocumentSessionObserver
 * @brief Observer interface receiving broadcast events from DocumentSession.
 */
class IDocumentSessionObserver {
public:
    virtual ~IDocumentSessionObserver() = default;

    /**
     * @brief Emitted when the user or session switches the active CanvasPage.
     * @param newPage Newly activated page.
     * @param oldPage Previously active page (may be null during startup).
     */
    virtual void OnActivePageChanged(const std::shared_ptr<CanvasPage>& newPage,
                                     const std::shared_ptr<CanvasPage>& oldPage) {}

    /**
     * @brief Emitted when the active Section tab changes.
     * @param newSection Newly activated section.
     * @param oldSection Previously active section.
     */
    virtual void OnActiveSectionChanged(const std::shared_ptr<Section>& newSection,
                                        const std::shared_ptr<Section>& oldSection) {}

    /**
     * @brief Emitted when the active Notebook package changes.
     * @param newNotebook Newly activated notebook.
     * @param oldNotebook Previously active notebook.
     */
    virtual void OnActiveNotebookChanged(const std::shared_ptr<Notebook>& newNotebook,
                                         const std::shared_ptr<Notebook>& oldNotebook) {}

    /**
     * @brief Emitted whenever the undo or redo command stack availability changes.
     * @param canUndo True if at least one action is available to undo.
     * @param canRedo True if at least one action is available to redo.
     */
    virtual void OnHistoryChanged(bool canUndo, bool canRedo) {}

    /**
     * @brief Emitted when a canvas page is marked modified (dirty) or flushed to disk.
     * @param page Shared pointer to the modified page.
     */
    virtual void OnPageModified(const std::shared_ptr<CanvasPage>& page) {}

    /**
     * @brief Emitted when a new CanvasPage is added to a section.
     * @param page Shared pointer to the newly created page.
     */
    virtual void OnPageCreated(const std::shared_ptr<CanvasPage>& page) {}

    /**
     * @brief Emitted when a CanvasPage is removed or moved to the recycle bin.
     * @param pageGuid Persistent GUID of the deleted page.
     */
    virtual void OnPageDeleted(const std::string& pageGuid) {}
};

} // namespace Folio
