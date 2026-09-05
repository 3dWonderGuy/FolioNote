#pragma once

#include <blend2d/blend2d.h>

struct Point2D {
    double x = 0.0;           // mm
    double y = 0.0;           // mm
    float pressure = 1.0f;
    double timeSeconds = 0.0;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
};

struct Segment1D {
    Point2D p0;
    Point2D p1;
    float width = 0.5f;       // mm
};