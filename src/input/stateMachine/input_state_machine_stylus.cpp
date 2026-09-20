/**
 * @file input_state_machine_stylus.cpp
 * @brief Implementation of Stylus input dispatching: inking, selecting, erasing, shape creation,
 *        and stylus gesture / double-tap handling.
 *
 * COORDINATE SPACE MAPPING:
 * -------------------------
 * Raw stylus coordinates reported by SDL3 are in window pixel coordinates: $(x_{pen}, y_{pen})$.
 * CanvasEngine maintains an origin offset $(canvasOriginX, canvasOriginY)$ corresponding to
 * top/side UI chrome (ribbon, navigation drawer).
 *
 * Local canvas projection:
 *   $x_{local} = x_{pen} - canvasOriginX$
 *   $y_{local} = y_{pen} - canvasOriginY$
 *
 * When hit-testing or creating objects, $x_{local}, y_{local}$ is projected into continuous
 * world millimeter space via the camera transform matrix:
 *   $P_{world} = \text{ScreenToWorld}(x_{local}, y_{local})$
 */

#include "input/stateMachine/input_state_machine.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

void InputStateMachine::DispatchStylus(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    const bool justDown = (currentStylusState == StylusState::Engaged && oldStylusState != StylusState::Engaged);
    const bool isMoving = (currentStylusState == StylusState::Engaged && oldStylusState == StylusState::Engaged);
    const bool justUp   = (currentStylusState != StylusState::Engaged && oldStylusState == StylusState::Engaged);

    // -------------------------------------------------------------------------
    // 1. UI CAPTURE EVALUATION
    // -------------------------------------------------------------------------
    // If stylus contact initiated over UI chrome (e.g. toolbar button), lock capture
    // to UI until the stylus lifts off the screen.
    if (justDown) {
        uiCapturedStylus = imguiWantsInput;

        // Check for double-tap special action (e.g. quick toggle to eraser or undo)
        if (!uiCapturedStylus) {
            uint64_t now = SDL_GetTicks();
            if (specialActions.EvaluateDoubleClick(pen.x, pen.y, now, config)) {
                LOG_INFO(InputStateMachine, "Stylus Double-Tap detected! Toggling eraser/pen tool.");
                // If currently inking, toggle to eraser; if eraser, toggle back to inking
                if (currentAction == InteractionState::Inking) {
                    currentAction = InteractionState::Eraser;
                    SetToolForDevice(DeviceType::Stylus, InteractionState::Eraser);
                } else if (currentAction == InteractionState::Eraser) {
                    currentAction = InteractionState::Inking;
                    SetToolForDevice(DeviceType::Stylus, InteractionState::Inking);
                }
            }
        }
    }

    if (uiCapturedStylus) {
        if (justUp) uiCapturedStylus = false;
        return;
    }

    // Transform from window pixel space to canvas-local coordinates
    const float canvasLocalX = pen.x - canvasOriginX;
    const float canvasLocalY = pen.y - canvasOriginY;

    if (currentAction != InteractionState::DrawingShape && canvas.shapeCreation.lockDrawingMode) {
        canvas.shapeCreation.lockDrawingMode = false;
        canvas.shapeCreation.isActive = false;
    }

    // -------------------------------------------------------------------------
    // 2. SEMANTIC ACTION DISPATCH
    // -------------------------------------------------------------------------
    switch (currentAction) {
        // =====================================================================
        // INKING (Pressure & Tilt Sensitive Digital Ink)
        // =====================================================================
        case InteractionState::Inking: {
            if (justDown) {
                canvas.OnPointerDown(canvasLocalX, canvasLocalY, pen.pressure, latestEventTimeSec, palette.GetActivePen(), pen.tiltX, pen.tiltY);
            } else if (isMoving) {
                canvas.OnPointerMove(canvasLocalX, canvasLocalY, pen.pressure, latestEventTimeSec, pen.tiltX, pen.tiltY);
            } else if (justUp) {
                canvas.OnPointerUp(session, palette.GetActivePen());
            }
            break;
        }

        // =====================================================================
        // SELECTING (Object Hit-Testing, Gizmo Manipulation, Lasso / Box Selection)
        // =====================================================================
        case InteractionState::Selecting: {
            if (justDown) {
                // First test if pointer clicked an active selection gizmo handle
                if (canvas.selectionGizmo.OnPointerDown(canvasLocalX, canvasLocalY, canvas.transform,
                                                       canvas.shapeCreation.lockToGrid, canvas.gridSpacingMm)) {
                    canvas.isDirty = true;
                } else {
                    // Convert local coordinates to world millimeters for spatial hit-testing
                    Point2D worldMm = canvas.transform.ScreenToWorld(canvasLocalX, canvasLocalY);
                    auto activePage = session.GetActivePage();
                    std::shared_ptr<CanvasObject> clickedObj = nullptr;

                    if (activePage) {
                        // Reverse iterate to test topmost objects first
                        for (auto it = activePage->objects.rbegin(); it != activePage->objects.rend(); ++it) {
                            auto& obj = *it;
                            if (obj && obj->isVisible && obj->isSelectable &&
                                (obj->HitTest(worldMm.x, worldMm.y) || obj->HitTestCircle(worldMm.x, worldMm.y, config.objectHitTestRadiusMm))) {
                                clickedObj = obj;
                                break;
                            }
                        }
                    }

                    if (clickedObj) {
                        canvas.ClearSelection(&session);
                        clickedObj->isSelected = 1;
                        canvas.selectionGizmo.SetSelectedObjects(activePage->objects);
                        canvas.selectionGizmo.OnPointerDown(canvasLocalX, canvasLocalY, canvas.transform,
                                                               canvas.shapeCreation.lockToGrid, canvas.gridSpacingMm);
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                        LOG_INFO(InputStateMachine, "Stylus direct click selected object uid=" + std::to_string(clickedObj->uid));
                    } else {
                        // Clicked empty canvas: clear selection and begin lasso or box select
                        canvas.ClearSelection(&session);
                        if (canvas.selectionMode == CanvasEngine::SelectionMode::Lasso) {
                            canvas.OnLassoDown(canvasLocalX, canvasLocalY);
                        } else {
                            canvas.OnBoxSelectDown(canvasLocalX, canvasLocalY);
                        }
                    }
                }
            }
            else if (isMoving) {
                if (canvas.selectionGizmo.isDragging) {
                    if (canvas.selectionGizmo.OnPointerMove(canvasLocalX, canvasLocalY, canvas.transform,
                                                           canvas.shapeCreation.lockToGrid, canvas.gridSpacingMm)) {
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                } else if (canvas.selectionMode == CanvasEngine::SelectionMode::Lasso) {
                    canvas.OnLassoMove(canvasLocalX, canvasLocalY);
                } else if (canvas.marqueeBox.isActive) {
                    canvas.OnBoxSelectMove(canvasLocalX, canvasLocalY);
                }
            }
            else if (justUp) {
                if (canvas.selectionGizmo.isDragging) {
                    canvas.selectionGizmo.OnPointerUp(&session);
                    canvas.SyncSelectionToSpatialIndex(&session);
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                } else if (canvas.selectionMode == CanvasEngine::SelectionMode::Lasso) {
                    canvas.OnLassoUp(&session);
                } else if (canvas.marqueeBox.isActive) {
                    canvas.OnBoxSelectUp(&session);
                }
            }
            break;
        }

        // =====================================================================
        // ERASER (Segment and Stroke Erasing)
        // =====================================================================
        case InteractionState::Eraser: {
            if (justDown) {
                lastEraserX = canvasLocalX;
                lastEraserY = canvasLocalY;
                isEraserActive = true;
                session.BeginEraseTransaction();
                canvas.EraseSegment(canvasLocalX, canvasLocalY, canvasLocalX, canvasLocalY, eraserRadiusMm, session, isStrokeEraser);
            } else if (isMoving && isEraserActive) {
                canvas.EraseSegment(lastEraserX, lastEraserY, canvasLocalX, canvasLocalY, eraserRadiusMm, session, isStrokeEraser);
                lastEraserX = canvasLocalX;
                lastEraserY = canvasLocalY;
            } else if (justUp) {
                isEraserActive = false;
                session.EndEraseTransaction(&canvas);
            }
            canvas.SetEraserCursor(canvasLocalX, canvasLocalY, eraserRadiusMm, isEraserActive, isStrokeEraser);
            break;
        }

        // =====================================================================
        // DRAWING SHAPE (Geometric Vector Shape Drag-Creation)
        // =====================================================================
        case InteractionState::DrawingShape: {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                canvas.CancelShapeCreation();
                canvas.ClearSelection(&session);
                currentAction = InteractionState::Selecting;
                SetToolForDevice(DeviceType::Stylus, InteractionState::Selecting);
                SetToolForDevice(DeviceType::Mouse, InteractionState::Selecting);
            } else if (justDown) {
                // Clean state isolation: suppress previous gizmo interactions when actively drawing
                if (canvas.selectionGizmo.HasSelection()) {
                    canvas.ClearSelection(&session);
                    canvas.selectionGizmo.ClearSelection();
                }
                canvas.OnShapeDrawDown(canvasLocalX, canvasLocalY, &session);
                canvas.isDirty = true;
            } else if (isMoving) {
                if (canvas.shapeCreation.isDragging || (canvas.shapeCreation.shapeType == Folio::ShapeType::Ellipse && canvas.shapeCreation.ellipseStep == 1)) {
                    canvas.OnShapeDrawMove(canvasLocalX, canvasLocalY, &session);
                    canvas.isDirty = true;
                }
            } else if (justUp) {
                if (canvas.shapeCreation.isDragging || (canvas.shapeCreation.shapeType == Folio::ShapeType::Ellipse && canvas.shapeCreation.ellipseStep == 1)) {
                    canvas.OnShapeDrawUp(&session);
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                    if (canvas.shapeCreation.ellipseStep == 0) {
                        if (!canvas.shapeCreation.lockDrawingMode) {
                            currentAction = InteractionState::Selecting;
                            SetToolForDevice(DeviceType::Stylus, InteractionState::Selecting);
                            SetToolForDevice(DeviceType::Mouse, InteractionState::Selecting);
                        }
                    }
                }
            }
            break;
        }

        default: break;
    }
}
