#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "core/objects/ink_container/ink_container.hpp"
#include "core/objects/primitives/shape_container.hpp"
#include "utils/uid_generator.hpp"
#include "utils/usage_tracker.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>

void CanvasEngine::SetEraserCursor(float screenX, float screenY, double radiusMm, bool isDown, bool isStroke) {
    bool wasVisible = eraserVisual.isVisible;
    float oldX = eraserVisual.screenX;
    float oldY = eraserVisual.screenY;
    eraserVisual.isVisible = true;
    eraserVisual.screenX = screenX;
    eraserVisual.screenY = screenY;
    eraserVisual.radiusMm = radiusMm;
    eraserVisual.isDown = isDown;
    eraserVisual.isStrokeEraser = isStroke;
    if (!wasVisible || std::abs(oldX - screenX) > 0.5f || std::abs(oldY - screenY) > 0.5f) {
        isDirty = true;
    }
}

void CanvasEngine::HideEraserCursor() {
    if (eraserVisual.isVisible) {
        eraserVisual.isVisible = false;
        isDirty = true;
    }
}

bool CanvasEngine::EraseSegment(float screenX0, float screenY0, float screenX1, float screenY1,
                               double radiusMm, DocumentSession& session, bool isStrokeEraser) {
    auto activePage = session.GetActivePage();
    if (!activePage) return false;

    Point2D w0 = transform.ScreenToWorld(screenX0, screenY0);
    Point2D w1 = transform.ScreenToWorld(screenX1, screenY1);
    double r = std::max(0.5, radiusMm);

    AABB sweptBox(
        std::min(w0.x, w1.x) - r,
        std::min(w0.y, w1.y) - r,
        std::max(w0.x, w1.x) + r,
        std::max(w0.y, w1.y) + r
    );

    if (devMode) {
        debugCollision.active = true;
        debugCollision.queryCenter = w1;
        debugCollision.queryRadius = r;
        debugCollision.queryBox = sweptBox;
        debugCollision.candidateUids.clear();
        debugCollision.hitUids.clear();
    }

    std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(sweptBox);
    if (candidateUids.empty()) {
        if (devMode) {
            isDirty = true;
        }
        return false;
    }

    if (devMode) {
        debugCollision.candidateUids = candidateUids;
    }

    bool modified = false;
    for (uint32_t uid : candidateUids) {
        auto obj = activePage->FindObjectByUid(uid);
        if (!obj) continue;

        if (obj->isLocked) {
            continue;
        }
        if (obj->type != ObjectType::InkContainer && obj->type != ObjectType::Shape) {
            continue;
        }

        if (isStrokeEraser) {
            bool hit = false;
            if (obj->type == ObjectType::InkContainer) {
                hit = std::static_pointer_cast<InkContainer>(obj)->HitTestSwept(w0, w1, r);
            } else if (obj->type == ObjectType::Shape) {
                hit = std::static_pointer_cast<Folio::ShapeObject>(obj)->HitTestSwept(w0, w1, r);
            }
            if (hit) {
                if (devMode) debugCollision.hitUids.push_back(obj->uid);
                session.RecordErasedObject(obj);
                activePage->RemoveObject(obj);
                modified = true;
            }
        } else {
            if (obj->type == ObjectType::InkContainer) {
                auto ink = std::static_pointer_cast<InkContainer>(obj);
                double segLen = std::hypot(w1.x - w0.x, w1.y - w0.y);
                int steps = std::clamp(static_cast<int>(std::ceil(segLen / (r * 0.6))), 1, 30);

                auto originalClone = std::shared_ptr<InkContainer>(static_cast<InkContainer*>(ink->Clone().release()));

                bool inkModified = false;
                std::vector<std::shared_ptr<InkContainer>> newFragments;

                for (int s = (steps > 1 ? 0 : 1); s <= steps; ++s) {
                    double t = (steps == 1) ? 1.0 : (static_cast<double>(s) / steps);
                    double curX = w0.x + t * (w1.x - w0.x);
                    double curY = w0.y + t * (w1.y - w0.y);

                    std::vector<std::shared_ptr<InkContainer>> stepFrags;
                    if (ink->SliceStrokeAt(curX, curY, r, stepFrags)) {
                        inkModified = true;
                        for (auto& frag : stepFrags) {
                            newFragments.push_back(std::move(frag));
                        }
                    }
                }

                if (inkModified) {
                    if (devMode) debugCollision.hitUids.push_back(obj->uid);
                    std::vector<std::shared_ptr<InkContainer>> survivingFragments;
                    if (ink->strokes.empty()) {
                        activePage->RemoveObject(ink);
                    } else {
                        activePage->UpdateObject(ink);
                        survivingFragments.push_back(ink);
                    }
                    for (auto& frag : newFragments) {
                        frag->uid = UIDGenerator::Next();
                        activePage->AddObject(frag);
                        survivingFragments.push_back(frag);
                    }
                    session.RecordSlicedStroke(originalClone, survivingFragments);
                    modified = true;
                }
            } else {
                bool hit = false;
                if (obj->type == ObjectType::Shape) {
                    hit = std::static_pointer_cast<Folio::ShapeObject>(obj)->HitTestSwept(w0, w1, r);
                }
                if (hit) {
                    if (devMode) debugCollision.hitUids.push_back(obj->uid);
                    session.RecordErasedObject(obj);
                    activePage->RemoveObject(obj);
                    modified = true;
                }
            }
        }
    }

    if (modified || devMode) {
        isDirty = true;
        if (modified) {
            needsFullRebake = true;
            layerCompositor.InvalidateBakedCanvas();
            ::Folio::UsageTracker::Instance().RecordEraserAction();
            LOG_INFO(CanvasEngine, "Erased content on page (strokeEraser=" + std::string(isStrokeEraser ? "true" : "false") + ")");
        }
    }
    return modified;
}

bool CanvasEngine::EraseAt(float screenX, float screenY, double radiusMm, DocumentSession& session, bool isStrokeEraser) {
    return EraseSegment(screenX, screenY, screenX, screenY, radiusMm, session, isStrokeEraser);
}
