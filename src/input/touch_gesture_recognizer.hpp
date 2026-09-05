#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "input/input_tracker.hpp"

enum class TouchGestureType : uint8_t {
    None,
    Tap,
    PressAndHold,
    SingleFingerScroll,
    GizmoTransform,
    TwoFingerPinchPan
};

struct TouchGestureEvent {
    TouchGestureType type = TouchGestureType::None;
    float focusX = 0.0f;
    float focusY = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    float zoomDelta = 1.0f;
};

class TouchGestureRecognizer {
private:
    float startX = 0.0f;
    float startY = 0.0f;
    float prevX  = 0.0f;
    float prevY  = 0.0f;
    uint64_t touchStartTimeMs = 0;
    
    bool isScrolling  = false;
    bool isHolding    = false;
    bool isPinching   = false;
    float prevPinchDist = 0.0f;
    float prevMidX    = 0.0f;
    float prevMidY    = 0.0f;

    static constexpr float DRAG_THRESHOLD_PX = 8.0f;
    static constexpr uint64_t HOLD_TIME_MS   = 450;

public:
    TouchGestureEvent Evaluate(const std::array<TouchSlot, 10>& fingers, size_t activeCount, uint64_t nowMs, bool isInteractingWithGizmo) {
        TouchGestureEvent gesture;

        // -------------------------------------------------------------
        // TOUCH UP / RELEASE (0 Active Fingers)
        // -------------------------------------------------------------
        if (activeCount == 0) {
            // If the finger was lifted quickly (under HOLD_TIME_MS), and the user didn't 
            // scroll, hold, or pinch, we classify this quick interaction as a Tap.
            // This is why a Tap event is only fired *after* the finger leaves the screen.
            if (!isScrolling && !isHolding && !isPinching && (nowMs - touchStartTimeMs < HOLD_TIME_MS) && touchStartTimeMs > 0) {
                gesture.type = TouchGestureType::Tap;
                gesture.focusX = startX;
                gesture.focusY = startY;
            }

            // Reset all internal state flags so the next touch event starts fresh
            isScrolling = false;
            isHolding = false;
            isPinching = false;
            prevPinchDist = 0.0f;
            touchStartTimeMs = 0;
            return gesture;
        }

        // -------------------------------------------------------------
        // 1 FINGER: Scroll, Gizmo Manipulate, Tap, or Press & Hold
        // -------------------------------------------------------------
        if (activeCount == 1) {
            size_t idx = 0;
            while (idx < fingers.size() && fingers[idx].fingerID == -1) idx++;
            if (idx >= fingers.size()) return gesture;

            const auto& pt = fingers[idx].point;

            // Initial Touch Down: Record the exact time and coordinates where the finger landed.
            // This baseline is used later to calculate how long the finger has been held, 
            // and how far it has moved from its original landing spot.
            if (touchStartTimeMs == 0) {
                touchStartTimeMs = nowMs;
                startX = pt.x;
                startY = pt.y;
                prevX  = pt.x;
                prevY  = pt.y;
                return gesture;
            }

            // Calculate how far the finger has drifted from its initial landing spot (Euclidean distance)
            float totalDist = std::hypot(pt.x - startX, pt.y - startY);
            
            // Calculate the delta movement since the *last* frame
            float dx = pt.x - prevX;
            float dy = pt.y - prevY;
            prevX = pt.x;
            prevY = pt.y;

            // 1. Gizmo manipulation (resizing, adding space)
            // If the UI reports we hit a gizmo, route all movement directly to it.
            if (isInteractingWithGizmo) {
                gesture.type = TouchGestureType::GizmoTransform;
                gesture.focusX = pt.x;
                gesture.focusY = pt.y;
                gesture.deltaX = dx;
                gesture.deltaY = dy;
                return gesture;
            }

            // 2. Press and Hold Trigger
            // If the finger hasn't moved beyond a small "slop" area (DRAG_THRESHOLD_PX), 
            // and it hasn't turned into a scroll yet...
            if (!isScrolling && totalDist < DRAG_THRESHOLD_PX) {
                // ...we check if enough time has passed (HOLD_TIME_MS).
                // If it has, the user successfully executed a "Press and Hold".
                if ((nowMs - touchStartTimeMs >= HOLD_TIME_MS) && !isHolding) {
                    isHolding = true; // Prevents firing the hold event repeatedly
                    gesture.type = TouchGestureType::PressAndHold;
                    gesture.focusX = pt.x;
                    gesture.focusY = pt.y;
                    return gesture;
                }
            }

            // 3. Scroll / Pan Trigger
            if (totalDist >= DRAG_THRESHOLD_PX || isScrolling) {
                isScrolling = true;
                gesture.type = TouchGestureType::SingleFingerScroll;
                gesture.focusX = pt.x;
                gesture.focusY = pt.y;
                gesture.deltaX = dx;
                gesture.deltaY = dy;
                return gesture;
            }

            return gesture;
        }

        // -------------------------------------------------------------
        // 2 FINGERS: Pinch-to-Zoom & Dual-Finger Pan
        // -------------------------------------------------------------
        if (activeCount == 2) {
            // Find the active slots holding our two fingers
            int idx1 = -1, idx2 = -1;
            for (size_t i = 0; i < fingers.size(); ++i) {
                if (fingers[i].fingerID != -1) {
                    if (idx1 == -1) idx1 = static_cast<int>(i);
                    else if (idx2 == -1) { idx2 = static_cast<int>(i); break; }
                }
            }

            if (idx1 != -1 && idx2 != -1) {
                const auto& p1 = fingers[idx1].point;
                const auto& p2 = fingers[idx2].point;

                // Calculate the geometric center (midpoint) between the two fingers
                float midX = (p1.x + p2.x) * 0.5f;
                float midY = (p1.y + p2.y) * 0.5f;
                
                // Calculate the absolute distance between the two fingers
                float dist = std::hypot(p2.x - p1.x, p2.y - p1.y);

                // Initial setup for the pinch gesture
                if (!isPinching) {
                    isPinching = true;
                    prevPinchDist = dist;
                    prevMidX = midX;
                    prevMidY = midY;
                } else {
                    // Emit a continuous Pinch/Pan gesture
                    gesture.type = TouchGestureType::TwoFingerPinchPan;
                    gesture.focusX = midX; // The focal point for zooming
                    gesture.focusY = midY;
                    
                    // How much the *center point* between the fingers has moved (for panning)
                    gesture.deltaX = midX - prevMidX;
                    gesture.deltaY = midY - prevMidY;
                    
                    // The ratio of the current distance to the previous distance (for zooming)
                    gesture.zoomDelta = (prevPinchDist > 0.001f) ? (dist / prevPinchDist) : 1.0f;

                    // Save state for the next frame
                    prevPinchDist = dist;
                    prevMidX = midX;
                    prevMidY = midY;
                }
            }
            return gesture;
        }

        return gesture;
    }
};