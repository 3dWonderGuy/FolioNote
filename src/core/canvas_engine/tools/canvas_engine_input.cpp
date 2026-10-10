#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/usage_tracker.hpp"
#include <chrono>
#include <cmath>

void CanvasEngine::AddEphemeralStroke(BLPath path, BLRgba32 color, uint32_t durationMs) {
    layerCompositor.GetLiveInteractionLayer().GetInkEngine().AddEphemeralStroke(std::move(path), color, durationMs);
    isDirty = true;
}

void CanvasEngine::ClearEphemeralStrokes() {
    layerCompositor.GetLiveInteractionLayer().GetInkEngine().ClearEphemeralStrokes();
    isDirty = true;
}

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
        double dx = worldMm.x - lastInkingWorldMm.x;
        double dy = worldMm.y - lastInkingWorldMm.y;
        double distMm = std::sqrt(dx * dx + dy * dy);
        ::Folio::UsageTracker::Instance().RecordInkingDistance(distMm);
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

    if (!data.outlinePath.is_empty() || !data.liveSegments.empty()) {
        if (tool.penType == PenType::LaserPointer) {
            if (session.HasEphemeralStrokeSink()) {
                session.CommitEphemeralStroke(std::move(data), tool, 2500);
            } else {
                AddEphemeralStroke(std::move(data.outlinePath), tool.color, 2500);
            }
        } else {
            session.CommitStroke(std::move(data), tool);
            ::Folio::UsageTracker::Instance().RecordStrokeCommitted();
            ::Folio::UsageTracker::Instance().RecordObjectCreated();
        }
    }

    InvalidateLayer();
}
