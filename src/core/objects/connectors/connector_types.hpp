#pragma once
#include <cstdint>

/**
 * @file connector_types.hpp
 * @brief Enum definitions for 2-point connector and arrowhead styles.
 *
 * Connectors (Line, Arrow) are distinct from closed 2D shapes:
 *  - They have two draggable endpoint handles instead of an 8-point bounding box.
 *  - They cannot be filled (no ShapeFillType applies).
 *  - They are managed by SmartArrowObject, not ShapeObject.
 *
 * Scalability: add new ArrowHeadType or ConnectorStyle values here without
 * touching shape_container or any other file outside connectors/.
 */

namespace Folio {

// =============================================================================
// ARROWHEAD TYPE
// =============================================================================

/**
 * @brief Termination style drawn at each endpoint of a SmartArrowObject.
 *
 * Drawing math (all computed relative to line direction angle θ):
 *   cosA = cos(θ), sinA = sin(θ), perpX = -sinA, perpY = cosA
 *
 *  Triangle:   filled solid triangle — tip at endpoint, base at (tip - size * dir)
 *  Stealth:    winged arrowhead with notched indentation at 75% shaft
 *  Open:       two-stroke open chevron, no fill (wireframe V)
 *  Circle:     filled circle at endpoint tip, radius = size * 0.4
 */
enum class ArrowHeadType : uint8_t {
    None     = 0,  ///< No termination decoration
    Triangle = 1,  ///< Classic solid filled equilateral arrowhead
    Stealth  = 2,  ///< Aerodynamic winged arrowhead with notched base
    Open     = 3,  ///< Two-stroke open chevron (wireframe, no fill)
    Circle   = 4,  ///< Circular terminal dot
};

// =============================================================================
// CONNECTOR STYLE
// =============================================================================

/**
 * @brief Routing / path style for multi-point smart connectors.
 *
 * Supports Straight (direct segment), Curved (smooth cubic S-curve),
 * and Elbow (orthogonal Manhattan right-angle routing).
 */
enum class ConnectorStyle : uint8_t {
    Straight = 0,  ///< Direct 2-point line segment 
    Curved   = 1,  ///< Cubic bezier routing 
    Elbow    = 2,  ///< Right-angle elbow routing
};

} // namespace Folio
