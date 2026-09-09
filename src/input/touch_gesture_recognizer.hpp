#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "input/input_tracker.hpp"

// The different types of gestures we can recognize from touch events on the screen.
enum class TouchGestureType : uint8_t {
    None,
    Tap,
    PressAndHold,
    SingleFingerScroll,
    GizmoTransform,
    TwoFingerPinchPan
};

// Payload sent out whenever a touch gesture is triggered or updated.
struct TouchGestureEvent {
    TouchGestureType type = TouchGestureType::None;
    float focusX = 0.0f;     // Where the gesture is happening (tap spot, or center point between fingers)
    float focusY = 0.0f;     // Focal point Y coordinate
    float deltaX = 0.0f;     // How far we moved horizontally (for panning/scrolling)
    float deltaY = 0.0f;     // How far we moved vertically
    float zoomDelta = 1.0f;  // Scale factor for pinch-to-zoom (1.0 = no change, >1 = zoom in, <1 = zoom out)
};

// Translates raw finger contacts on glass into clean UI gestures like tapping, panning, holding, and pinch-to-zoom.
class TouchGestureRecognizer {
private:
    // Tracking variables for single-finger touches
    float startX = 0.0f;
    float startY = 0.0f;
    float prevX  = 0.0f;
    float prevY  = 0.0f;
    uint64_t touchStartTimeMs = 0; // we need to tell if it is tap or hold
    
    // Status flags to track what the user is currently up to
    bool isScrolling  = false;
    bool isHolding    = false;
    bool isPinching   = false;

    // Tracking variables for two-finger gestures (pinch & pan)
    float prevPinchDist = 0.0f;
    float prevMidX    = 0.0f;
    float prevMidY    = 0.0f;

    // Remembers how many fingers were on screen during the previous frame.
    // This helps us smoothly handle transitions—like when a user lifts one finger after pinch-zooming.
    size_t lastActiveCount = 0;

    // Small wiggle threshold in pixels (slop zone).
    // Humans can't hold a finger 100% still on glass, so we ignore movements smaller than this 
    // before declaring that the user has started scrolling or dragging.
    static constexpr float DRAG_THRESHOLD_PX = 8.0f;

    // How long (in milliseconds) a finger has to rest in place before it counts as a "Press and Hold".
    static constexpr uint64_t HOLD_TIME_MS   = 450;

public:
    TouchGestureEvent Evaluate(const std::array<TouchSlot, 10>& fingers, size_t activeCount, uint64_t nowMs, bool isInteractingWithGizmo) {
        TouchGestureEvent gesture;

        // ---------------------------------------------------------------------
        // 0 FINGERS: ALL TOUCHES RELEASED
        // ---------------------------------------------------------------------
        // When all fingers leave the screen, we check if the user just performed a quick "Tap".
        // We only confirm a Tap when the finger is lifted, because while the finger is down 
        // we can't tell yet if they're about to drag, hold, or place a second finger down!
        if (activeCount == 0) {
            
            if (!isScrolling && !isHolding && !isPinching && (nowMs - touchStartTimeMs < HOLD_TIME_MS) && touchStartTimeMs > 0) {
                gesture.type = TouchGestureType::Tap;
                gesture.focusX = startX;
                gesture.focusY = startY;
            }

            // Wipe clean all internal tracking state so the next touch sequence starts fresh
            isScrolling      = false;
            isHolding        = false;
            isPinching       = false;
            prevPinchDist    = 0.0f;
            prevMidX         = 0.0f;
            prevMidY         = 0.0f;
            touchStartTimeMs = 0;
            lastActiveCount  = 0;
            return gesture;
        }

        // ---------------------------------------------------------------------
        // 1 FINGER: SCROLLING, GIZMO DRAGGING, TAP PREPARATION, OR PRESS & HOLD
        // ---------------------------------------------------------------------
        if (activeCount == 1) {
            // Find which slot holds our active finger
            size_t idx = 0;
            while (idx < fingers.size() && fingers[idx].fingerID == -1) idx++;
            if (idx >= fingers.size()) {
                lastActiveCount = 0;
                return gesture;
            }

            const auto& pt = fingers[idx].point;

            // Handle finger transition: If we just came from 2+ fingers down to 1 finger (e.g., user lifted one finger),
            // we re-anchor our position baseline. Otherwise `prevX` and `prevY` would be stale from before the 2-finger gesture,
            // causing a sudden coordinate jump on screen!
            if (lastActiveCount > 1 || touchStartTimeMs == 0) {
                touchStartTimeMs = nowMs;
                startX           = pt.x;
                startY           = pt.y;
                prevX            = pt.x;
                prevY            = pt.y;
                isPinching       = false;
                isHolding        = false;
                lastActiveCount  = 1;
                return gesture;
            }

            lastActiveCount = 1;

            // Total distance moved from where the finger first touched down
            float totalDist = std::hypot(pt.x - startX, pt.y - startY);
            
            // Movement delta since the very last frame
            float dx = pt.x - prevX;
            float dy = pt.y - prevY;
            prevX = pt.x;
            prevY = pt.y;

            // Option 1: Gizmo Dragging
            // If the user is grabbing an on-screen widget handle (like a selection box corner),
            // route the movement directly to the gizmo transformer.
            if (isInteractingWithGizmo) {
                gesture.type   = TouchGestureType::GizmoTransform;
                gesture.focusX = pt.x;
                gesture.focusY = pt.y;
                gesture.deltaX = dx;
                gesture.deltaY = dy;
                return gesture;
            }

            // Option 2: Press and Hold Trigger
            // If the finger stayed put inside the small drag slop zone and hasn't started scrolling...
            if (!isScrolling && totalDist < DRAG_THRESHOLD_PX) {
                // ...and enough time has passed without moving, trigger Press & Hold!
                if ((nowMs - touchStartTimeMs >= HOLD_TIME_MS) && !isHolding) {
                    isHolding = true; // Prevents firing the hold event repeatedly every frame
                    gesture.type   = TouchGestureType::PressAndHold;
                    gesture.focusX = pt.x;
                    gesture.focusY = pt.y;
                    return gesture;
                }
            }

            // Option 3: Single Finger Scroll / Pan
            // Once the finger moves past the slop threshold (or was already scrolling), kick off canvas scrolling.
            if (totalDist >= DRAG_THRESHOLD_PX || isScrolling) {
                isScrolling    = true;
                gesture.type   = TouchGestureType::SingleFingerScroll;
                gesture.focusX = pt.x;
                gesture.focusY = pt.y;
                gesture.deltaX = dx;
                gesture.deltaY = dy;
                return gesture;
            }

            return gesture;
        }

        // ---------------------------------------------------------------------
        // 2 FINGERS: PINCH-TO-ZOOM & TWO-FINGER PANNING
        // ---------------------------------------------------------------------
        if (activeCount == 2) {
            // Locate the two active finger slots
            int idx1 = -1, idx2 = -1;
            for (size_t i = 0; i < fingers.size(); ++i) {
                if (fingers[i].fingerID != -1) {
                    if (idx1 == -1)      idx1 = static_cast<int>(i);
                    else if (idx2 == -1) { idx2 = static_cast<int>(i); break; }
                }
            }

            if (idx1 != -1 && idx2 != -1) {
                const auto& p1 = fingers[idx1].point;
                const auto& p2 = fingers[idx2].point;

                // Midpoint between both fingers (acts as the zoom focus point and pan reference)
                float midX = (p1.x + p2.x) * 0.5f;
                float midY = (p1.y + p2.y) * 0.5f;
                
                // Straight-line distance between the two fingers
                float dist = std::hypot(p2.x - p1.x, p2.y - p1.y);

                // Initial setup frame for 2-finger gesture:
                // Record initial distance & midpoint without emitting a gesture event yet.
                // This prevents a sudden jump on frame 1 when the 2nd finger touches down.
                if (!isPinching || lastActiveCount != 2) {
                    isPinching    = true;
                    prevPinchDist = dist;
                    prevMidX      = midX;
                    prevMidY      = midY;
                } else {
                    // Continuous 2-finger pan & zoom updates
                    gesture.type   = TouchGestureType::TwoFingerPinchPan;
                    gesture.focusX = midX;
                    gesture.focusY = midY;
                    
                    // How far the center point between fingers moved (for canvas panning)
                    gesture.deltaX = midX - prevMidX;
                    gesture.deltaY = midY - prevMidY;
                    
                    // Ratio of current finger distance vs previous finger distance (for zooming)
                    gesture.zoomDelta = (prevPinchDist > 0.001f) ? (dist / prevPinchDist) : 1.0f;

                    // Save values for next frame's comparison
                    prevPinchDist = dist;
                    prevMidX      = midX;
                    prevMidY      = midY;
                }
            }

            lastActiveCount = 2;
            return gesture;
        }

        // 3+ fingers (multi-touch shortcuts or palm rejection fallthrough)
        lastActiveCount = activeCount;
        return gesture;
    }
};