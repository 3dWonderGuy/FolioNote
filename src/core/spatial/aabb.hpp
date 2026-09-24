#pragma once
#include <algorithm>
#include <limits>
#include <cmath>
#include <blend2d/blend2d.h>

/**
 * @brief Axis-Aligned Bounding Box (AABB) in world space (millimeters).
 */
struct AABB {
    // Default to an empty, inverted state so the first merged point sets the true extents.
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();

    /**
     * @brief Checks whether all coordinates are finite numbers (not NaN, not Inf).
     */
    [[nodiscard]] constexpr bool IsFinite() const noexcept {
        return (minX == minX) && (maxX == maxX) && (minY == minY) && (maxY == maxY) &&
               (minX > -std::numeric_limits<double>::max()) &&
               (maxX < std::numeric_limits<double>::max()) &&
               (minY > -std::numeric_limits<double>::max()) &&
               (maxY < std::numeric_limits<double>::max());
    }

    /**
     * @brief Checks whether this bounding box has valid, finite, and non-inverted bounds within canvas limits.
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        if (!IsFinite()) return false;
        if (minX > maxX || minY > maxY) return false;
        constexpr double MAX_COORD = 1e8; // 100,000 km in world mm
        return (minX > -MAX_COORD && maxX < MAX_COORD &&
                minY > -MAX_COORD && maxY < MAX_COORD);
    }

    /**
     * @brief Checks whether the bounding box is empty, inverted, or non-finite.
     */
    [[nodiscard]] constexpr bool IsEmpty() const noexcept {
        if (!IsFinite()) return true;
        return minX > maxX || minY > maxY;
    }

    constexpr void Reset() noexcept {
        minX = std::numeric_limits<double>::infinity();
        minY = std::numeric_limits<double>::infinity();
        maxX = -std::numeric_limits<double>::infinity();
        maxY = -std::numeric_limits<double>::infinity();
    }

    [[nodiscard]] constexpr bool Intersects(const AABB& other) const noexcept {
        if (IsEmpty() || other.IsEmpty()) return false;
        return minX <= other.maxX && 
               maxX >= other.minX &&
               minY <= other.maxY && 
               maxY >= other.minY;
    }

    [[nodiscard]] constexpr bool Contains(double x, double y) const noexcept {
        if (IsEmpty()) return false;
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }

    constexpr void Merge(const AABB& other) noexcept {
        if (other.IsEmpty()) return;
        if (IsEmpty()) {
            *this = other;
            return;
        }
        minX = (std::min)(minX, other.minX);
        minY = (std::min)(minY, other.minY);
        maxX = (std::max)(maxX, other.maxX);
        maxY = (std::max)(maxY, other.maxY);
    }

    constexpr void Merge(double x, double y) noexcept {
        if (IsEmpty()) {
            minX = maxX = x;
            minY = maxY = y;
            return;
        }
        minX = (std::min)(minX, x);
        minY = (std::min)(minY, y);
        maxX = (std::max)(maxX, x);
        maxY = (std::max)(maxY, y);
    }

    constexpr void Expand(double delta) noexcept {
        if (IsEmpty()) return;
        minX -= delta;
        minY -= delta;
        maxX += delta;
        maxY += delta;
    }

    [[nodiscard]] constexpr double Width() const noexcept { 
        return (maxX > minX) ? (maxX - minX) : 0.0; 
    }

    [[nodiscard]] constexpr double Height() const noexcept { 
        return (maxY > minY) ? (maxY - minY) : 0.0; 
    }

    [[nodiscard]] constexpr double Area() const noexcept { 
        double w = Width();
        double h = Height();
        return (w > 0.0 && h > 0.0) ? (w * h) : 0.0; 
    }

    [[nodiscard]] constexpr AABB Intersection(const AABB& other) const noexcept {
        if (IsEmpty() || other.IsEmpty()) return AABB{};

        double ix0 = (std::max)(minX, other.minX);
        double iy0 = (std::max)(minY, other.minY);
        double ix1 = (std::min)(maxX, other.maxX);
        double iy1 = (std::min)(maxY, other.maxY);

        if (ix0 < ix1 && iy0 < iy1) {
            return AABB{ix0, iy0, ix1, iy1};
        }
        return AABB{};
    }

    [[nodiscard]] constexpr double IntersectionArea(const AABB& other) const noexcept {
        return Intersection(other).Area();
    }
};

/**
 * @brief Represents the visible viewport on canvas.
 * Defaults to a standard 1080p canvas window surface rather than an empty box.
 */
struct Viewport {
    AABB bounds{ 0.0, 0.0, 1920.0, 1080.0 };
    double zoom = 1.0;
};