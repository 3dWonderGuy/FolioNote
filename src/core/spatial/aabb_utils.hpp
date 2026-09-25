#pragma once
/**
 * =========================================================================================
 * @file aabb_utils.hpp
 * @brief Extended spatial mathematics and geometric query algorithms for AABB bounding boxes.
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE:
 * Keeps `aabb.hpp` pristine as a lightweight POD data structure while providing
 * reusable, high-performance geometric math functions (circle-box intersection,
 * swept capsule intersection, clamped Euclidean distances) for hit-testing,
 * proximity queries, and selection systems.
 */

#include "core/spatial/aabb.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <blend2d/blend2d.h>

namespace Folio::AABBUtils {

/**
 * @brief Computes the enclosing world-space AABB of a transformed rectangle.
 * 
 * MATHEMATICAL FOUNDATION & WORKING PROCESS:
 * ------------------------------------------
 * 1. Coordinate Normalization:
 *    Handles negative width/height (which occur during inverted user resizing/mirroring):
 *      minX_local = min(x, x + width),  maxX_local = max(x, x + width)
 *      minY_local = min(y, y + height), maxY_local = max(y, y + height)
 * 
 * 2. 2D Affine Transformation:
 *    Maps each of the 4 normalized corners through the 2D affine matrix M:
 *      [ x' ]   [ m00  m01  m02 ] [ x ]   [ m00*x + m01*y + m02 ]
 *      [ y' ] = [ m10  m11  m12 ] [ y ] = [ m10*x + m11*y + m12 ]
 *      [ 1  ]   [  0    0    1  ] [ 1 ]   [          1          ]
 * 
 *    Corners transformed:
 *      C0 = M * (minX, minY)
 *      C1 = M * (maxX, minY)
 *      C2 = M * (maxX, maxY)
 *      C3 = M * (minX, maxY)
 * 
 * 3. Enclosing World-Space Bounds Calculation:
 *    Finds the minimum and maximum X and Y across all 4 mapped vertices:
 *      worldMinX = min(C0.x, C1.x, C2.x, C3.x)
 *      worldMinY = min(C0.y, C1.y, C2.y, C3.y)
 *      worldMaxX = max(C0.x, C1.x, C2.x, C3.x)
 *      worldMaxY = max(C0.y, C1.y, C2.y, C3.y)
 * 
 * @param x Origin X in local millimeters.
 * @param y Origin Y in local millimeters.
 * @param width Extent width in local millimeters (may be negative).
 * @param height Extent height in local millimeters (may be negative).
 * @param transform 2D affine transformation matrix.
 * @return AABB Tight world-space bounding box enclosing the transformed rectangle.
 */
[[nodiscard]] inline AABB ComputeTransformedBounds(double x, double y, double width, double height, const BLMatrix2D& transform) noexcept {
    double minX = (std::min)(x, x + width);
    double maxX = (std::max)(x, x + width);
    double minY = (std::min)(y, y + height);
    double maxY = (std::max)(y, y + height);

    BLPoint corners[4] = {
        transform.map_point(minX, minY),
        transform.map_point(maxX, minY),
        transform.map_point(maxX, maxY),
        transform.map_point(minX, maxY)
    };

    return AABB{
        (std::min)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
        (std::min)({corners[0].y, corners[1].y, corners[2].y, corners[3].y}),
        (std::max)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
        (std::max)({corners[0].y, corners[1].y, corners[2].y, corners[3].y})
    };
}

/**
 * @brief Computes the enclosing world-space AABB of a local AABB transformed by a 2D matrix.
 * 
 * @param localBox Local-space bounding box.
 * @param transform 2D affine transformation matrix.
 * @return AABB Tight world-space bounding box.
 */
[[nodiscard]] inline AABB ComputeTransformedBounds(const AABB& localBox, const BLMatrix2D& transform) noexcept {
    if (localBox.IsEmpty()) {
        return localBox;
    }
    return ComputeTransformedBounds(localBox.minX, localBox.minY, localBox.Width(), localBox.Height(), transform);
}

/**
 * @brief Computes the enclosing world-space AABB of an arbitrary set of 2D points transformed by a matrix.
 * 
 * @param points Pointer to array of BLPoint vertices.
 * @param count Number of vertices in the array.
 * @param transform 2D affine transformation matrix.
 * @return AABB Tight world-space bounding box enclosing all transformed vertices.
 */
[[nodiscard]] inline AABB ComputeTransformedBounds(const BLPoint* points, size_t count, const BLMatrix2D& transform) noexcept {
    if (!points || count == 0) {
        return AABB{};
    }

    BLPoint first = transform.map_point(points[0].x, points[0].y);
    double minX = first.x, maxX = first.x;
    double minY = first.y, maxY = first.y;

    for (size_t i = 1; i < count; ++i) {
        BLPoint pt = transform.map_point(points[i].x, points[i].y);
        minX = (std::min)(minX, pt.x);
        maxX = (std::max)(maxX, pt.x);
        minY = (std::min)(minY, pt.y);
        maxY = (std::max)(maxY, pt.y);
    }

    return AABB(minX, minY, maxX, maxY);
}

/**
 * @brief Commits an accumulated 2D affine transform matrix into intrinsic rectangular coordinates (X, Y, Width, Height)
 * and resets the matrix to the identity state.
 * 
 * MATHEMATICAL FOUNDATION & WORKING PROCESS:
 * ------------------------------------------
 * 1. Axis-Aligned Guard:
 *    Only axis-aligned scale + translation transforms (where shear/rotation m01 ≈ 0 and m10 ≈ 0)
 *    can be committed cleanly into an axis-aligned rectangle (worldX, worldY, worldWidth, worldHeight).
 *    Rotated objects must preserve their rotation matrix.
 * 
 * 2. Identity Fast-Path:
 *    If the matrix is already identity (m00=1, m11=1, m20=0, m21=0), returns false (nothing to bake).
 * 
 * 3. 2D Affine Transformation of Diagonal Corners:
 *    P0 = (inOutX, inOutY)
 *    P1 = (inOutX + inOutWidth, inOutY + inOutHeight)
 *    
 *    p0' = (m00 * P0.x + m20, m11 * P0.y + m21)
 *    p1' = (m00 * P1.x + m20, m11 * P1.y + m21)
 * 
 * 4. Normalization and Bounds Protection:
 *    Handles handle dragging across opposite axes (horizontal or vertical flipping):
 *      inOutX      = min(p0'.x, p1'.x)
 *      inOutY      = min(p0'.y, p1'.y)
 *      inOutWidth  = max(minSize, |p1'.x - p0'.x|)
 *      inOutHeight = max(minSize, |p1'.y - p0'.y|)
 * 
 * 5. Matrix Reset:
 *    Resets inOutTransform to BLMatrix2D::make_identity().
 * 
 * @param[in,out] inOutX Origin X coordinate in millimeters.
 * @param[in,out] inOutY Origin Y coordinate in millimeters.
 * @param[in,out] inOutWidth Width in millimeters.
 * @param[in,out] inOutHeight Height in millimeters.
 * @param[in,out] inOutTransform 2D affine matrix; committed and reset to identity on success.
 * @param[in] minSize Minimum allowed width and height to prevent degenerate zero-area collapse.
 * @return true if the transform was non-identity, axis-aligned, and successfully baked; false otherwise.
 */
inline bool BakeTransformedRect(double& inOutX, double& inOutY,
                                double& inOutWidth, double& inOutHeight,
                                BLMatrix2D& inOutTransform,
                                double minSize = 1.0) noexcept {
    if (std::abs(inOutTransform.m01) < 1e-6 && std::abs(inOutTransform.m10) < 1e-6) {
        if (inOutTransform.m00 == 1.0 && inOutTransform.m11 == 1.0 &&
            inOutTransform.m20 == 0.0 && inOutTransform.m21 == 0.0) {
            return false; // Already identity — nothing to bake
        }

        double p0x = (inOutTransform.m00 * inOutX)                 + inOutTransform.m20;
        double p0y = (inOutTransform.m11 * inOutY)                 + inOutTransform.m21;
        double p1x = (inOutTransform.m00 * (inOutX + inOutWidth))  + inOutTransform.m20;
        double p1y = (inOutTransform.m11 * (inOutY + inOutHeight)) + inOutTransform.m21;

        inOutX      = (std::min)(p0x, p1x);
        inOutY      = (std::min)(p0y, p1y);
        inOutWidth  = (std::max)(minSize, std::abs(p1x - p0x));
        inOutHeight = (std::max)(minSize, std::abs(p1y - p0y));

        inOutTransform = BLMatrix2D::make_identity();
        return true;
    }
    return false;
}

/**
 * @brief Computes the closest point on an AABB to an external point (px, py).
 * 
 * Mathematical Process:
 *   Clamps the query coordinates to the box bounds:
 *     Q_x = clamp(px, minX, maxX)
 *     Q_y = clamp(py, minY, maxY)
 * 
 * @param box Target bounding box.
 * @param px Query point X coordinate in millimeters.
 * @param py Query point Y coordinate in millimeters.
 * @return std::pair<double, double> Coordinates (Q_x, Q_y) of the closest point on the AABB.
 */
[[nodiscard]] inline std::pair<double, double> ClosestPoint(const AABB& box, double px, double py) noexcept {
    if (box.IsEmpty()) {
        return {px, py};
    }
    double qx = (std::max)(box.minX, (std::min)(px, box.maxX));
    double qy = (std::max)(box.minY, (std::min)(py, box.maxY));
    return {qx, qy};
}

/**
 * @brief Computes the squared Euclidean distance from a point to the closest point on the AABB.
 * Returns 0.0 if the point is strictly inside the box.
 * 
 * Mathematical Formulation:
 *   dist^2 = (px - clamp(px, minX, maxX))^2 + (py - clamp(py, minY, maxY))^2
 *   Using squared distance avoids expensive std::sqrt() calls in tight inner collision loops.
 * 
 * @param box Target bounding box.
 * @param px Query point X coordinate in millimeters.
 * @param py Query point Y coordinate in millimeters.
 * @return Squared Euclidean distance as a positive double.
 */
[[nodiscard]] inline double DistanceSquared(const AABB& box, double px, double py) noexcept {
    if (box.IsEmpty()) {
        return std::numeric_limits<double>::infinity();
    }
    double clampedX = (std::max)(box.minX, (std::min)(px, box.maxX));
    double clampedY = (std::max)(box.minY, (std::min)(py, box.maxY));
    double dx = px - clampedX;
    double dy = py - clampedY;
    return (dx * dx) + (dy * dy);
}

/**
 * @brief Evaluates whether a circle (center at cx, cy with given radius) touches or overlaps an AABB.
 * 
 * Working Process:
 *   1. Broad phase: Checks if circle center is inside the box (instant hit).
 *   2. Distance test: Evaluates if squared distance from circle center to closest point on AABB <= radius^2.
 * 
 * @param box Target bounding box.
 * @param cx Circle center X in millimeters.
 * @param cy Circle center Y in millimeters.
 * @param radius Radius of circle in millimeters.
 * @return true if the circle overlaps or touches the AABB; false otherwise.
 */
[[nodiscard]] inline bool IntersectsCircle(const AABB& box, double cx, double cy, double radius) noexcept {
    if (box.IsEmpty() || radius < 0.0) {
        return false;
    }
    // Direct interior containment fast-path
    if (box.Contains(cx, cy)) {
        return true;
    }
    return DistanceSquared(box, cx, cy) <= (radius * radius);
}

/**
 * @brief Evaluates whether a continuous swept capsule segment (from (x0, y0) to (x1, y1) with radius) touches an AABB.
 * Prevents high-speed tunneling in eraser strokes and continuous raycast queries.
 * 
 * Mathematical Process:
 *   1. Broad phase: Check if swept segment AABB intersects target box.
 *   2. Endpoint tests: IntersectsCircle(box, x0, y0, radius) or IntersectsCircle(box, x1, y1, radius).
 *   3. Segment projection: Projects box center C onto segment S(t) = w0 + t * (w1 - w0), t in [0, 1].
 *   4. Circle test at closest segment point: IntersectsCircle(box, projX, projY, radius).
 * 
 * @param box Target bounding box.
 * @param x0 Segment start X in millimeters.
 * @param y0 Segment start Y in millimeters.
 * @param x1 Segment end X in millimeters.
 * @param y1 Segment end Y in millimeters.
 * @param radius Capsule radius in millimeters.
 * @return true if the swept volume intersects the AABB; false otherwise.
 */
[[nodiscard]] inline bool IntersectsSwept(const AABB& box, double x0, double y0, double x1, double y1, double radius) noexcept {
    if (box.IsEmpty() || radius < 0.0) {
        return false;
    }

    // 1. Broad-phase query box check
    AABB sweptBox(
        (std::min)(x0, x1) - radius,
        (std::min)(y0, y1) - radius,
        (std::max)(x0, x1) + radius,
        (std::max)(y0, y1) + radius
    );
    if (!box.Intersects(sweptBox)) {
        return false;
    }

    // 2. Check segment endpoints
    if (IntersectsCircle(box, x0, y0, radius) || IntersectsCircle(box, x1, y1, radius)) {
        return true;
    }

    // 3. Project box center onto the segment
    double centerX = (box.minX + box.maxX) * 0.5;
    double centerY = (box.minY + box.maxY) * 0.5;

    double vx = x1 - x0;
    double vy = y1 - y0;
    double lenSq = (vx * vx) + (vy * vy);

    if (lenSq > 1e-6) {
        double t = ((centerX - x0) * vx + (centerY - y0) * vy) / lenSq;
        t = (std::max)(0.0, (std::min)(1.0, t));
        double projX = x0 + t * vx;
        double projY = y0 + t * vy;
        return IntersectsCircle(box, projX, projY, radius);
    }

    return false;
}

} // namespace Folio::AABBUtils

// Global namespace alias for convenience across non-namespaced translation units
namespace AABBUtils = Folio::AABBUtils;
