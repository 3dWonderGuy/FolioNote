#pragma once
#include <algorithm>
#include <blend2d/blend2d.h>

/**
 * @brief Axis-Aligned Bounding Box struct that holds the bounds of an object
 * This is basiccaly a square with X and Y coordinates for its top-left corner and bottom-right corner
 */
struct AABB {

    // main variables
    double minX = 0.0, minY = 0.0;
    double maxX = 0.0, maxY = 0.0;

    /**
     * @brief Checks if this AABB intersects with another AABB
     * @param other The other AABB to check for intersection
     * @return True if the AABBs intersect, false otherwise
     */
    constexpr bool Intersects(const AABB& other) const noexcept {
        return minX <= other.maxX && 
               maxX >= other.minX &&
               minY <= other.maxY && 
               maxY >= other.minY;
    }

    /**
     * @brief Simple functiont that checks if the point is inside the AABB box
     * @param x X coordinate of the point
     * @param y Y coordinate of the point
     * @return True if the point is inside the AABB, false otherwise
     */
    constexpr bool Contains(double x, double y) const noexcept {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }

    /**
     * @brief Merges this AABB with another AABB, basicaly we create a bigger box that covers both AABBs
     * @param other The other AABB to merge with
     * @note 
     */
    constexpr void Merge(const AABB& other) noexcept {
        minX = std::min(minX, other.minX);
        minY = std::min(minY, other.minY);
        maxX = std::max(maxX, other.maxX);
        maxY = std::max(maxY, other.maxY);
    }

    /**
     * @brief Merges this AABB with a point
     * @param x X coordinate of the point
     * @param y Y coordinate of the point
     * @note This is used to expand the AABB to include a new point 
     */
    constexpr void Merge(double x, double y) noexcept {
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }

    /**
     * @brief Expands the AABB by a given delta in all directions
     * @param delta The delta to expand the AABB by
     */
    constexpr void Expand(double delta) noexcept {
        minX -= delta;
        minY -= delta;
        maxX += delta;
        maxY += delta;
    }

    /**
     * @brief Returns the width of the AABB
     * @return The width of the AABB
     */
    constexpr double Width() const noexcept { return maxX - minX; }

    /**
     * @brief Returns the height of the AABB
     * @return The height of the AABB
     */
    constexpr double Height() const noexcept { return maxY - minY; }

    /**
     * @brief Returns the area of the AABB
     * @return The area of the AABB
     */
    constexpr double Area() const noexcept { return Width() * Height(); }

    /**
     * @brief Computes the intersection AABB between this and another AABB.
     * If they do not intersect, returns an AABB with zero width/height.
     */
    constexpr AABB Intersection(const AABB& other) const noexcept {
        double ix0 = (minX > other.minX) ? minX : other.minX;
        double iy0 = (minY > other.minY) ? minY : other.minY;
        double ix1 = (maxX < other.maxX) ? maxX : other.maxX;
        double iy1 = (maxY < other.maxY) ? maxY : other.maxY;

        if (ix0 < ix1 && iy0 < iy1) {
            return AABB(ix0, iy0, ix1, iy1);
        }
        return AABB(0.0, 0.0, 0.0, 0.0);
    }

    /**
     * @brief Computes the area of intersection between this and another AABB.
     */
    constexpr double IntersectionArea(const AABB& other) const noexcept {
        return Intersection(other).Area();
    }
};

/**
 * @brief This structure basically holds the visible area of our window/canvas
 * for efficiency. so we only have to draw what we see
 */
struct Viewport {
    AABB bounds;
    double zoom = 1.0; // default 100% zoom
};