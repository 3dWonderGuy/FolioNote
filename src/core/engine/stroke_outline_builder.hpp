#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <blend2d/blend2d.h>
#include "core/engine/stroke_smoother.hpp"
#include "input/pen_palette.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class StrokeOutlineBuilder {
public:
    struct InputPoint {
        double x = 0.0;
        double y = 0.0;
        float width = 1.0f; // Total thickness in mm
        float pressure = 1.0f;
        float speed = 0.0f;
        float tiltX = 0.0f;
        float tiltY = 0.0f;
    };

    /**
     * @brief Builds a unified, continuous 2D closed ribbon polygon from an ordered list of points.
     */
    static BLPath BuildOutline(const std::vector<InputPoint>& rawPoints, CapType capType = CapType::Round) {
        BLPath path;
        if (rawPoints.empty()) return path;

        // /*
        //  * OLD LOGIC:
        //  * 1. ResampleArcLength(rawPoints, STEP_MM, pts);
        //  *    - Resampled the stroke at 0.5mm intervals for dense geometry.
        //  */
        
        // We now just use the raw points directly.
        const auto& pts = rawPoints;

        if (pts.size() == 1) {
            double r = std::max(0.05, (double)pts[0].width * 0.5);
            path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
            return path;
        }

        const size_t N = pts.size();

        struct Vec2 { double x = 0; double y = 0; };
        std::vector<Vec2> left(N);
        std::vector<Vec2> right(N);

        // Basic Polygon Formation
        for (size_t i = 0; i < N; ++i) {
            double nx = 0.0, ny = 0.0;
            // Average normal from adjacent segments for smooth continuous ribbon
            if (i > 0) {
                double dx = pts[i].x - pts[i - 1].x;
                double dy = pts[i].y - pts[i - 1].y;
                double len = std::hypot(dx, dy);
                if (len > 1e-7) {
                    nx += -dy / len;
                    ny += dx / len;
                }
            }
            if (i < N - 1) {
                double dx = pts[i + 1].x - pts[i].x;
                double dy = pts[i + 1].y - pts[i].y;
                double len = std::hypot(dx, dy);
                if (len > 1e-7) {
                    nx += -dy / len;
                    ny += dx / len;
                }
            }
            double nLen = std::hypot(nx, ny);
            if (nLen > 1e-7) {
                nx /= nLen;
                ny /= nLen;
            } else {
                nx = 1.0;
                ny = 0.0;
            }

            double r = std::max(0.05, (double)pts[i].width * 0.5);
            left[i]  = { pts[i].x + nx * r, pts[i].y + ny * r };
            right[i] = { pts[i].x - nx * r, pts[i].y - ny * r };
        }

        path.move_to(left[0].x, left[0].y);

        // 1. Follow Left side forward
        for (size_t i = 1; i < N; ++i) {
            path.line_to(left[i].x, left[i].y);
        }

        // 2. End Cap: Simple flat line
        path.line_to(right[N - 1].x, right[N - 1].y);

        // 3. Follow Right side backward
        for (size_t i = N - 1; i > 0; --i) {
            path.line_to(right[i - 1].x, right[i - 1].y);
        }

        // 4. Start Cap: Simple flat line connecting back
        path.line_to(left[0].x, left[0].y);

        path.close();
        return path;
    }

private:
    /*
     * OLD LOGIC:
     * static void AppendSemiCircleCap(...)
     * - Generated complex cubic bezier semicircles for round start/end caps.
     * 
     * static void AppendArc(...)
     * - Generated circular fillets for outer joints on sharp turns.
     * 
     * static void ResampleArcLength(...)
     * - Linearly interpolated points based on arc-length distance.
     */
};