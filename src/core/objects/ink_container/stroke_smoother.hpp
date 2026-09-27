#pragma once
/**
 * =========================================================================================
 * @file core/objects/ink_container/stroke_smoother.hpp
 * @brief Core Vector Ink Geometry Primitives (Point2D and Segment1D)
 * =========================================================================================
 *
 * Provides fundamental 2D point and line segment data structures used across vector ink
 * stroke modeling, outline tessellation, collision testing, and rendering.
 */

#include <blend2d/blend2d.h>

/**
 * @struct Point2D
 * @brief 2D coordinate on the infinite canvas with stylus dynamics telemetry.
 *
 * All spatial coordinates (x, y) are represented in physical millimeters (mm).
 */
struct Point2D {
    double x = 0.0;           ///< X position in world millimeters (mm)
    double y = 0.0;           ///< Y position in world millimeters (mm)
    float pressure = 1.0f;    ///< Stylus tip normalized pressure [0.0, 1.0]
    double timeSeconds = 0.0; ///< Sampling timestamp in seconds
    float tiltX = 0.0f;       ///< Stylus tilt angle along X axis in radians
    float tiltY = 0.0f;       ///< Stylus tilt angle along Y axis in radians
};

/**
 * @struct Segment1D
 * @brief Linear segment between two points with physical thickness.
 */
struct Segment1D {
    Point2D p0;               ///< Starting point of the segment
    Point2D p1;               ///< Ending point of the segment
    float width = 0.5f;       ///< Segment line thickness in world millimeters (mm)
};
