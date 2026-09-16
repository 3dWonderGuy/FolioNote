/**
 * @file shape_container.hpp (ROOT — REDIRECT HEADER)
 *
 * This file is kept for backward compatibility with existing #include paths.
 * The ShapeObject class and associated enums have been modularized into:
 *
 *   core/objects/primitives/shape_types.hpp      — enums only
 *   core/objects/primitives/shape_container.hpp  — class declaration
 *   core/objects/primitives/shape_container.cpp  — implementation
 *   core/objects/connectors/connector_types.hpp  — ArrowHeadType, ConnectorStyle
 *   core/objects/connectors/smart_arrow_container.hpp — SmartArrowObject (Line/Arrow)
 *
 * All new code should #include the specific sub-folder headers directly.
 */
#pragma once
#include "core/objects/primitives/shape_types.hpp"
#include "core/objects/primitives/shape_container.hpp"
#include "core/objects/connectors/connector_types.hpp"
#include "core/objects/connectors/smart_arrow_container.hpp"
