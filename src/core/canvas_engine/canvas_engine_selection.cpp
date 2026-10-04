#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include <algorithm>

void CanvasEngine::OnLassoDown(float screenX, float screenY) {
    Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
    layerCompositor.GetLiveInteractionLayer().BeginLasso(worldMm.x, worldMm.y);
    isDirty = true;
}

void CanvasEngine::OnLassoMove(float screenX, float screenY) {
    Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
    layerCompositor.GetLiveInteractionLayer().AddLassoPoint(worldMm.x, worldMm.y);
    isDirty = true;
}

std::vector<Point2D> CanvasEngine::OnLassoUp(DocumentSession* session) {
    std::vector<Point2D> lasso = layerCompositor.GetLiveInteractionLayer().FinishLasso();
    isDirty = true;
    if (session && lasso.size() >= 3) {
        auto activePage = session->GetActivePage();
        if (activePage) {
            double minX = lasso[0].x, maxX = lasso[0].x;
            double minY = lasso[0].y, maxY = lasso[0].y;
            for (const auto& pt : lasso) {
                minX = std::min(minX, pt.x);
                maxX = std::max(maxX, pt.x);
                minY = std::min(minY, pt.y);
                maxY = std::max(maxY, pt.y);
            }
            AABB lassoBox(minX, minY, maxX, maxY);

            std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(lassoBox);
            for (uint32_t uid : candidateUids) {
                auto obj = activePage->FindObjectByUid(uid);
                if (obj && obj->isVisible && obj->isSelectable && obj->bounds.Intersects(lassoBox)) {
                    double objArea = obj->bounds.Area();
                    if (objArea > 1e-4) {
                        double isectArea = lassoBox.IntersectionArea(obj->bounds);
                        double coverageRatio = isectArea / objArea;
                        if (coverageRatio >= 0.50) {
                            obj->isSelected = 1;
                        }
                    } else {
                        if (lassoBox.Contains((obj->bounds.minX + obj->bounds.maxX) * 0.5,
                                              (obj->bounds.minY + obj->bounds.maxY) * 0.5)) {
                            obj->isSelected = 1;
                        }
                    }
                }
            }
            selectionGizmo.SetSelectedObjects(activePage->objects);
            needsFullRebake = true;
        }
    }
    return lasso;
}

void CanvasEngine::OnBoxSelectDown(float screenX, float screenY) {
    marqueeBox.isActive = true;
    marqueeBox.startScreenX = screenX;
    marqueeBox.startScreenY = screenY;
    marqueeBox.currentScreenX = screenX;
    marqueeBox.currentScreenY = screenY;
    marqueeBox.startWorld = transform.ScreenToWorld(screenX, screenY);
    marqueeBox.currentWorld = marqueeBox.startWorld;
    isDirty = true;
}

void CanvasEngine::OnBoxSelectMove(float screenX, float screenY) {
    if (!marqueeBox.isActive) return;
    marqueeBox.currentScreenX = screenX;
    marqueeBox.currentScreenY = screenY;
    marqueeBox.currentWorld = transform.ScreenToWorld(screenX, screenY);
    isDirty = true;
}

void CanvasEngine::OnBoxSelectUp(DocumentSession* session) {
    if (!marqueeBox.isActive) return;
    marqueeBox.isActive = false;
    isDirty = true;
    if (session) {
        auto activePage = session->GetActivePage();
        if (activePage) {
            double minX = std::min(marqueeBox.startWorld.x, marqueeBox.currentWorld.x);
            double maxX = std::max(marqueeBox.startWorld.x, marqueeBox.currentWorld.x);
            double minY = std::min(marqueeBox.startWorld.y, marqueeBox.currentWorld.y);
            double maxY = std::max(marqueeBox.startWorld.y, marqueeBox.currentWorld.y);

            if ((maxX - minX) > 2.0 || (maxY - minY) > 2.0) {
                AABB box(minX, minY, maxX, maxY);
                std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(box);
                for (uint32_t uid : candidateUids) {
                    auto obj = activePage->FindObjectByUid(uid);
                    if (obj && obj->isVisible && obj->isSelectable && obj->bounds.Intersects(box)) {
                        double objArea = obj->bounds.Area();
                        if (objArea > 1e-4) {
                            double isectArea = box.IntersectionArea(obj->bounds);
                            double coverageRatio = isectArea / objArea;
                            if (coverageRatio >= 0.50) {
                                obj->isSelected = 1;
                            }
                        } else {
                            if (box.Contains((obj->bounds.minX + obj->bounds.maxX) * 0.5,
                                             (obj->bounds.minY + obj->bounds.maxY) * 0.5)) {
                                obj->isSelected = 1;
                            }
                        }
                    }
                }
                selectionGizmo.SetSelectedObjects(activePage->objects);
                needsFullRebake = true;
            }
        }
    }
}

void CanvasEngine::ClearSelection(DocumentSession* session) {
    if (session) {
        auto activePage = session->GetActivePage();
        if (activePage) {
            for (auto& obj : activePage->objects) {
                if (obj) obj->isSelected = 0;
            }
        }
    }
    selectionGizmo.ClearSelection();
    needsFullRebake = true;
    isDirty = true;
}

void CanvasEngine::OnActivePageChanged() {
    selectionGizmo.ClearSelection();
    layerCompositor.GetEmbeddedAppLayer().DismountAll();
    layerCompositor.GetLiveInteractionLayer().Clear();
    layerCompositor.InvalidateBakedCanvas();
    isDirty = true;
}

bool CanvasEngine::DeleteSelectedObjects(DocumentSession* session) {
    if (!session) return false;
    size_t deletedCount = session->DeleteSelection();
    if (deletedCount > 0) {
        selectionGizmo.ClearSelection();
        needsFullRebake = true;
        isDirty = true;
        layerCompositor.InvalidateBakedCanvas();
        return true;
    }
    return false;
}

size_t CanvasEngine::SelectAll(DocumentSession* session) {
    if (!session) return 0;
    size_t count = session->SelectAll();
    if (count > 0) {
        selectionGizmo.SetSelectedObjects(session->GetSelectedObjects());
        isDirty = true;
    }
    return count;
}

void CanvasEngine::SyncSelectionToSpatialIndex(DocumentSession* session) {
    if (!session) return;
    auto activePage = session->GetActivePage();
    if (!activePage) return;
    for (const auto& obj : selectionGizmo.selectedObjects) {
        if (obj) {
            activePage->UpdateObject(obj);
        }
    }
}
