/**
 * =========================================================================================
 * @file session_history.cpp
 * @brief Implementation of DocumentSession transactional history, GoF composite macro commands,
 *        and continuous eraser gesture aggregation.
 * =========================================================================================
 *
 * ARCHITECTURAL & MATHEMATICAL PROCESS:
 * - Implements the Command Pattern for reversible canvas mutations.
 * - Manages GoF Composite Pattern via MacroTransactionState, batching multi-step operations into
 *   a single atomic undoable unit.
 * - Aggregates continuous high-frequency (120Hz) stylus eraser sweeps into a unified BatchEraseCommand
 *   committed only upon pointer-up.
 */

#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

// -----------------------------------------------------------------------------
// Transactional Undo / Redo
// -----------------------------------------------------------------------------

/**
 * @brief Reverses the most recent canvas command on the active page.
 * @param engine Optional pointer to CanvasEngine to trigger dirty re-render and composite rebake.
 * @return true if an action was undone, false if undo stack is empty or active page is null.
 */
bool DocumentSession::Undo(CanvasEngine* engine) {
    auto activePage = GetActivePage();
    if (!activePage) {
        LOG_WARN(DocumentSession, "Undo rejected: No active CanvasPage available.");
        return false;
    }
    bool success = activePage->history.Undo(*activePage, engine);
    if (success) {
        activePage->isModified = true;
        NotifyHistoryChanged();
        NotifyPageModified(activePage);
        LOG_INFO(DocumentSession, "Undo successfully applied on page '" + activePage->title + "'");
    }
    return success;
}

/**
 * @brief Re-applies the most recently reversed canvas command on the active page.
 * @param engine Optional pointer to CanvasEngine to trigger dirty re-render and composite rebake.
 * @return true if an action was redone, false if redo stack is empty or active page is null.
 */
bool DocumentSession::Redo(CanvasEngine* engine) {
    auto activePage = GetActivePage();
    if (!activePage) {
        LOG_WARN(DocumentSession, "Redo rejected: No active CanvasPage available.");
        return false;
    }
    bool success = activePage->history.Redo(*activePage, engine);
    if (success) {
        activePage->isModified = true;
        NotifyHistoryChanged();
        NotifyPageModified(activePage);
        LOG_INFO(DocumentSession, "Redo successfully applied on page '" + activePage->title + "'");
    }
    return success;
}

/**
 * @brief Checks if there are any commands available to undo on the active page.
 */
bool DocumentSession::CanUndo() const {
    auto activePage = GetActivePage();
    return activePage ? activePage->history.CanUndo() : false;
}

/**
 * @brief Checks if there are any commands available to redo on the active page.
 */
bool DocumentSession::CanRedo() const {
    auto activePage = GetActivePage();
    return activePage ? activePage->history.CanRedo() : false;
}

// -----------------------------------------------------------------------------
// Macro Transaction Subsystem (GoF Composite Pattern)
// -----------------------------------------------------------------------------

/**
 * @brief Begins a multi-step macro transaction.
 * @param description Human-readable label for history inspection and debugging.
 * @return true if the macro transaction was successfully initiated.
 */
bool DocumentSession::BeginMacroTransaction(std::string description) {
    auto activePage = GetActivePage();
    if (!activePage) {
        LOG_WARN(DocumentSession, "BeginMacroTransaction rejected: No active page available.");
        return false;
    }
    if (macroTx.isActive) {
        LOG_WARN(DocumentSession, "BeginMacroTransaction: A macro transaction was already active. Ending previous.");
        EndMacroTransaction();
    }
    macroTx.isActive = true;
    macroTx.description = description;
    macroTx.boundPage = activePage;
    macroTx.macro = std::make_unique<Folio::MacroCommand>(std::move(description));
    return true;
}

/**
 * @brief Commits the active macro transaction, recording the aggregated MacroCommand to page history.
 * @return true if a non-empty macro was committed, false if empty or inactive.
 */
bool DocumentSession::EndMacroTransaction() {
    if (!macroTx.isActive || !macroTx.macro) {
        macroTx.isActive = false;
        macroTx.macro.reset();
        macroTx.boundPage.reset();
        return false;
    }

    auto page = macroTx.boundPage ? macroTx.boundPage : GetActivePage();
    bool committed = false;
    if (page && !macroTx.macro->IsEmpty()) {
        page->history.RecordCommand(std::move(macroTx.macro));
        page->isModified = true;
        NotifyHistoryChanged();
        NotifyPageModified(page);
        committed = true;
        LOG_INFO(DocumentSession, "EndMacroTransaction: Committed macro '" + macroTx.description + "'");
    }

    macroTx.isActive = false;
    macroTx.macro.reset();
    macroTx.boundPage.reset();
    return committed;
}

/**
 * @brief Cancels an in-flight macro transaction, rolling back (undoing) any sub-commands executed.
 */
void DocumentSession::CancelMacroTransaction() {
    if (!macroTx.isActive || !macroTx.macro) {
        macroTx.isActive = false;
        macroTx.macro.reset();
        macroTx.boundPage.reset();
        return;
    }
    auto page = macroTx.boundPage ? macroTx.boundPage : GetActivePage();
    if (page) {
        macroTx.macro->Undo(*page);
        page->isModified = true;
        NotifyPageModified(page);
    }
    LOG_INFO(DocumentSession, "CancelMacroTransaction: Rolled back macro '" + macroTx.description + "'");
    macroTx.isActive = false;
    macroTx.macro.reset();
    macroTx.boundPage.reset();
}

/**
 * @brief Records a command to the target page history or appends it to the active MacroTransaction.
 * @param page Target CanvasPage receiving the command.
 * @param cmd Unique pointer to concrete ICanvasCommand.
 */
void DocumentSession::RecordHistoryCommand(std::shared_ptr<CanvasPage> page, std::unique_ptr<Folio::ICanvasCommand> cmd) {
    if (!page || !cmd) return;
    if (macroTx.isActive && macroTx.macro) {
        macroTx.macro->AddCommand(std::move(cmd));
    } else {
        page->history.RecordCommand(std::move(cmd));
        NotifyHistoryChanged();
    }
}

// -----------------------------------------------------------------------------
// Continuous Compound Eraser Transaction API
// -----------------------------------------------------------------------------

/**
 * @brief Starts a new atomic continuous eraser transaction bound to the active page.
 * Called on pointer-down / stylus-contact when the eraser tool is active.
 */
void DocumentSession::BeginEraseTransaction() {
    auto activePage = GetActivePage();
    eraseTx.isActive = true;
    eraseTx.targetPageGuid = activePage ? activePage->guid : "";
    eraseTx.deletedObjects.clear();
    eraseTx.slicedStrokes.clear();
    eraseTx.originalStrokeIndexMap.clear();
    eraseTx.recordedDeletedUids.clear();
}

/**
 * @brief Cancels an active eraser transaction without committing changes to history.
 */
void DocumentSession::CancelEraseTransaction() {
    eraseTx.isActive = false;
    eraseTx.targetPageGuid.clear();
    eraseTx.deletedObjects.clear();
    eraseTx.slicedStrokes.clear();
    eraseTx.originalStrokeIndexMap.clear();
    eraseTx.recordedDeletedUids.clear();
}

/**
 * @brief Records an object deleted during the continuous eraser gesture.
 * @param obj The canvas object being removed.
 */
void DocumentSession::RecordErasedObject(const std::shared_ptr<CanvasObject>& obj) {
    if (!obj) return;
    auto activePage = GetActivePage();
    if (!eraseTx.isActive || (activePage && activePage->guid != eraseTx.targetPageGuid)) {
        // Standalone deletion outside an active pointer-drag transaction or page switched mid-drag
        if (activePage) {
            RecordHistoryCommand(activePage, std::make_unique<Folio::RemoveObjectsCommand>(obj));
            activePage->isModified = true;
        }
        return;
    }
    if (eraseTx.recordedDeletedUids.insert(obj->uid).second) {
        eraseTx.deletedObjects.push_back(obj);
    }
}

/**
 * @brief Records a stroke slicing operation during a continuous point eraser drag.
 * @param original Deep clone of the pristine original stroke container prior to any slicing.
 * @param survivingFragments Vector of surviving stroke fragments resulting from slicing.
 */
void DocumentSession::RecordSlicedStroke(const std::shared_ptr<InkContainer>& original,
                                        const std::vector<std::shared_ptr<InkContainer>>& survivingFragments) {
    if (!original) return;
    auto activePage = GetActivePage();
    if (!eraseTx.isActive || (activePage && activePage->guid != eraseTx.targetPageGuid)) {
        if (activePage) {
            std::vector<Folio::BatchEraseCommand::SlicedStrokeEntry> entries = { { original, survivingFragments } };
            RecordHistoryCommand(activePage, std::make_unique<Folio::BatchEraseCommand>(
                std::vector<std::shared_ptr<CanvasObject>>{},
                std::move(entries)
            ));
            activePage->isModified = true;
        }
        return;
    }
    auto it = eraseTx.originalStrokeIndexMap.find(original->uid);
    if (it != eraseTx.originalStrokeIndexMap.end()) {
        eraseTx.slicedStrokes[it->second].generatedFragments = survivingFragments;
    } else {
        size_t idx = eraseTx.slicedStrokes.size();
        eraseTx.originalStrokeIndexMap[original->uid] = idx;
        eraseTx.slicedStrokes.push_back({ original, survivingFragments });
    }
}

/**
 * @brief Finalizes the active continuous eraser transaction.
 * Commits BatchEraseCommand only if the active page matches the transaction origin.
 *
 * @param engine Optional CanvasEngine pointer to update dirty rebake state.
 */
void DocumentSession::EndEraseTransaction(CanvasEngine* engine) {
    if (!eraseTx.isActive) return;
    eraseTx.isActive = false;

    auto activePage = GetActivePage();
    if (activePage && activePage->guid == eraseTx.targetPageGuid &&
        (!eraseTx.deletedObjects.empty() || !eraseTx.slicedStrokes.empty())) {
        auto cmd = std::make_unique<Folio::BatchEraseCommand>(
            std::move(eraseTx.deletedObjects),
            std::move(eraseTx.slicedStrokes)
        );
        RecordHistoryCommand(activePage, std::move(cmd));
        activePage->isModified = true;
        NotifyHistoryChanged();
        NotifyPageModified(activePage);
        LOG_INFO(DocumentSession, "Committed compound BatchEraseCommand to page history.");
    } else if (!eraseTx.targetPageGuid.empty() && activePage && activePage->guid != eraseTx.targetPageGuid) {
        LOG_WARN(DocumentSession, "EndEraseTransaction aborted: Active page switched from '" +
                 eraseTx.targetPageGuid + "' to '" + activePage->guid + "' during erase gesture.");
    }

    CancelEraseTransaction();
}
