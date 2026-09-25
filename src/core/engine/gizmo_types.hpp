#pragma once
#include <cstdint>
#include "core/engine/stroke_smoother.hpp"

/**
 * @brief Categorizes the locked interaction gizmo style for canvas objects.
 *
 * Each object class designates a static/locked gizmo style:
 * - BoundingBox: Standard 8 resize handles + rotation knob (Shapes, Text, Images, Ink strokes, Tables, PDF, Video)
 * - TwoPoint: 2 draggable endpoint handles (Smart Arrows, Lines, Connectors)
 * - MoveOnly: Bounding box outline for dragging/moving, but NO resize grips or rotation knob (Attachment, Audio, Web bookmark chips)
 * - None: Completely suppresses gizmo rendering and manipulation
 */
enum class GizmoStyle : uint8_t {
    BoundingBox,
    TwoPoint,
    MoveOnly,
    None
};

/**
 * @brief Identifies the role and spatial position of a selection manipulator handle.
 */
enum class HandleRole {
    None,
    TopLeft,
    TopCenter,
    TopRight,
    RightCenter,
    BottomRight,
    BottomCenter,
    BottomLeft,
    LeftCenter,
    Rotation,
    Body,
    Custom
};

/**
 * @brief Describes a single interactive control grip/handle on the canvas.
 */
struct GizmoHandle {
    Point2D worldPos{0.0, 0.0};   // Position in canvas world space (mm)
    Point2D screenPos{0.0, 0.0};  // Position in physical viewport screen pixels
    HandleRole role = HandleRole::None;
    int customId = 0;             // Identifier for custom/TwoPoint handle roles (e.g. 0=start, 1=end)
};
