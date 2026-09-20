#pragma once
/**
 * =========================================================================================
 * @file undo_redo_manager.hpp
 * @brief Centralized Command Architecture (ICommand & CommandManager) for FolioNote.
 * =========================================================================================
 *
 * GENERAL ARCHITECTURE & WORKING PROCESS:
 * ---------------------------------------
 * 1. Command Pattern:
 *    Every canvas mutation (adding ink, shapes, text, modifying text, moving selection, erasing)
 *    is encapsulated into a discrete, reversible atomic unit implementing `ICommand`
 *    (`Folio::ICanvasCommand`).
 * 2. Unification with CommandManager / CommandHistory:
 *    `CommandManager` serves as the centralized per-page and cross-system history manager.
 *    It maintains bounded undo/redo stacks (`undoStack`, `redoStack`) and guarantees that
 *    every mutation updates the R-Tree spatial index, active page dirty flags, and selection state.
 * 3. Bidirectional Invariants:
 *    - Execute(page, engine): applies forward mutation, updates R-Tree bounds, invalidates viewport caches.
 *    - Undo(page, engine): reverses mutation, restores previous geometry/properties, invalidates viewport caches.
 *    - Redo(page, engine): re-executes reversed mutation with zero pointer corruption or orphaned objects.
 */

#include <vector>
#include <memory>
#include "core/history/canvas_command.hpp"
#include "core/history/command_history.hpp"

// Universal type aliases unifying the command architecture
using ICommand           = Folio::ICanvasCommand;
using CommandManager     = Folio::CommandHistory;
using AddObjectCommand   = Folio::AddObjectCommand;
using AddObjectsCommand  = Folio::AddObjectsCommand;
using DeleteCommand      = Folio::RemoveObjectsCommand;
using RemoveObjectsCommand = Folio::RemoveObjectsCommand;
using TransformCommand   = Folio::TransformObjectsCommand;
using TransformObjectsCommand = Folio::TransformObjectsCommand;
using BatchEraseCommand  = Folio::BatchEraseCommand;
using ModifyTextCommand  = Folio::ModifyTextCommand;
using MacroCommand       = Folio::MacroCommand;
using GroupObjectsCommand = Folio::GroupObjectsCommand;
using UngroupObjectsCommand = Folio::UngroupObjectsCommand;
using LockObjectsCommand = Folio::LockObjectsCommand;
