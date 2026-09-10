#pragma once
#include "core/engine/stroke_smoother.hpp"

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
    int customId = 0;             // Identifier passed to OnGizmoHandleDrag for custom roles
};
