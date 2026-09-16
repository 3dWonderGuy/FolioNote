/**
 * @file touch_gesture_recognizer.hpp
 * @brief Translates raw multi-touch contact telemetry into high-level UI gestures.
 *
 * GESTURE RECOGNITION ARCHITECTURE:
 * ---------------------------------
 * Glass touchscreens report discrete contact points across consecutive frames.
 * The recognizer classifies these contacts into distinct semantic gestures:
 *   1. Tap: Single finger contact released within HOLD_TIME_MS without moving beyond DRAG_THRESHOLD_PX.
 *   2. Press & Hold: Single finger dwelling in place >= HOLD_TIME_MS within DRAG_THRESHOLD_PX.
 *   3. Single Finger Scroll / Pan: Single finger moving beyond DRAG_THRESHOLD_PX.
 *   4. Two-Finger Pinch-Pan: Two fingers moving; centroid translation drives pan, Euclidean distance ratio drives zoom.
 *   5. Two-Finger Tap: Quick two-finger tap released within MULTI_TAP_MAX_MS (Digital ink shortcut for Undo).
 *   6. Three-Finger Tap: Quick three-finger tap released within MULTI_TAP_MAX_MS (Digital ink shortcut for Redo).
 *   7. Gizmo Transform: Route single-finger drag directly to selection handles when active.
 */

#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "input/input_tracker.hpp"

// The different types of gestures recognized from capacitive multi-touch contacts.
enum class TouchGestureType : uint8_t {
    None = 0,
    Tap,
    PressAndHold,
    SingleFingerScroll,
    GizmoTransform,
    TwoFingerPinchPan,
    TwoFingerTap,   ///< Quick 2-finger tap (Undo)
    ThreeFingerTap  ///< Quick 3-finger tap (Redo)
};

// Payload sent out whenever a touch gesture is triggered or updated.
struct TouchGestureEvent {
    TouchGestureType type = TouchGestureType::None;
    float focusX = 0.0f;     ///< Focal point X coordinate (tap spot or centroid between fingers)
    float focusY = 0.0f;     ///< Focal point Y coordinate
    float deltaX = 0.0f;     ///< Horizontal displacement delta (for panning/scrolling)
    float deltaY = 0.0f;     ///< Vertical displacement delta
    float zoomDelta = 1.0f;  ///< Scale factor for pinch-to-zoom (1.0 = no change, >1 = zoom in, <1 = zoom out)
};

// Translates raw finger contacts on glass into clean UI gestures.
class TouchGestureRecognizer {
private:
    // Tracking variables for single-finger touches
    float startX = 0.0f;
    float startY = 0.0f;
    float prevX  = 0.0f;
    float prevY  = 0.0f;
    uint64_t touchStartTimeMs = 0;
    
    // Status flags to track what the user is currently performing
    bool isScrolling  = false;
    bool isHolding    = false;
    bool isPinching   = false;

    // Tracking variables for two-finger gestures (pinch & pan)
    float prevPinchDist = 0.0f;
    float prevMidX      = 0.0f;
    float prevMidY      = 0.0f;
    uint64_t twoFingerStartTimeMs = 0;
    bool twoFingerMoved = false;

    // Tracking variables for three-finger gestures
    uint64_t threeFingerStartTimeMs = 0;

    // Remembers how many fingers were on screen during the previous frame
    size_t lastActiveCount = 0;

    // Spatial slop threshold in pixels
    static constexpr float DRAG_THRESHOLD_PX = 8.0f;

    // Minimum dwell duration in ms for Press and Hold
    static constexpr uint64_t HOLD_TIME_MS = 450;

    // Maximum duration in ms for multi-finger tap gestures (Undo/Redo)
    static constexpr uint64_t MULTI_TAP_MAX_MS = 250;

public:
    /**
     * @brief Evaluates the current state of active touch contacts and yields a gesture event.
     *
     * @param fingers                Array of tracked touch slots.
     * @param activeCount            Number of currently engaged touch slots (fingerID != -1).
     * @param nowMs                  Current timestamp in milliseconds.
     * @param isInteractingWithGizmo True if user is dragging an active selection gizmo handle.
     * @return TouchGestureEvent containing the resolved gesture type and transform deltas.
     */
    TouchGestureEvent Evaluate(const std::array<TouchSlot, 10>& fingers, size_t activeCount, uint64_t nowMs, bool isInteractingWithGizmo) {
        TouchGestureEvent gesture;

        // ---------------------------------------------------------------------
        // 0 FINGERS: ALL TOUCHES RELEASED
        // ---------------------------------------------------------------------
        // Evaluate completion of tap gestures (Single Tap, Two-Finger Tap, Three-Finger Tap)
        if (activeCount == 0) {
            // Case A: Two-Finger Tap (Undo)
            if (lastActiveCount == 2 && (nowMs - twoFingerStartTimeMs <= MULTI_TAP_MAX_MS) && !twoFingerMoved && twoFingerStartTimeMs > 0) {
                gesture.type   = TouchGestureType::TwoFingerTap;
                gesture.focusX = prevMidX;
                gesture.focusY = prevMidY;
            }
            // Case B: Three-Finger Tap (Redo)
            else if (lastActiveCount == 3 && (nowMs - threeFingerStartTimeMs <= MULTI_TAP_MAX_MS) && threeFingerStartTimeMs > 0) {
                gesture.type   = TouchGestureType::ThreeFingerTap;
                gesture.focusX = prevMidX;
                gesture.focusY = prevMidY;
            }
            // Case C: Single Finger Tap
            else if (!isScrolling && !isHolding && !isPinching && (nowMs - touchStartTimeMs < HOLD_TIME_MS) && touchStartTimeMs > 0) {
                gesture.type   = TouchGestureType::Tap;
                gesture.focusX = startX;
                gesture.focusY = startY;
            }

            // Wipe clean internal tracking state
            isScrolling            = false;
            isHolding              = false;
            isPinching             = false;
            prevPinchDist          = 0.0f;
            prevMidX               = 0.0f;
            prevMidY               = 0.0f;
            touchStartTimeMs       = 0;
            twoFingerStartTimeMs   = 0;
            threeFingerStartTimeMs = 0;
            twoFingerMoved         = false;
            lastActiveCount        = 0;
            return gesture;
        }

        // ---------------------------------------------------------------------
        // 1 FINGER: SCROLLING, GIZMO DRAGGING, TAP PREPARATION, OR PRESS & HOLD
        // ---------------------------------------------------------------------
        if (activeCount == 1) {
            size_t idx = 0;
            while (idx < fingers.size() && fingers[idx].fingerID == -1) idx++;
            if (idx >= fingers.size()) {
                lastActiveCount = 0;
                return gesture;
            }

            const auto& pt = fingers[idx].point;

            // Handle transition from multi-touch down to single touch
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

            // Total Euclidean distance moved from where the finger first touched down:
            //   D = sqrt((x - x0)^2 + (y - y0)^2)
            float totalDist = std::hypot(pt.x - startX, pt.y - startY);
            
            // Movement delta since the previous frame
            float dx = pt.x - prevX;
            float dy = pt.y - prevY;
            prevX = pt.x;
            prevY = pt.y;

            // Option 1: Gizmo Dragging
            if (isInteractingWithGizmo) {
                gesture.type   = TouchGestureType::GizmoTransform;
                gesture.focusX = pt.x;
                gesture.focusY = pt.y;
                gesture.deltaX = dx;
                gesture.deltaY = dy;
                return gesture;
            }

            // Option 2: Press and Hold Trigger
            // If contact remains stationary within DRAG_THRESHOLD_PX for >= HOLD_TIME_MS:
            if (!isScrolling && totalDist < DRAG_THRESHOLD_PX) {
                if ((nowMs - touchStartTimeMs >= HOLD_TIME_MS) && !isHolding) {
                    isHolding = true; // Prevents firing repeatedly every frame
                    gesture.type   = TouchGestureType::PressAndHold;
                    gesture.focusX = pt.x;
                    gesture.focusY = pt.y;
                    return gesture;
                }
            }

            // Option 3: Single Finger Scroll / Pan
            // Moving beyond the slop threshold initiates scrolling
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

                // Midpoint (centroid) between both fingers:
                //   M = ((p1.x + p2.x) / 2, (p1.y + p2.y) / 2)
                float midX = (p1.x + p2.x) * 0.5f;
                float midY = (p1.y + p2.y) * 0.5f;
                
                // Straight-line Euclidean distance between both fingers:
                //   dist = sqrt((p2.x - p1.x)^2 + (p2.y - p1.y)^2)
                float dist = std::hypot(p2.x - p1.x, p2.y - p1.y);

                // Initial frame for 2-finger contact
                if (!isPinching || lastActiveCount != 2) {
                    isPinching           = true;
                    prevPinchDist        = dist;
                    prevMidX             = midX;
                    prevMidY             = midY;
                    twoFingerStartTimeMs = nowMs;
                    twoFingerMoved       = false;
                } else {
                    // Check if movement exceeded tap slop
                    if (std::abs(dist - prevPinchDist) > DRAG_THRESHOLD_PX ||
                        std::hypot(midX - prevMidX, midY - prevMidY) > DRAG_THRESHOLD_PX) {
                        twoFingerMoved = true;
                    }

                    // Continuous 2-finger pan & zoom updates
                    gesture.type   = TouchGestureType::TwoFingerPinchPan;
                    gesture.focusX = midX;
                    gesture.focusY = midY;
                    
                    // Centroid translation for canvas panning
                    gesture.deltaX = midX - prevMidX;
                    gesture.deltaY = midY - prevMidY;
                    
                    // Distance ratio for zoom factor:
                    //   zoomDelta = dist_current / dist_previous
                    gesture.zoomDelta = (prevPinchDist > 0.001f) ? (dist / prevPinchDist) : 1.0f;

                    prevPinchDist = dist;
                    prevMidX      = midX;
                    prevMidY      = midY;
                }
            }

            lastActiveCount = 2;
            return gesture;
        }

        // ---------------------------------------------------------------------
        // 3 FINGERS: THREE-FINGER GESTURES (e.g. Redo tap tracking)
        // ---------------------------------------------------------------------
        if (activeCount == 3) {
            if (lastActiveCount != 3) {
                threeFingerStartTimeMs = nowMs;
            }
            lastActiveCount = 3;
            return gesture;
        }

        lastActiveCount = activeCount;
        return gesture;
    }
};