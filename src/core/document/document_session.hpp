#pragma once
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <future>
#include <optional>
#include <unordered_set>
#include <unordered_map>

#include "core/document/workspace.hpp"
#include "core/document/document_observer.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container.hpp"
#include "core/objects/image_container.hpp"
#include "core/objects/text_box.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "core/engine/live_layer_pipeline.hpp"
#include "input/pen_palette.hpp"
#include "core/history/canvas_command.hpp"
#include "core/backup/page_version_manager.hpp"

// Forward declaration
class CanvasEngine;

/**
 * =========================================================================================
 * @file document_session.hpp
 * @brief Top-level document controller and input/render facade for the active session.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * - Coordinates runtime engine systems (InputManager, CanvasEngine, PenPalette, RibbonBar)
 *   with the underlying document model (Workspace).
 * - Delegates definitions to domain units under `src/core/document/session/`:
 *     - session_lifecycle.cpp  (Init, session restore, hierarchy, notebook switching)
 *     - session_navigation.cpp (Page transitions, deep lookup, section activation, viewport cache)
 *     - session_history.cpp    (Undo/Redo, GoF composite macros, continuous eraser aggregation)
 *     - session_canvas_ops.cpp (Stroke commits, laser pointer, clipboard, grouping, locking)
 *     - session_metadata.cpp   (Page metadata DTO, styling, autosave, observer notifications)
 */
class DocumentSession {
public:
    Workspace workspace; ///< Owns the notebook collection and PageRepository persistence

    /// Aggregates stroke deletions and slices produced during a continuous eraser drag.
    struct EraseTransaction {
        bool isActive = false;
        std::string targetPageGuid; ///< CanvasPage where the erase gesture started
        std::vector<std::shared_ptr<CanvasObject>> deletedObjects;
        std::vector<Folio::BatchEraseCommand::SlicedStrokeEntry> slicedStrokes;
        std::unordered_map<uint32_t, size_t> originalStrokeIndexMap;
        std::unordered_set<uint32_t> recordedDeletedUids;
    } eraseTx;

    /// Aggregates multiple sequential canvas mutations into a single atomic undo/redo unit (GoF Composite).
    struct MacroTransactionState {
        bool isActive = false;
        std::string description;
        std::shared_ptr<CanvasPage> boundPage;
        std::unique_ptr<Folio::MacroCommand> macro;
    } macroTx;

    /// Structured snapshot of active page properties, rules, borders, and geometry.
    struct PageMetadataDTO {
        std::string guid;
        std::string title;
        std::string createdDateStr;
        std::string createdTimeStr;
        int32_t nestingLevel = 0;
        int32_t sortOrder = 0;
        PaperStyle paperStyle = PaperStyle::Grid;
        double gridSpacingMm = 5.0;
        PageSizeFormat pageSizeFormat = PageSizeFormat::Letter;
        bool pageIsLandscape = false;
        double pageWidthMm = 215.9;
        double pageHeightMm = 279.4;
        bool showPageBorder = false;
        PageBorderType pageBorderType = PageBorderType::Automatic;
        PageBorderStyle pageBorderStyle = PageBorderStyle::Continuous;
        double pageBorderWidth = 1.5;
        CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity;
        size_t objectCount = 0;
        bool isModified = false;
    };

    /// Encapsulates a dynamic navigation target: target page, optional world coords, zoom, and object UID.
    struct CanvasDeepLink {
        std::string pageGuid;         ///< Target page UUID v4
        bool hasTargetCoords = false; ///< True if target world (X, Y) was specified
        double worldXMm = 0.0;        ///< Target world X in millimeters
        double worldYMm = 0.0;        ///< Target world Y in millimeters
        double zoom = 1.0;            ///< Target camera magnification factor
        bool hasTargetObject = false; ///< True if targeted by specific object UID
        uint32_t objectUid = 0;       ///< Target CanvasObject UID
    };

    // -------------------------------------------------------------------------
    // Observer Subsystem & Ephemeral Sink State
    // -------------------------------------------------------------------------
    std::vector<Folio::IDocumentSessionObserver*> observers;              ///< Registered event listeners
    std::vector<std::shared_ptr<CanvasObject>> clipboardObjects;          ///< Transient session clipboard
    std::function<void(BLPath, BLRgba32, uint32_t)> ephemeralStrokeSink; ///< Laser pointer presentation ink receiver
    std::unordered_set<std::string> sessionInteractedPages;               ///< Pages interacted with in this session (auto-versioning)

    // -------------------------------------------------------------------------
    // 1. Session Lifecycle & Restoration (session_lifecycle.cpp)
    // -------------------------------------------------------------------------

    /// @brief Initializes the document session by scanning or creating the workspace directory.
    void Init(const std::string& workspaceDirectory);

    /// @brief Restores active notebook, section, and page position from persistent settings.
    bool RestoreLastSession(const std::string& notebookGuid,
                            const std::string& sectionGuid,
                            const std::string& pageGuid);

    /// @brief Resolves the currently active CanvasPage (lazy-loads from SQLite if needed).
    [[nodiscard]] std::shared_ptr<CanvasPage> GetActivePage() const;

    /// @brief Resolves the currently active Section from the active notebook.
    [[nodiscard]] std::shared_ptr<Section> GetActiveSection() const;

    /// @brief Resolves the currently active Notebook package.
    [[nodiscard]] std::shared_ptr<Notebook> GetActiveNotebook() const;

    /// @brief Returns a copy of the list of all currently loaded notebooks.
    [[nodiscard]] std::vector<std::shared_ptr<Notebook>> GetNotebooks() const;

    /// @brief Returns the active page title, or an empty string if no page is active.
    [[nodiscard]] std::string GetActivePageTitle() const {
        auto p = GetActivePage();
        return p ? p->title : "";
    }

    /// @brief Opens or activates a notebook by persistent GUID or filesystem path.
    bool OpenNotebook(const std::string& guidOrPath);

    /// @brief Creates a new notebook package inside a target library and activates it.
    std::shared_ptr<Notebook> CreateNotebook(const std::string& name, const std::string& libraryPath = "");

    /// @brief Closes the currently active notebook, persisting pending modifications.
    bool CloseActiveNotebook();

    // -------------------------------------------------------------------------
    // 2. Page & Section Navigation Facade (session_navigation.cpp)
    // -------------------------------------------------------------------------

    /// @brief Advances to the next sequential CanvasPage within the active section.
    bool NextPage();

    /// @brief Steps backward to the previous sequential CanvasPage within the active section.
    bool PreviousPage();

    /// @brief Navigates directly to a CanvasPage by GUID (with cross-notebook search fallback).
    bool NavigateToPage(const std::string& pageGuid);

    /// @brief Navigates directly to a Section by its persistent GUID in the active notebook.
    bool NavigateToSection(const std::string& sectionGuid);

    /// @brief Factory method: Creates and appends a new CanvasPage to the active section.
    std::shared_ptr<CanvasPage> CreateNewPage(std::string title = "New Untitled",
                                              std::string parentGuid = "",
                                              int32_t level = 0);

    /// @brief Deletes or soft-deletes the currently active CanvasPage.
    bool DeleteActivePage(bool moveToTrash = true);

    /// @brief Creates a deep duplicate of the active CanvasPage with fresh UUIDs and objects.
    std::shared_ptr<CanvasPage> DuplicateActivePage();

    /// @brief Moves the active page forward (+1) or backward (-1) in display sequence.
    bool MoveActivePage(int delta);

    /// @brief Caches in-memory camera pan (mm) and zoom scale for a page throughout the session.
    void SavePageViewport(const std::string& pageGuid, double panX, double panY, double zoom);

    /// @brief Retrieves the cached in-memory camera viewport for a page if previously visited.
    bool GetPageViewport(const std::string& pageGuid, double& outPanX, double& outPanY, double& outZoom) const;

    /// @brief Queries all objects on the active page that intersect the camera viewport frustum.
    [[nodiscard]] std::vector<std::shared_ptr<CanvasObject>> QueryVisible(const Viewport& viewport) const;

    // -------------------------------------------------------------------------
    // Dynamic Canvas Deep Linking & Targeted Navigation
    // -------------------------------------------------------------------------

    /// @brief Parses a dynamic deep-link URI (e.g. "folionote://page/{guid}?x=10&y=20#obj=42").
    static std::optional<CanvasDeepLink> ParseDeepLinkUri(const std::string& uri);

    /// @brief Navigates to a specific canvas coordinate, centering the viewport on that point.
    bool NavigateToCanvasLocation(const std::string& pageGuid,
                                  double targetWorldXMm, double targetWorldYMm,
                                  double screenWidthPx, double screenHeightPx,
                                  double pixelsPerMm, double zoom = 1.0);

    /// @brief Navigates to a specific CanvasObject by UID, centering the viewport on its centroid.
    bool NavigateToObject(const std::string& pageGuid, uint32_t objectUid,
                          double screenWidthPx, double screenHeightPx,
                          double pixelsPerMm, bool selectObject = true,
                          double zoom = 1.0);

    /// @brief Navigates to a structured CanvasDeepLink target.
    bool NavigateToDeepLink(const CanvasDeepLink& link,
                            double screenWidthPx, double screenHeightPx,
                            double pixelsPerMm, bool selectObject = true);

    /// @brief Parses and navigates directly to any canvas deep-link URI string.
    bool NavigateToUri(const std::string& uri,
                       double screenWidthPx, double screenHeightPx,
                       double pixelsPerMm, bool selectObject = true);

    // -------------------------------------------------------------------------
    // 3. Command History, Macros & Eraser Gestures (session_history.cpp)
    // -------------------------------------------------------------------------

    /// @brief Reverses the most recent canvas command on the active page.
    bool Undo(CanvasEngine* engine = nullptr);

    /// @brief Re-applies the most recently reversed canvas command on the active page.
    bool Redo(CanvasEngine* engine = nullptr);

    /// @brief Returns true if there are commands available to undo on the active page.
    [[nodiscard]] bool CanUndo() const;

    /// @brief Returns true if there are commands available to redo on the active page.
    [[nodiscard]] bool CanRedo() const;

    /// @brief Begins a multi-step macro transaction (GoF Composite Command).
    bool BeginMacroTransaction(std::string description = "Compound Action");

    /// @brief Commits the active macro transaction as a single atomic history step.
    bool EndMacroTransaction();

    /// @brief Cancels an in-flight macro transaction, rolling back intermediate mutations.
    void CancelMacroTransaction();

    /// @brief Returns true if a compound macro transaction is currently recording.
    [[nodiscard]] bool IsMacroTransactionActive() const noexcept { return macroTx.isActive; }

    /// @brief Records a command to page history or appends to the active macro transaction.
    void RecordHistoryCommand(std::shared_ptr<CanvasPage> page, std::unique_ptr<Folio::ICanvasCommand> cmd);

    /// @brief Retrieves the CommandManager (CommandHistory) for the currently active page.
    Folio::CommandHistory* GetCommandManager();

    /// @brief Executes a command directly on the active page and pushes it onto the undo stack.
    void ExecuteCommand(std::unique_ptr<Folio::ICanvasCommand> cmd, CanvasEngine* engine = nullptr);

    /// @brief Records a command that has already executed live on the active page into the undo history.
    void RecordCommand(std::unique_ptr<Folio::ICanvasCommand> cmd);

    /// @brief Starts an atomic continuous eraser transaction bound to the active page.
    void BeginEraseTransaction();

    /// @brief Cancels an active eraser transaction without committing mutations to history.
    void CancelEraseTransaction();

    /// @brief Records an object deleted during the continuous eraser gesture.
    void RecordErasedObject(const std::shared_ptr<CanvasObject>& obj);

    /// @brief Records a stroke sliced into surviving fragments during a continuous eraser drag.
    void RecordSlicedStroke(const std::shared_ptr<InkContainer>& original,
                            const std::vector<std::shared_ptr<InkContainer>>& survivingFragments);

    /// @brief Finalizes the active continuous eraser transaction into a single BatchEraseCommand.
    void EndEraseTransaction(CanvasEngine* engine = nullptr);

    /// @brief Returns true if an eraser drag transaction is currently ongoing.
    [[nodiscard]] bool IsEraseTransactionActive() const noexcept { return eraseTx.isActive; }

    // -------------------------------------------------------------------------
    // 4. Canvas Operations: Inking, Selection, Clipboard, Grouping (session_canvas_ops.cpp)
    // -------------------------------------------------------------------------

    /// @brief Registers a delegate receiver for ephemeral presentation strokes.
    void SetEphemeralStrokeSink(std::function<void(BLPath, BLRgba32, uint32_t)> sink);

    /// @brief Checks if a presentation sink is registered to accept laser pointer strokes.
    [[nodiscard]] bool HasEphemeralStrokeSink() const noexcept { return static_cast<bool>(ephemeralStrokeSink); }

    /// @brief Commits an ephemeral presentation stroke (Laser Pointer) with quadratic alpha fade.
    void CommitEphemeralStroke(FinishedStrokeData&& data, const PenTool& tool, uint32_t fadeDurationMs = 2500);

    /// @brief Commits finished stroke data with pre-computed polygon outline geometry.
    void CommitStroke(FinishedStrokeData&& data, const PenTool& tool);

    /// @brief Commits raw 1D stroke segments by computing polygon outline hulls on the fly.
    void CommitStroke(std::vector<Segment1D>&& segments, const PenTool& tool);

    /// @brief Adds any CanvasObject (Image, TextBox, PDF, Ink) to the active page with undo tracking.
    void AddObject(const std::shared_ptr<CanvasObject>& obj);

    /// @brief Convenience helper to add an ImageObject to the active page.
    void AddImage(const std::shared_ptr<Folio::ImageObject>& img);

    /// @brief Convenience helper to add a TextBoxObject to the active page.
    void AddTextBox(const std::shared_ptr<Folio::TextBoxObject>& textBox);

    /// @brief Selects all visible, selectable, unlocked objects on the active page.
    size_t SelectAll();

    /// @brief Deselects all objects on the active page.
    void DeselectAll();

    /// @brief Retrieves all currently selected objects on the active page.
    [[nodiscard]] std::vector<std::shared_ptr<CanvasObject>> GetSelectedObjects() const;

    /// @brief Deletes all currently selected objects atomically with undo support.
    size_t DeleteSelection();

    /// @brief Checks whether the session clipboard currently holds copied/cut objects.
    [[nodiscard]] bool HasClipboardContent() const noexcept { return !clipboardObjects.empty(); }

    /// @brief Copies the currently selected objects on the active page into clipboard.
    void CopySelection();

    /// @brief Copies the specified canvas objects into the session clipboard.
    void CopySelection(const std::vector<std::shared_ptr<CanvasObject>>& selected);

    /// @brief Cuts the currently selected objects: copies to clipboard and removes from page.
    std::vector<std::shared_ptr<CanvasObject>> CutSelection();

    /// @brief Cuts the specified objects: copies to clipboard and removes from page with undo.
    std::vector<std::shared_ptr<CanvasObject>> CutSelection(const std::vector<std::shared_ptr<CanvasObject>>& selected);

    /// @brief Pastes session clipboard contents centered at (worldX, worldY) with atomic undo.
    std::vector<std::shared_ptr<CanvasObject>> PasteObjects(double worldX, double worldY);

    /// @brief Duplicates selected objects in-place with an offset (+offsetMm X and Y).
    std::vector<std::shared_ptr<CanvasObject>> DuplicateSelection(double offsetMm = 10.0);

    /// @brief Duplicates specified objects in-place with an offset (+offsetMm X and Y).
    std::vector<std::shared_ptr<CanvasObject>> DuplicateSelection(const std::vector<std::shared_ptr<CanvasObject>>& selected, double offsetMm = 10.0);

    /// @brief Groups selected objects under a shared UUID v4 with undo tracking.
    std::string GroupSelection();

    /// @brief Ungroups any selected grouped objects, restoring them to individual items.
    size_t UngroupSelection();

    /// @brief Locks selected objects as immutable background templates at z-order 0.
    size_t LockSelectionAsBackground();

    /// @brief Unlocks specific objects by UID, restoring their selectability.
    size_t UnlockObjects(const std::vector<uint32_t>& objectUids);

    /// @brief Unlocks all background templates and locked objects on the active page.
    size_t UnlockAllBackgroundTemplates();

    /// @brief Retrieves all locked background template objects on the active page.
    [[nodiscard]] std::vector<std::shared_ptr<CanvasObject>> GetBackgroundTemplateObjects() const;

    // -------------------------------------------------------------------------
    // 5. Page Metadata, Styling, Autosave & Observers (session_metadata.cpp)
    // -------------------------------------------------------------------------

    /// @brief Returns a structured metadata snapshot of the currently active CanvasPage.
    [[nodiscard]] PageMetadataDTO GetActivePageMetadata() const;

    /// @brief Renames the active CanvasPage and touches its timestamp.
    void SetPageTitle(std::string newTitle);

    /// @brief Updates the paper rule style (Grid, Ruled, Blank, Dotted, Cornell, etc.).
    void SetPaperStyle(PaperStyle style);

    /// @brief Updates physical rule or grid spacing in millimeters for the active page.
    void SetGridSpacingMm(double spacingMm);

    /// @brief Configures page border demarcation settings on the active page.
    void SetPageBorderSettings(bool show, PageBorderType type, PageBorderStyle style, double width = 1.5);

    /// @brief Configures physical sheet dimensions and orientation for the active page.
    void SetPageSizeFormat(PageSizeFormat format, bool isLandscape, double customW = 215.9, double customH = 279.4);

    /// @brief Configures canvas infinity boundary mode (SemiInfinity, FullInfinity, etc.).
    void SetCanvasInfinityMode(CanvasInfinityMode mode);

    /// @brief Checks whether the active page or any resident pages have unsaved modifications.
    [[nodiscard]] bool HasUnsavedChanges() const;

    /// @brief Flushes the active CanvasPage asynchronously to disk via atomic .ink staging.
    std::future<bool> SaveActivePageAsync();

    /// @brief Flushes all modified pages across the active notebook to disk asynchronously.
    void SaveAllModifiedPages();

    /// @brief Registers an observer to receive document session mutation events.
    void AddObserver(Folio::IDocumentSessionObserver* observer);

    /// @brief Unregisters an observer from receiving document session events.
    void RemoveObserver(Folio::IDocumentSessionObserver* observer);

    /// @brief Broadcasts an active page change event to registered observers (re-entrancy guarded).
    void NotifyActivePageChanged(const std::shared_ptr<CanvasPage>& newPage,
                                 const std::shared_ptr<CanvasPage>& oldPage);

    /// @brief Broadcasts an active section change event to registered observers.
    void NotifyActiveSectionChanged(const std::shared_ptr<Section>& newSec,
                                    const std::shared_ptr<Section>& oldSec);

    /// @brief Broadcasts an active notebook change event to registered observers.
    void NotifyActiveNotebookChanged(const std::shared_ptr<Notebook>& newNb,
                                     const std::shared_ptr<Notebook>& oldNb);

    /// @brief Broadcasts an undo/redo stack availability transition event.
    void NotifyHistoryChanged();

    /// @brief Broadcasts a page dirty/modified state event.
    void NotifyPageModified(const std::shared_ptr<CanvasPage>& page);

    /// @brief Broadcasts a new page creation event.
    void NotifyPageCreated(const std::shared_ptr<CanvasPage>& page);

    /// @brief Broadcasts a page deletion/soft-deletion event.
    void NotifyPageDeleted(const std::string& pageGuid);

    // -------------------------------------------------------------------------
    // 6. Page Revision History & Rollback (Google Docs-Style) (session_metadata.cpp)
    // -------------------------------------------------------------------------

    /// @brief Captures a point-in-time Google Docs-style revision snapshot of the active page.
    bool CreateActivePageRevision(const std::string& versionName = "", bool isNamed = false);

    /// @brief Discovers and lists all historical revisions for the active page, sorted newest first.
    [[nodiscard]] std::vector<Folio::PageRevisionInfo> GetActivePageRevisions() const;

    /// @brief Restores the active page to a specific historical revision with automatic safety snapshot.
    bool RestoreActivePageRevision(const std::string& versionId, bool createSafetySnapshot = true);

    /// @brief Renames a historical revision and pins it as a named milestone.
    bool NameActivePageRevision(const std::string& versionId, const std::string& newName);
};