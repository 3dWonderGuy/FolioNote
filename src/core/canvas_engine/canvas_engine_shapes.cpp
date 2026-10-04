#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "utils/guid_generator.hpp"
#include "utils/uid_generator.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>

Point2D CanvasEngine::SnapToGridIfNeeded(const Point2D& pt) const {
    if (!shapeCreation.lockToGrid || gridSpacingMm <= 0.001) return pt;
    return Point2D(
        std::round(pt.x / gridSpacingMm) * gridSpacingMm,
        std::round(pt.y / gridSpacingMm) * gridSpacingMm
    );
}

void CanvasEngine::StartShapeCreation(Folio::ShapeType type, bool lockMode, DocumentSession* session) {
    if (session) {
        ClearSelection(session);
    }
    selectionGizmo.ClearSelection();
    shapeCreation.isActive = true;
    shapeCreation.isDragging = false;
    shapeCreation.ellipseStep = 0;
    shapeCreation.shapeType = type;
    shapeCreation.lockDrawingMode = lockMode;
    shapeCreation.isSnapped = false;
    isDirty = true;
    LOG_INFO(CanvasEngine, "Started shape creation mode for type=" + std::to_string(static_cast<int>(type)) +
             (lockMode ? " [LOCKED]" : " [ONE-SHOT]"));
}

void CanvasEngine::CancelShapeCreation() {
    shapeCreation.isActive = false;
    shapeCreation.isDragging = false;
    shapeCreation.ellipseStep = 0;
    shapeCreation.lockDrawingMode = false;
    shapeCreation.isSnapped = false;
    isDirty = true;
}

void CanvasEngine::OnShapeDrawDown(float screenX, float screenY, DocumentSession* session) {
    if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1) {
        shapeCreation.isDragging = true;
        return;
    }

    shapeCreation.isDragging = true;
    shapeCreation.startScreenX = screenX;
    shapeCreation.startScreenY = screenY;
    shapeCreation.currentScreenX = screenX;
    shapeCreation.currentScreenY = screenY;
    Point2D rawStart = transform.ScreenToWorld(screenX, screenY);
    shapeCreation.startWorld = SnapToGridIfNeeded(rawStart);

    if ((shapeCreation.shapeType == Folio::ShapeType::Line || shapeCreation.shapeType == Folio::ShapeType::LineArrow) && session) {
        if (auto pg = session->GetActivePage()) {
            Point2D snapAnchor;
            if (Folio::SmartArrowObject::FindSnapAnchor(shapeCreation.startWorld, pg->objects, snapAnchor, 6.0)) {
                shapeCreation.startWorld = snapAnchor;
            }
        }
    }

    shapeCreation.currentWorld = shapeCreation.startWorld;
    shapeCreation.isSnapped = false;
    isDirty = true;
}

void CanvasEngine::OnShapeDrawMove(float screenX, float screenY, DocumentSession* session) {
    shapeCreation.currentScreenX = screenX;
    shapeCreation.currentScreenY = screenY;
    Point2D rawWorld = transform.ScreenToWorld(screenX, screenY);
    shapeCreation.currentWorld = SnapToGridIfNeeded(rawWorld);

    if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1) {
        double d = std::hypot(shapeCreation.currentWorld.x - shapeCreation.ellipseCenter.x,
                              shapeCreation.currentWorld.y - shapeCreation.ellipseCenter.y);
        shapeCreation.ellipseMinorRadius = std::clamp(d, 1.0, std::max(2.0, shapeCreation.ellipseMajorRadius));
        isDirty = true;
        return;
    }

    if ((shapeCreation.shapeType == Folio::ShapeType::Line || shapeCreation.shapeType == Folio::ShapeType::LineArrow) && session) {
        if (auto pg = session->GetActivePage()) {
            Point2D snapAnchor;
            if (Folio::SmartArrowObject::FindSnapAnchor(shapeCreation.currentWorld, pg->objects, snapAnchor, 6.0)) {
                shapeCreation.snapAnchorPoint = snapAnchor;
                shapeCreation.currentWorld = snapAnchor;
                shapeCreation.isSnapped = true;
            } else {
                shapeCreation.isSnapped = false;
            }
        }
    } else {
        shapeCreation.isSnapped = false;
    }

    if (shapeCreation.isDragging) {
        isDirty = true;
    }
}

std::shared_ptr<CanvasObject> CanvasEngine::OnShapeDrawUp(DocumentSession* session) {
    if (!shapeCreation.isDragging && shapeCreation.ellipseStep == 0) return nullptr;

    if (!session) return nullptr;
    auto activePage = session->GetActivePage();
    if (!activePage) return nullptr;

    if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1) {
        double rx = shapeCreation.ellipseMajorRadius;
        double ry = shapeCreation.ellipseMinorRadius;
        if (rx < 1.0) rx = 10.0;
        if (ry < 1.0) ry = 10.0;

        double cx = shapeCreation.ellipseCenter.x;
        double cy = shapeCreation.ellipseCenter.y;
        double minX = cx - rx;
        double minY = cy - ry;
        double w = rx * 2.0;
        double h = ry * 2.0;

        auto shp = std::make_shared<Folio::ShapeObject>(Folio::ShapeType::Ellipse, minX, minY, w, h);
        shp->guuid = GUIDGenerator::GenerateV4();
        shp->uid = UIDGenerator::Next();
        shp->fillType = shapeCreation.defaultFillType;
        shp->fillColor = shapeCreation.defaultFillColor;
        shp->outlineType = shapeCreation.defaultOutlineType;
        shp->strokeColor = shapeCreation.defaultOutlineColor;
        shp->strokeWidth = shapeCreation.defaultStrokeWidth;
        shp->UpdateBounds();

        activePage->AddObject(shp, true);
        if (session) {
            session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(shp));
        }
        ClearSelection(session);
        shp->isSelected = 1;
        selectionGizmo.SetSelectedObjects(activePage->objects);

        shapeCreation.ellipseStep = 0;
        shapeCreation.isDragging = false;
        shapeCreation.isSnapped = false;
        if (!shapeCreation.lockDrawingMode) {
            shapeCreation.isActive = false;
        }
        needsFullRebake = true;
        isDirty = true;
        layerCompositor.InvalidateBakedCanvas();
        return shp;
    }

    shapeCreation.isDragging = false;

    double dragDist = std::hypot(shapeCreation.currentWorld.x - shapeCreation.startWorld.x,
                                 shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
    if (dragDist < 2.5) {
        LOG_INFO(CanvasEngine, "Shape drag cancelled: drag distance below 2.5mm threshold - clearing selection");
        shapeCreation.isDragging = false;
        shapeCreation.ellipseStep = 0;
        shapeCreation.isSnapped = false;
        ClearSelection(session);
        selectionGizmo.ClearSelection();
        needsFullRebake = true;
        isDirty = true;
        return nullptr;
    }

    if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 0) {
        shapeCreation.ellipseStep = 1;
        shapeCreation.ellipseCenter = shapeCreation.startWorld;
        shapeCreation.ellipseMajorPoint = shapeCreation.currentWorld;
        shapeCreation.ellipseMajorRadius = dragDist;
        shapeCreation.ellipseMinorRadius = dragDist;
        isDirty = true;
        LOG_INFO(CanvasEngine, "Ellipse Step 1: Major radius set to " + std::to_string(dragDist) + "mm. Move mouse to adjust thickness, click to place.");
        return nullptr;
    }

    if (shapeCreation.shapeType == Folio::ShapeType::Line ||
        shapeCreation.shapeType == Folio::ShapeType::LineArrow) {
        auto arrow = std::make_shared<Folio::SmartArrowObject>(
            shapeCreation.startWorld.x, shapeCreation.startWorld.y,
            shapeCreation.currentWorld.x, shapeCreation.currentWorld.y
        );
        arrow->guuid = GUIDGenerator::GenerateV4();
        arrow->uid = UIDGenerator::Next();
        arrow->strokeColor = shapeCreation.defaultOutlineColor;
        arrow->strokeWidth = shapeCreation.defaultStrokeWidth;
        arrow->outlineType = shapeCreation.defaultOutlineType;
        arrow->connectorStyle = shapeCreation.defaultConnectorStyle;
        arrow->startArrow = Folio::ArrowHeadType::None;
        arrow->endArrow = (shapeCreation.shapeType == Folio::ShapeType::LineArrow)
                          ? shapeCreation.defaultEndArrow
                          : Folio::ArrowHeadType::None;
        arrow->arrowHeadSize = 4.0;
        arrow->UpdateBounds();

        activePage->AddObject(arrow, true);
        if (session) {
            session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(arrow));
        }

        ClearSelection(session);
        arrow->isSelected = 1;
        selectionGizmo.SetSelectedObjects(activePage->objects);

        shapeCreation.isDragging = false;
        shapeCreation.isSnapped = false;
        if (!shapeCreation.lockDrawingMode) {
            shapeCreation.isActive = false;
        }
        needsFullRebake = true;
        isDirty = true;
        layerCompositor.InvalidateBakedCanvas();

        LOG_INFO(CanvasEngine, "Created SmartArrowObject connector (uid=" + std::to_string(arrow->uid) +
                 ", style=" + std::to_string(static_cast<int>(arrow->connectorStyle)) +
                 ", from [" + std::to_string(arrow->x1) + "," + std::to_string(arrow->y1) + "] to [" +
                 std::to_string(arrow->x2) + "," + std::to_string(arrow->y2) + "])");
        return arrow;
    }

    double minX = 0.0, minY = 0.0, w = 0.0, h = 0.0;

    if (shapeCreation.shapeType == Folio::ShapeType::Circle) {
        double r = dragDist;
        w = r * 2.0;
        h = r * 2.0;
        minX = shapeCreation.startWorld.x - r;
        minY = shapeCreation.startWorld.y - r;
    }
    else if (shapeCreation.shapeType == Folio::ShapeType::Hexagon ||
             shapeCreation.shapeType == Folio::ShapeType::RegularPolygon) {
        double r = dragDist;
        w = r * 2.0;
        h = r * 2.0;
        minX = shapeCreation.startWorld.x - r;
        minY = shapeCreation.startWorld.y - r;
    }
    else if (shapeCreation.shapeType == Folio::ShapeType::SineWave ||
             shapeCreation.shapeType == Folio::ShapeType::SquareWave ||
             shapeCreation.shapeType == Folio::ShapeType::TriangleWave ||
             shapeCreation.shapeType == Folio::ShapeType::RightTriangleWave) {
        minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
        w = std::max(0.5, std::abs(shapeCreation.currentWorld.x - shapeCreation.startWorld.x));
        double rawH = std::abs(shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
        if (rawH < 4.0) {
            h = 20.0;
            minY = shapeCreation.startWorld.y - 10.0;
        } else {
            minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
            h = std::max(0.5, rawH);
        }
    }
    else {
        minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
        minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
        w = std::abs(shapeCreation.currentWorld.x - shapeCreation.startWorld.x);
        h = std::abs(shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
    }

    auto shp = std::make_shared<Folio::ShapeObject>(shapeCreation.shapeType, minX, minY, w, h);
    shp->guuid = GUIDGenerator::GenerateV4();
    shp->uid = UIDGenerator::Next();
    shp->fillType = shapeCreation.defaultFillType;
    shp->fillColor = shapeCreation.defaultFillColor;
    shp->outlineType = shapeCreation.defaultOutlineType;
    shp->strokeColor = shapeCreation.defaultOutlineColor;
    shp->strokeWidth = shapeCreation.defaultStrokeWidth;
    if (shapeCreation.shapeType == Folio::ShapeType::Hexagon ||
        shapeCreation.shapeType == Folio::ShapeType::RegularPolygon) {
        shp->param1 = static_cast<double>(shapeCreation.polygonSides);
    } else if (shapeCreation.shapeType == Folio::ShapeType::SineWave ||
               shapeCreation.shapeType == Folio::ShapeType::SquareWave ||
               shapeCreation.shapeType == Folio::ShapeType::TriangleWave ||
               shapeCreation.shapeType == Folio::ShapeType::RightTriangleWave) {
        shp->param1 = 3.0;
        shp->param2 = 0.0;
    }
    shp->UpdateBounds();

    activePage->AddObject(shp, true);
    if (session) {
        session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(shp));
    }

    ClearSelection(session);
    shp->isSelected = 1;
    selectionGizmo.SetSelectedObjects(activePage->objects);

    needsFullRebake = true;
    isDirty = true;
    layerCompositor.InvalidateBakedCanvas();

    LOG_INFO(CanvasEngine, "Created vector shape by drag (type=" + std::to_string(static_cast<int>(shapeCreation.shapeType)) +
             ", uid=" + std::to_string(shp->uid) + ", bounds=[" + std::to_string(minX) + "," + std::to_string(minY) + " " + std::to_string(w) + "x" + std::to_string(h) + "])");

    if (!shapeCreation.lockDrawingMode) {
        shapeCreation.isActive = false;
    }

    return shp;
}

std::shared_ptr<Folio::ShapeObject> CanvasEngine::GetSelectedShape(DocumentSession* session) const {
    if (!session) return nullptr;
    auto activePage = session->GetActivePage();
    if (!activePage) return nullptr;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isSelected && obj->type == ObjectType::Shape) {
            return std::dynamic_pointer_cast<Folio::ShapeObject>(obj);
        }
    }
    return nullptr;
}

std::shared_ptr<Folio::SmartArrowObject> CanvasEngine::GetSelectedConnector(DocumentSession* session) const {
    if (!session) return nullptr;
    auto activePage = session->GetActivePage();
    if (!activePage) return nullptr;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->isSelected && obj->type == ObjectType::Connector) {
            return std::dynamic_pointer_cast<Folio::SmartArrowObject>(obj);
        }
    }
    return nullptr;
}

std::shared_ptr<Folio::TextBoxObject> CanvasEngine::GetSelectedTextBox(DocumentSession* session) const {
    if (!session) return nullptr;
    auto activePage = session->GetActivePage();
    if (!activePage) return nullptr;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->type == ObjectType::Text) {
            if (obj->isSelected || (textEditor.IsActive() && obj.get() == textEditor.GetTarget())) {
                return std::dynamic_pointer_cast<Folio::TextBoxObject>(obj);
            }
        }
    }
    return nullptr;
}

std::shared_ptr<Folio::TextBoxObject> CanvasEngine::InsertTextBox(DocumentSession* session, double worldX, double worldY) {
    if (!session) return nullptr;
    auto activePage = session->GetActivePage();
    if (!activePage) return nullptr;

    if (worldX == 0.0 && worldY == 0.0) {
        Point2D center = transform.ScreenToWorld(viewportW * 0.5f, viewportH * 0.5f);
        worldX = center.x - 35.0;
        worldY = center.y - 10.0;
    }

    auto box = std::make_shared<Folio::TextBoxObject>(worldX, worldY, 70.0, 20.0);
    session->AddTextBox(box);
    textEditor.Attach(box.get());
    needsFullRebake = true;
    isDirty = true;
    layerCompositor.InvalidateBakedCanvas();
    return box;
}

std::shared_ptr<Folio::ShapeObject> CanvasEngine::InsertShape(Folio::ShapeType type, DocumentSession* session, double worldX, double worldY) {
    if (!session) return nullptr;
    auto activePage = session->GetActivePage();
    if (!activePage) return nullptr;

    if (worldX == 0.0 && worldY == 0.0) {
        Point2D center = transform.ScreenToWorld(viewportW * 0.5f, viewportH * 0.5f);
        worldX = center.x - 30.0;
        worldY = center.y - 20.0;
    }

    auto shp = std::make_shared<Folio::ShapeObject>(type, worldX, worldY, 60.0, 40.0);
    shp->guuid = GUIDGenerator::GenerateV4();
    shp->uid = UIDGenerator::Next();
    shp->fillType = shapeCreation.defaultFillType;
    shp->fillColor = shapeCreation.defaultFillColor;
    shp->outlineType = shapeCreation.defaultOutlineType;
    shp->strokeColor = shapeCreation.defaultOutlineColor;
    shp->strokeWidth = shapeCreation.defaultStrokeWidth;
    shp->UpdateBounds();

    activePage->AddObject(shp, true);
    if (session) {
        session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(shp));
    }

    ClearSelection(session);
    shp->isSelected = 1;
    selectionGizmo.SetSelectedObjects(activePage->objects);

    needsFullRebake = true;
    isDirty = true;
    layerCompositor.InvalidateBakedCanvas();

    LOG_INFO(CanvasEngine, "Inserted vector shape (type=" + std::to_string(static_cast<int>(type)) + ", uid=" + std::to_string(shp->uid) + ")");
    return shp;
}
