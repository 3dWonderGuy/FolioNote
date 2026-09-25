#pragma once
/**
 * @file smart_arrow_container.hpp
 * @brief Two-point line and arrow connector object with draggable endpoint handles.
 *
 * SmartArrowObject represents a directed line segment on the canvas. Unlike
 * closed shapes (ShapeObject), it:
 *  - Has exactly 2 control points: start (x1, y1) and end (x2, y2)
 *  - Exposes 2 custom gizmo handles (endpoint drag) instead of 8 bounding-box grips
 *  - Supports 4 arrowhead styles on each endpoint (None, Triangle, Stealth, Open, Circle)
 *  - Cannot have a fill (it is an open path by definition)
 *  - Uses the same outline styles as ShapeObject (Solid, Dashed, Dotted, DashDot)
 *
 * Routing Options:
 *  - Straight: Direct linear segment connecting start and end.
 *  - Curved: Cubic Bézier with automatic horizontal/vertical tangent control points.
 *  - Elbow: Orthogonal Manhattan 3-segment routing with midpoint step.
 *
 * Transform Contract:
 *  - ApplyTransform(): accumulates in BLMatrix2D without mutating x1/y1/x2/y2.
 *  - BakeTransform(): maps endpoints through the matrix, resets to identity.
 *  - Called by SelectionGizmo::OnPointerUp() same as ShapeObject.
 *
 * Gizmo Handles:
 *  - customId 0 = start endpoint (x1, y1)
 *  - customId 1 = end   endpoint (x2, y2)
 *  - TwoPoint gizmo drag updates the dragged endpoint directly (no matrix needed
 *    for individual endpoint adjustment — it is always a world translation).
 */

#include <memory>
#include <vector>
#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/objects/connectors/connector_types.hpp"
#include "core/objects/primitives/shape_types.hpp"  // ShapeOutlineType reuse
#include "core/spatial/aabb.hpp"
#include "core/engine/canvas_transform.hpp"

namespace Folio {

/**
 * @brief Directed line segment connector with optional arrowheads at each endpoint.
 *
 * Coordinates are in world-space millimeters. The line is defined by:
 *   start = (x1, y1)
 *   end   = (x2, y2)
 *
 * Any combination of start/end arrowheads can be active simultaneously.
 * strokeWidth is in world mm and visually compensated for active zoom/scale.
 */
class SmartArrowObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    double x1 = 0.0;   ///< Start point X (world mm)
    double y1 = 0.0;   ///< Start point Y (world mm)
    double x2 = 60.0;  ///< End point X (world mm)
    double y2 = 0.0;   ///< End point Y (world mm)

    BLRgba32        strokeColor{0x18, 0x1A, 0x20, 0xFF};
    double          strokeWidth   = 1.0;                     ///< Line thickness in world mm
    ShapeOutlineType outlineType  = ShapeOutlineType::Solid;

    ArrowHeadType   startArrow    = ArrowHeadType::None;
    ArrowHeadType   endArrow      = ArrowHeadType::Triangle;
    double          arrowHeadSize = 4.0;                     ///< Arrowhead size in world mm

    ConnectorStyle  connectorStyle = ConnectorStyle::Straight;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor creating a horizontal connector from (0, 0) to (60, 0).
     */
    SmartArrowObject();

    /**
     * @brief Explicit coordinate constructor.
     * @param sx Start point X in world millimeters
     * @param sy Start point Y in world millimeters
     * @param ex End point X in world millimeters
     * @param ey End point Y in world millimeters
     */
    SmartArrowObject(double sx, double sy, double ex, double ey);

    // =========================================================================
    // ROUTING GEOMETRY
    // =========================================================================

    /**
     * @brief Computes cubic Bézier control points for Curved connector routing.
     *
     * Mathematical derivation:
     * Evaluates dominant orientation: dx = x2 - x1, dy = y2 - y1.
     * - If |dx| >= |dy|: Horizontal tangents at mid-span:
     *     C1 = (x1 + 0.5 * dx, y1)
     *     C2 = (x2 - 0.5 * dx, y2)
     * - If |dy| > |dx|: Vertical tangents at mid-span:
     *     C1 = (x1, y1 + 0.5 * dy)
     *     C2 = (x2, y2 - 0.5 * dy)
     *
     * @param[out] outC1 First Bézier control point
     * @param[out] outC2 Second Bézier control point
     */
    void GetCurvedControlPoints(Point2D& outC1, Point2D& outC2) const;

    /**
     * @brief Computes orthogonal waypoints for Elbow (Manhattan) connector routing.
     *
     * Stepped routing creates a 3-segment orthogonal path through 4 waypoints:
     * - If |dx| >= |dy|: Stepped along mid-X:
     *     P0=(x1,y1), P1=(x1+0.5*dx,y1), P2=(x1+0.5*dx,y2), P3=(x2,y2)
     * - If |dy| > |dx|: Stepped along mid-Y:
     *     P0=(x1,y1), P1=(x1,y1+0.5*dy), P2=(x2,y1+0.5*dy), P3=(x2,y2)
     *
     * @param[out] outPoints Array of 4 waypoints defining the 3 orthogonal segments
     */
    void GetElbowWaypoints(Point2D outPoints[4]) const;

    // =========================================================================
    // BOUNDS & HIT TESTING
    // =========================================================================

    /**
     * @brief Recomputes world-space AABB bounding box enclosing all control points/waypoints,
     *        accounting for stroke half-width, arrowhead dimensions, and the active affine transform.
     */
    void UpdateBounds() override;

    /**
     * @brief Evaluates whether a world-space point lies within hit-test proximity of the connector path.
     *
     * Geometry routing checks:
     * - Straight: Point-to-segment distance to [ (x1,y1), (x2,y2) ].
     * - Curved: Polyline subdivision (16 segments) distance check.
     * - Elbow: Point-to-segment distance to each of the 3 orthogonal segments.
     *
     * @param worldX Query X in world millimeters
     * @param worldY Query Y in world millimeters
     * @return true if point is within tolerance (strokeWidth/2 + margin)
     */
    bool HitTest(double worldX, double worldY) const override;


    // =========================================================================
    // TRANSFORMS & GIZMO HANDLES
    // =========================================================================

    /**
     * @brief Applies an incremental affine transformation matrix to this connector.
     * @param matrix 2D affine transformation matrix
     */
    void ApplyTransform(const BLMatrix2D& matrix) override;

    /**
     * @brief Bakes the accumulated affine transform matrix into endpoints (x1,y1) and (x2,y2),
     *        then resets the transform matrix to identity.
     */
    void BakeTransform() override;

    /**
     * @brief SmartArrow connectors use a locked TwoPoint gizmo (endpoint handles 0 and 1).
     */
    GizmoStyle GetGizmoStyle() const noexcept override {
        return GizmoStyle::TwoPoint;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the connector path and arrowheads to a Blend2D raster context.
     *
     * Supports:
     * - Straight, Curved (Bézier), and Elbow routing styles
     * - Solid, Dashed, Dotted, and DashDot outline patterns via PathDasher
     * - Triangle, Stealth, Open, and Circle arrowheads
     * - Apex tangent offset push to prevent stroke round cap poke-through
     *
     * @param ctx Blend2D rendering context
     * @param viewport Active canvas viewport for culling/scaling
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // MAGNETIC ANCHOR SNAPPING
    // =========================================================================

    /**
     * @brief Finds the closest connection anchor point on candidate canvas objects.
     *
     * Evaluates 5 cardinal anchor points on each object's bounding box:
     *   1. Center:        (minX + 0.5*W, minY + 0.5*H)
     *   2. Top Center:    (minX + 0.5*W, minY)
     *   3. Bottom Center: (minX + 0.5*W, maxY)
     *   4. Left Center:   (minX,         minY + 0.5*H)
     *   5. Right Center:  (maxX,         minY + 0.5*H)
     *
     * @param queryPt World position of the cursor / endpoint
     * @param objects List of canvas objects on active page
     * @param[out] outAnchor Populated with closest anchor coordinates if snapped
     * @param thresholdMm Snap capture radius in world millimeters (default 6.0 mm)
     * @param excludeUid Object UID to exclude (e.g. self)
     * @return true if an anchor was found within thresholdMm, false otherwise.
     */
    static bool FindSnapAnchor(const Point2D& queryPt,
                               const std::vector<std::shared_ptr<CanvasObject>>& objects,
                               Point2D& outAnchor,
                               double thresholdMm = 6.0,
                               uint32_t excludeUid = 0);

    // =========================================================================
    // DUPLICATION & PERSISTENCE
    // =========================================================================

    /**
     * @brief Clones this connector instance.
     */
    std::unique_ptr<CanvasObject> Clone() const override;

private:
    // =========================================================================
    // PRIVATE HELPERS
    // =========================================================================

    /**
     * @brief Computes squared Euclidean distance from point P to segment [A, B].
     *
     * Vector projection math:
     *   v = B - A,  w = P - A
     *   t = clamp((w · v) / (v · v), 0, 1)
     *   proj = A + t * v
     *   return ||P - proj||²
     *
     * @param p Query point
     * @param a Segment start
     * @param b Segment end
     * @return Squared distance in mm²
     */
    static double DistSqPointToSegment(const Point2D& p,
                                       const Point2D& a,
                                       const Point2D& b);

    /**
     * @brief Draws a directional arrowhead at a line endpoint.
     *
     * Forward tangent vector:  (cos θ, sin θ)
     * Normal vector:           (-sin θ, cos θ)
     *
     * @param ctx Blend2D rendering context
     * @param tipX Arrowhead tip apex X in world mm
     * @param tipY Arrowhead tip apex Y in world mm
     * @param angleRad Direction angle in radians
     * @param headType Arrowhead geometry style
     * @param sizeMm Arrowhead length/scale in world mm
     * @param color Stroke/fill color
     */
    static void DrawArrowHead(BLContext& ctx,
                              double tipX, double tipY, double angleRad,
                              ArrowHeadType headType, double sizeMm,
                              const BLRgba32& color);
};

} // namespace Folio
