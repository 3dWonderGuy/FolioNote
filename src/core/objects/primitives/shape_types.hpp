#pragma once
#include <cstdint>

/**
 * @file shape_types.hpp
 * @brief Enum definitions for 2D geometric vector shape categorization.
 *
 * Separated from shape_container.hpp so that any subsystem (ribbon, engine,
 * serializer, input state machine) can reference shape type constants without
 * pulling in the full ShapeObject class or its Blend2D / ImGui dependencies.
 *
 * Scalability note: To add a new closed 2D shape primitive, add an entry here
 * and implement the corresponding case in shape_container.cpp::BuildPath().
 * No other files need modification.
 */

namespace Folio {

// =============================================================================
// SHAPE TYPE
// =============================================================================

/**
 * @brief Categorization of all 2D geometric vector shape primitives.
 *
 * Numerical values are stable for serialization. Pre-alpha: backward
 * compatibility is NOT guaranteed. Clean removals are acceptable.
 *
 * Closed shapes (closed path, can be filled):
 *   Rectangle, RoundedRectangle, Ellipse, Circle,
 *   Triangle, RightTriangle, RegularPolygon, Star, Heart, Cloud
 *
 * NOTE: Line and LineArrow have been promoted to SmartArrowObject
 * (connectors/smart_arrow_container.hpp). They are NOT listed here.
 */
enum class ShapeType : uint8_t {
    Rectangle       = 0,   ///< Standard axis-aligned rectangle
    RoundedRectangle = 1,  ///< Rectangle with adjustable corner fillet radius
    Ellipse         = 2,   ///< Full ellipse (two-step: major axis then minor)
    Triangle        = 3,   ///< Isosceles triangle (apex at top center)
    Diamond         = 4,   ///< 4-point rhombus/diamond
    Star            = 5,   ///< 5-point star (inner/outer radius ratio via param2)
    Hexagon         = 6,   ///< 6-sided regular polygon
    Arrow           = 7,   ///< Block arrow shape pointing right
    DoubleArrow     = 8,   ///< Bidirectional block arrow
    Line            = 9,   ///< Linear segment between start and current points
    Heart           = 10,  ///< Cubic-bezier heart shape
    Cloud           = 11,  ///< Rounded cloud silhouette (add_round_rect + ellipses)
    Circle          = 12,  ///< Symmetric circle dragged outward from center
    RightTriangle   = 13,  ///< 90-degree orthogonal triangle (right angle bottom-left)
    LineArrow       = 14,  ///< Linear connector with directional arrowheads
    RegularPolygon  = 15,  ///< N-sided regular polygon (param1 = side count, 3..32)
    SineWave          = 16,  ///< Continuous sine wave along horizontal axis (param1 = cycles)
    SquareWave        = 17,  ///< Alternating square wave / pulse train (param1 = cycles)
    TriangleWave      = 18,  ///< Symmetric triangle wave (param1 = cycles)
    RightTriangleWave = 19,  ///< Right triangle / sawtooth wave (param1 = cycles, param2 = 0: right angle right, 1: right angle left)
};

// =============================================================================
// SHAPE FILL TYPE
// =============================================================================

/**
 * @brief Infill rendering style for closed vector shapes.
 *
 * None: shape is transparent (no fill drawn). Default.
 * Solid: full opaque solid color (alpha forced to 255).
 * SemiTransparent: soft translucent wash (alpha ~90).
 * LinearGradient: two-stop left-to-right Blend2D gradient.
 * RadialGradient: two-stop center-outward Blend2D gradient.
 * Hatch*: tiled drafting texture drawn as a repeating BLPattern.
 *
 * Scalability: new fill types only require a new case in shape_container.cpp.
 */
enum class ShapeFillType : uint8_t {
    None            = 0,  ///< Transparent / no fill
    Solid           = 1,  ///< Fully opaque solid color
    SemiTransparent = 2,  ///< Translucent color wash (~35% opacity)
    LinearGradient  = 3,  ///< Left-to-right two-stop gradient
    RadialGradient  = 4,  ///< Center-outward two-stop gradient
    HatchDiagonal   = 5,  ///< 45-degree parallel drafting hatch lines (///)
    HatchCross      = 6,  ///< Intersecting 45-degree drafting grid (XXX)
    HatchHorizontal = 7,  ///< Horizontal parallel drafting lines (---)
    HatchVertical   = 8,  ///< Vertical parallel drafting lines (|||)
    HatchDots       = 9,  ///< Fine circular dot stipple (concrete/sand/soil grain)
};

// =============================================================================
// SHAPE OUTLINE TYPE
// =============================================================================

/**
 * @brief Border stroke pattern for vector shape outlines.
 *
 * Shapes with None outline and None fill are still selectable via AABB
 * proximity test. Outlines are always rendered at alpha=255 (fully opaque).
 */
enum class ShapeOutlineType : uint8_t {
    None    = 0,  ///< No border drawn (invisible outline)
    Solid   = 1,  ///< Continuous unbroken stroke
    Dashed  = 2,  ///< Evenly spaced dash segments
    Dotted  = 3,  ///< Crisp circular dots along the path
    DashDot = 4,  ///< Alternating long dash and circular dot
};

} // namespace Folio
