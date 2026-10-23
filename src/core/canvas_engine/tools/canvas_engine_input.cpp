#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include <cmath>

void CanvasEngine::OnPointerDown(float screenX, float screenY, float pressure, double timeSec, const PenTool& tool, float tiltX, float tiltY) {
    Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
    if (ruler.IsEnabled()) {
        auto snap = ruler.SnapPoint(worldMm);
        if (snap.isSnapped) {
            worldMm = snap.point;
            ruler.SetActiveInkingState(true, 0.0);
        }
    }
    lastInkingWorldMm = worldMm;
    isCurrentlyInking = true;

    auto& liveInteraction = layerCompositor.GetLiveInteractionLayer();
    liveInteraction.SetPenStyle(tool.color.value, tool.widthMm > 0.0f ? tool.widthMm : tool.baseSize);
    liveInteraction.BeginStroke(worldMm.x, worldMm.y, pressure, timeSec, tool,
                               static_cast<float>(transform.GetEffectiveScale()), tiltX, tiltY);

    isDirty = true;
}

void CanvasEngine::OnPointerMove(float screenX, float screenY, float pressure, double timeSec, float tiltX, float tiltY) {
    Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
    if (ruler.IsEnabled()) {
        auto snap = ruler.SnapPoint(worldMm);
        if (snap.isSnapped) {
            worldMm = snap.point;
            double drawnLen = std::hypot(worldMm.x - lastInkingWorldMm.x, worldMm.y - lastInkingWorldMm.y);
            ruler.SetActiveInkingState(true, drawnLen);
        }
    }
    if (isCurrentlyInking) {
        lastInkingWorldMm = worldMm;
    }

    layerCompositor.GetLiveInteractionLayer().AddStrokePoint(
        worldMm.x, worldMm.y, pressure, timeSec,
        static_cast<float>(transform.GetEffectiveScale()), tiltX, tiltY);

    isDirty = true;
}

void CanvasEngine::OnPointerUp(DocumentSession& session, const PenTool& tool) {
    isCurrentlyInking = false;
    if (ruler.IsEnabled()) {
        ruler.SetActiveInkingState(false, 0.0);
    }
    FinishedStrokeData data = layerCompositor.GetLiveInteractionLayer().FinishStroke();

    isDirty = true;

    if (!data.modeledPoints.empty() || !data.liveSegments.empty()) {
        BLBox wBox;
        bool hasBox = false;
        if (!data.modeledPoints.empty()) {
            wBox.x0 = wBox.x1 = data.modeledPoints[0].x;
            wBox.y0 = wBox.y1 = data.modeledPoints[0].y;
            for (const auto& pt : data.modeledPoints) {
                wBox.x0 = (std::min)(wBox.x0, static_cast<double>(pt.x - pt.r));
                wBox.y0 = (std::min)(wBox.y0, static_cast<double>(pt.y - pt.r));
                wBox.x1 = (std::max)(wBox.x1, static_cast<double>(pt.x + pt.r));
                wBox.y1 = (std::max)(wBox.y1, static_cast<double>(pt.y + pt.r));
            }
            hasBox = true;
        }

        session.CommitStroke(std::move(data), tool);

        if (hasBox) {
            AABB strokeBox(wBox.x0 - 2.0, wBox.y0 - 2.0, wBox.x1 + 2.0, wBox.y1 + 2.0);
            InvalidateLayerRect(strokeBox);
        } else {
            InvalidateLayer();
        }
    } else {
        InvalidateLayer();
    }
}
