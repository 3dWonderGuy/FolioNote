/**
 * @file input_state_machine_mouse.cpp
 * @brief Implementation of Mouse input dispatching: navigation, transient pan overrides,
 *        gizmo cursors, selection, and scroll-wheel zooming.
 *
 * NAVIGATION OVERVIEW:
 * --------------------
 * Unlike a stylus, which defaults to inking, the mouse primarily functions as a navigation
 * and selection device. Transient navigation modes are supported:
 *   1. Middle-Click Drag: Immediately pans the canvas (`canvas.Pan(dx, dy)`) without modifying tool state.
 *   2. Spacebar + Left Drag: Standard graphics editor hand tool navigation.
 *   3. Mouse Wheel: Continuous zoom centered at current cursor position (`canvas.ZoomAt(x, y, factor)`).
 */

#include "input/stateMachine/input_state_machine.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

void InputStateMachine::DispatchMouse(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    InteractionState oldAction = currentAction;

    // -------------------------------------------------------------------------
    // 1. TRANSIENT TOOL EVALUATION
    // -------------------------------------------------------------------------
    // Space bar or middle mouse button transiently overrides the current tool with Panning
    if (mouse.middleButton || keyboard.space) {
        currentAction = InteractionState::Panning;
    } else {
        currentAction = mouseTool.savedTool;
    }

    if (currentAction != InteractionState::DrawingShape && canvas.shapeCreation.lockDrawingMode) {
        canvas.shapeCreation.lockDrawingMode = false;
        canvas.shapeCreation.isActive = false;
    }

    if (oldAction != currentAction) {
        std::string actionName = (currentAction == InteractionState::Inking) ? "Inking" :
                                 (currentAction == InteractionState::Eraser) ? "Eraser" :
                                 (currentAction == InteractionState::Selecting) ? "Selecting" :
                                 (currentAction == InteractionState::Panning) ? "Panning" :
                                 (currentAction == InteractionState::DrawingShape) ? "DrawingShape" : "Idle";
        LOG_INFO(InputStateMachine, "Mouse interaction state changed to: " + actionName);
    }

    // Edge transition booleans
    const bool middleJustDown = mouse.middleButton && !wasMiddleDown;
    const bool middleIsMoving = mouse.middleButton && wasMiddleDown;
    const bool middleJustUp   = !mouse.middleButton && wasMiddleDown;

    const bool justDown = mouse.leftButton && !wasMouseDown;
    const bool isMoving = mouse.leftButton && wasMouseDown;
    const bool justUp   = !mouse.leftButton && wasMouseDown;

    // -------------------------------------------------------------------------
    // 2. DIRECT MIDDLE & SPACE PAN (Fires before UI capture)
    // -------------------------------------------------------------------------
    if (currentAction == InteractionState::Panning && middleIsMoving) {
        canvas.Pan(mouse.dx, mouse.dy);
    }

    if (keyboard.space && (isMoving || middleIsMoving)) {
        canvas.Pan(mouse.dx, mouse.dy);
    }

    if (middleJustUp) {
        mouse.dx = 0.0f;
        mouse.dy = 0.0f;
    }

    // -------------------------------------------------------------------------
    // 3. UI CAPTURE CHECK
    // -------------------------------------------------------------------------
    if (justDown) {
        uiCapturedMouse = imguiWantsInput;

        // Double click detection on canvas
        if (!uiCapturedMouse) {
            uint64_t now = SDL_GetTicks();
            if (specialActions.EvaluateDoubleClick(mouse.x, mouse.y, now, config)) {
                LOG_INFO(InputStateMachine, "Mouse Double-Click detected!");
            }
        }
    }

    if (uiCapturedMouse) {
        if (justUp) uiCapturedMouse = false;
        return;
    }

    const float canvasLocalX = mouse.x - canvasOriginX;
    const float canvasLocalY = mouse.y - canvasOriginY;

    if (currentAction != InteractionState::Eraser) {
        canvas.HideEraserCursor();
    }

    // -------------------------------------------------------------------------
    // 4. MOUSE ACTION DISPATCH
    // -------------------------------------------------------------------------
    if (currentAction == InteractionState::Inking) {
        if (justDown) {
            canvas.OnPointerDown(canvasLocalX, canvasLocalY, 1.0f, latestEventTimeSec, palette.GetActivePen(), 0.0f, 0.0f);
        } else if (isMoving) {
            canvas.OnPointerMove(canvasLocalX, canvasLocalY, 1.0f, latestEventTimeSec, 0.0f, 0.0f);
        } else if (justUp) {
            canvas.OnPointerUp(session, palette.GetActivePen());
        }
    } 
    else if (currentAction == InteractionState::Eraser) {
        if (justDown) {
            lastEraserX = canvasLocalX;
            lastEraserY = canvasLocalY;
            isEraserActive = true;
            canvas.EraseSegment(canvasLocalX, canvasLocalY, canvasLocalX, canvasLocalY, eraserRadiusMm, session, isStrokeEraser);
        } else if (isMoving && isEraserActive) {
            canvas.EraseSegment(lastEraserX, lastEraserY, canvasLocalX, canvasLocalY, eraserRadiusMm, session, isStrokeEraser);
            lastEraserX = canvasLocalX;
            lastEraserY = canvasLocalY;
        } else if (justUp) {
            isEraserActive = false;
        }
        canvas.SetEraserCursor(canvasLocalX, canvasLocalY, eraserRadiusMm, isEraserActive, isStrokeEraser);
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
    else if (currentAction == InteractionState::Selecting) {
        if (justDown) {
            if (canvas.selectionGizmo.OnPointerDown(canvasLocalX, canvasLocalY, canvas.transform,
                                                   canvas.shapeCreation.lockToGrid, canvas.gridSpacingMm)) {
                canvas.isDirty = true;
            } else {
                Point2D worldMm = canvas.transform.ScreenToWorld(canvasLocalX, canvasLocalY);
                auto activePage = session.GetActivePage();
                std::shared_ptr<CanvasObject> clickedObj = nullptr;

                if (activePage) {
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
                    LOG_INFO(InputStateMachine, "Mouse direct click selected object uid=" + std::to_string(clickedObj->uid));
                } else {
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
                canvas.selectionGizmo.OnPointerUp();
                canvas.SyncSelectionToSpatialIndex(&session);
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            } else if (canvas.selectionMode == CanvasEngine::SelectionMode::Lasso) {
                canvas.OnLassoUp(&session);
            } else if (canvas.marqueeBox.isActive) {
                canvas.OnBoxSelectUp(&session);
            }
        }

        // Dynamically update mouse cursor based on hovered gizmo handle
        if (!canvas.selectionGizmo.isDragging && canvas.selectionGizmo.HasSelection()) {
            auto hit = canvas.selectionGizmo.HitTest(canvasLocalX, canvasLocalY, canvas.transform);
            if (hit.hit) {
                switch (hit.role) {
                    case HandleRole::TopLeft:
                    case HandleRole::BottomRight:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                        break;
                    case HandleRole::TopRight:
                    case HandleRole::BottomLeft:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                        break;
                    case HandleRole::TopCenter:
                    case HandleRole::BottomCenter:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                        break;
                    case HandleRole::LeftCenter:
                    case HandleRole::RightCenter:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        break;
                    case HandleRole::Rotation:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        break;
                    case HandleRole::Body:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                        break;
                    default:
                        break;
                }
            }
        } else if (canvas.selectionGizmo.isDragging) {
            if (canvas.selectionGizmo.activeRole == HandleRole::Rotation) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else if (canvas.selectionGizmo.activeRole == HandleRole::Body) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
        }
    }
    else if (currentAction == InteractionState::DrawingShape) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            canvas.CancelShapeCreation();
            canvas.ClearSelection(&session);
            currentAction = InteractionState::Selecting;
            SetToolForDevice(DeviceType::Mouse, InteractionState::Selecting);
            SetToolForDevice(DeviceType::Stylus, InteractionState::Selecting);
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
                        SetToolForDevice(DeviceType::Mouse, InteractionState::Selecting);
                        SetToolForDevice(DeviceType::Stylus, InteractionState::Selecting);
                    }
                }
            }
        }

        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }
    else if (currentAction == InteractionState::Panning && isMoving) {
        canvas.Pan(mouse.dx, mouse.dy);
    }

    // -------------------------------------------------------------------------
    // 5. SCROLL WHEEL ZOOM
    // -------------------------------------------------------------------------
    // Zoom around cursor position using configured multipliers:
    //   scaleFactor = (wheelY > 0) ? config.mouseWheelZoomInFactor : config.mouseWheelZoomOutFactor
    if (mouse.wheelY != 0.0f) {
        if (isCanvasHovered && !imguiWantsInput) {
            float factor = (mouse.wheelY > 0.0f) ? config.mouseWheelZoomInFactor : config.mouseWheelZoomOutFactor;
            canvas.ZoomAt(canvasLocalX, canvasLocalY, factor);
        }
        mouse.wheelX = 0.0f;
        mouse.wheelY = 0.0f;
    }
}
