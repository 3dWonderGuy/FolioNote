#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/ink_stroke_data.hpp
 * @brief Self-Sustained Vector Ink Geometry Data Structures
 * =========================================================================================
 *
 * Fundamental 2D point, line segment, and finished stroke data structures used
 * across vector ink stroke modeling, outline tessellation, collision testing, and rendering.
 *
 * Self-Sustained Design:
 * Holds pure geometric and telemetry value types without dependencies on external engine
 * logic, solvers, or UI palettes.
 */

#include <cstdint>
#include <vector>
#include <blend2d/blend2d.h>

/**
 * @enum CapType
 * @brief Endcap geometry style for terminal stroke ribbon segments.
 */
enum class CapType : uint8_t {
    Round = 0,
    Flat,
    Chisel,
    Square
};

/**
 * @enum StrokePattern
 * @brief Tessellation pattern along the stroke centerline.
 */
enum class StrokePattern : uint8_t {
    Solid = 0,
    Dashed,
    Dotted,
    DashDot,        ///< Alternating dash and dot stroke pattern
    TexturedPencil
};

/**
 * @struct StrokeStyle
 * @brief Core visual and geometric properties applied to a vector stroke or shape outline.
 */
struct StrokeStyle {
    float         baseWidth = 0.5f;                             ///< Base line thickness in world millimeters (mm)
    CapType       capType   = CapType::Round;                   ///< Terminal segment cap style
    StrokePattern pattern   = StrokePattern::Solid;             ///< Tessellation pattern (solid, dashed, dotted, dash-dot)
    BLRgba32      color     = BLRgba32(0x18, 0x1A, 0x20, 0xFF); ///< Pigment color
};

/**
 * @enum InfillType
 * @brief Interior fill style for closed polygons (polygons only; ink strokes never fill).
 */
enum class InfillType : uint8_t {
    None = 0,         ///< Transparent / no fill
    Solid,            ///< Fully opaque solid color
    SemiTransparent,  ///< Translucent color wash
    LinearGradient,   ///< Two-stop gradient across the bounds diagonal
    RadialGradient,   ///< Two-stop center-outward gradient
    HatchDiagonal,    ///< 45-degree parallel drafting lines (///)
    HatchCross,       ///< Intersecting 45-degree drafting grid (XXX)
    HatchHorizontal,  ///< Horizontal parallel drafting lines (---)
    HatchVertical,    ///< Vertical parallel drafting lines (|||)
    HatchDots         ///< Fine circular dot stipple
};

/**
 * @struct InfillStyle
 * @brief Everything the generator needs to paint a polygon interior.
 */
struct InfillStyle {
    InfillType type           = InfillType::None;
    BLRgba32   primaryColor   = BLRgba32(0x00, 0x78, 0xD4, 0x40); ///< Main fill / hatch color
    BLRgba32   secondaryColor = BLRgba32(0x00, 0xC4, 0xFF, 0x20); ///< Gradient end color
    double     outlineWidthMm = 1.0;                              ///< Outline thickness (hatch lines = half of this)
    BLBox      bounds         = BLBox(0.0, 0.0, 0.0, 0.0);        ///< World-space bounds (gradients & hatch density)
};

/**
 * @struct Point2D
 * @brief 2D coordinate on the infinite canvas with stylus dynamics telemetry.
 *
 * All spatial coordinates (x, y) are represented in physical world millimeters (mm).
 */
struct Point2D {
    double x = 0.0;            ///< X position in world millimeters (mm)
    double y = 0.0;            ///< Y position in world millimeters (mm)
    float  width = 1.0f;       ///< Total stroke thickness in millimeters (mm)
    float  pressure = 1.0f;    ///< Stylus tip pressure [0.0, 1.0]
    double timeSeconds = 0.0;  ///< Sampling timestamp in seconds
    float  speed = 0.0f;       ///< Stylus velocity in mm/s
    float  tiltX = 0.0f;       ///< Stylus tilt angle along X axis in radians
    float  tiltY = 0.0f;       ///< Stylus tilt angle along Y axis in radians
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

/**
 * @struct FinishedStrokeData
 * @brief Completed inking stroke container encapsulating 1D segments, modeled centerline, and outline polygon.
 */
struct FinishedStrokeData {
    std::vector<Segment1D> liveSegments;
    std::vector<Point2D> modeledPoints;
    BLPath outlinePath;
};