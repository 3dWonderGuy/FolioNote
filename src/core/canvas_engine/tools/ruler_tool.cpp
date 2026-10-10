/**
 * =========================================================================================
 * @file core/canvas_engine/tools/ruler_tool.cpp
 * @brief Implementation of the Interactive Digital Straightedge Ruler
 * =========================================================================================
 *
 * MATHEMATICAL PROCESS & TESSELLATION DETAILS:
 * 1. World-to-Screen Affine Mapping:
 *    All ruler geometry (center, ticks, straightedge limits) is anchored in physical
 *    canvas millimeters (mm). When rendering, vertices are projected into screen pixels:
 *      S = WorldToScreen(W) + canvasOrigin
 *    This ensures that when the user pans or zooms the canvas, the ruler stays accurately
 *    registered against the real-world scale of the page.
 *
 * 2. Orthogonal Snapping Projection:
 *    Given stylus position P:
 *      t = (P - C) · u                 (distance along ruler axis)
 *      d_v = (P - C) · v               (transverse distance across ruler)
 *    The pen position is snapped to the nearest straightedge if |d_v ± (W/2)| <= snapDistance.
 *
 * 3. Rotational Kinematics & Angular Snapping:
 *    Mouse drags on the center dial evaluate angle = atan2(my - cy, mx - cx).
 *    Angles are automatically snapped to 15° increments when within 2.0° of a major drafting angle.
 */

#include "core/canvas_engine/tools/ruler_tool.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {

// -----------------------------------------------------------------------------
// Constructor & Kinematic Setters
// -----------------------------------------------------------------------------

RulerTool::RulerTool() {
    m_centerWorld = { 150.0, 150.0 }; // Initial center in physical canvas mm
    m_angleDeg = 0.0;
    m_lengthMm = 240.0; // 24 cm
    m_widthMm = 52.0;   // 5.2 cm
}

void RulerTool::SetAngleDeg(double degrees) noexcept {
    m_angleDeg = NormalizeAngleDeg(degrees);
}

double RulerTool::NormalizeAngleDeg(double deg) noexcept {
    while (deg >= 180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return deg;
}

double RulerTool::SnapAngleToIncrements(double deg, double increment, double threshold) noexcept {
    double snapped = std::round(deg / increment) * increment;
    if (std::abs(deg - snapped) <= threshold) {
        return snapped;
    }
    return deg;
}

// -----------------------------------------------------------------------------
// Vector Snapping Calculus (Stylus Telemetry)
// -----------------------------------------------------------------------------

RulerSnapResult RulerTool::SnapPoint(const Point2D& worldPoint) const {
    RulerSnapResult result;
    result.point = worldPoint;
    result.isSnapped = false;

    if (!m_enabled) {
        return result;
    }

    const double rad = GetAngleRad();
    const double ux = std::cos(rad);
    const double uy = std::sin(rad);
    const double vx = -uy;
    const double vy = ux;

    const double dx = worldPoint.x - m_centerWorld.x;
    const double dy = worldPoint.y - m_centerWorld.y;

    // Longitudinal projection (along ruler length)
    const double t = dx * ux + dy * uy;
    // Transverse projection (across ruler width)
    const double dv = dx * vx + dy * vy;

    const double halfLen = m_lengthMm * 0.5;
    const double halfWidth = m_widthMm * 0.5;

    // Distance to top straightedge (+halfWidth) and bottom straightedge (-halfWidth)
    const double distTop = std::abs(dv - halfWidth);
    const double distBot = std::abs(dv - (-halfWidth));

    // Allow a generous 15mm run-off past ends so pen gestures along edges don't abruptly disconnect
    const double marginMm = 15.0;
    const bool withinLength = (t >= -halfLen - marginMm) && (t <= halfLen + marginMm);

    if (!withinLength) {
        return result;
    }

    if (distTop <= m_snapThresholdMm && distTop <= distBot) {
        // Snap to top straightedge
        result.point.x = m_centerWorld.x + t * ux + halfWidth * vx;
        result.point.y = m_centerWorld.y + t * uy + halfWidth * vy;
        result.isSnapped = true;
        result.isTopEdge = true;
        result.distanceAlongMm = t + halfLen;
    } else if (distBot <= m_snapThresholdMm) {
        // Snap to bottom straightedge
        result.point.x = m_centerWorld.x + t * ux - halfWidth * vx;
        result.point.y = m_centerWorld.y + t * uy - halfWidth * vy;
        result.isSnapped = true;
        result.isTopEdge = false;
        result.distanceAlongMm = t + halfLen;
    }

    return result;
}

// -----------------------------------------------------------------------------
// Interactive ImGui Drawing & Manipulator Pipeline
// -----------------------------------------------------------------------------

void RulerTool::RenderImGui(const CanvasTransform& transform,
                            const ImVec2& canvasOrigin,
                            const ImVec2& canvasSize,
                            bool isDarkMode) {
    if (!m_enabled) {
        m_dragMode = DragMode::None;
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!drawList) return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mousePos = io.MousePos;

    // 1. Orientation & Direction Vectors in World Space
    const double rad = GetAngleRad();
    const double ux = std::cos(rad);
    const double uy = std::sin(rad);
    const double vx = -uy;
    const double vy = ux;

    const double halfLen = m_lengthMm * 0.5;
    const double halfWidth = m_widthMm * 0.5;

    // 2. Corner Vertices in World Space (mm)
    // TL = Center - (L/2)*u + (W/2)*v (Top-Left)
    // TR = Center + (L/2)*u + (W/2)*v (Top-Right)
    // BR = Center + (L/2)*u - (W/2)*v (Bottom-Right)
    // BL = Center - (L/2)*u - (W/2)*v (Bottom-Left)
    const Point2D wTL = { m_centerWorld.x - halfLen * ux + halfWidth * vx, m_centerWorld.y - halfLen * uy + halfWidth * vy };
    const Point2D wTR = { m_centerWorld.x + halfLen * ux + halfWidth * vx, m_centerWorld.y + halfLen * uy + halfWidth * vy };
    const Point2D wBR = { m_centerWorld.x + halfLen * ux - halfWidth * vx, m_centerWorld.y + halfLen * uy - halfWidth * vy };
    const Point2D wBL = { m_centerWorld.x - halfLen * ux - halfWidth * vx, m_centerWorld.y - halfLen * uy - halfWidth * vy };

    // 3. Project to Screen Space (pixels)
    const Point2D sTL_raw = transform.WorldToScreen(wTL.x, wTL.y);
    const Point2D sTR_raw = transform.WorldToScreen(wTR.x, wTR.y);
    const Point2D sBR_raw = transform.WorldToScreen(wBR.x, wBR.y);
    const Point2D sBL_raw = transform.WorldToScreen(wBL.x, wBL.y);
    const Point2D sC_raw  = transform.WorldToScreen(m_centerWorld.x, m_centerWorld.y);

    const ImVec2 sTL(canvasOrigin.x + static_cast<float>(sTL_raw.x), canvasOrigin.y + static_cast<float>(sTL_raw.y));
    const ImVec2 sTR(canvasOrigin.x + static_cast<float>(sTR_raw.x), canvasOrigin.y + static_cast<float>(sTR_raw.y));
    const ImVec2 sBR(canvasOrigin.x + static_cast<float>(sBR_raw.x), canvasOrigin.y + static_cast<float>(sBR_raw.y));
    const ImVec2 sBL(canvasOrigin.x + static_cast<float>(sBL_raw.x), canvasOrigin.y + static_cast<float>(sBL_raw.y));
    const ImVec2 sC (canvasOrigin.x + static_cast<float>(sC_raw.x),  canvasOrigin.y + static_cast<float>(sC_raw.y));

    // Screen Normalized Direction Vectors
    const float lenPx = std::hypot(sTR.x - sTL.x, sTR.y - sTL.y);
    const float widthPx = std::hypot(sTL.x - sBL.x, sTL.y - sBL.y);
    if (lenPx < 10.0f || widthPx < 5.0f) {
        return; // Too small to render meaningfully
    }

    const ImVec2 uScreen((sTR.x - sTL.x) / lenPx, (sTR.y - sTL.y) / lenPx);
    const ImVec2 vScreen((sTL.x - sBL.x) / widthPx, (sTL.y - sBL.y) / widthPx);

    // Left & Right Center Rotation Handles
    const ImVec2 handleLeft ((sTL.x + sBL.x) * 0.5f, (sTL.y + sBL.y) * 0.5f);
    const ImVec2 handleRight((sTR.x + sBR.x) * 0.5f, (sTR.y + sBR.y) * 0.5f);
    const float handleRadiusPx = 14.0f;

    // Center Protractor Dial Metrics
    const float dialRadiusPx = 28.0f;

    // 4. Point-in-Convex-Quad Hit Testing
    auto PointInQuad = [](const ImVec2& p, const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d) -> bool {
        auto Cross = [](const ImVec2& p1, const ImVec2& p2, const ImVec2& pt) -> float {
            return (p2.x - p1.x) * (pt.y - p1.y) - (p2.y - p1.y) * (pt.x - p1.x);
        };
        const float c1 = Cross(a, b, p);
        const float c2 = Cross(b, c, p);
        const float c3 = Cross(c, d, p);
        const float c4 = Cross(d, a, p);
        const bool hasNeg = (c1 < 0) || (c2 < 0) || (c3 < 0) || (c4 < 0);
        const bool hasPos = (c1 > 0) || (c2 > 0) || (c3 > 0) || (c4 > 0);
        return !(hasNeg && hasPos);
    };

    const Point2D mouseWorld = transform.ScreenToWorld(mousePos.x - canvasOrigin.x, mousePos.y - canvasOrigin.y);
    const double mouseDx = mouseWorld.x - m_centerWorld.x;
    const double mouseDy = mouseWorld.y - m_centerWorld.y;
    const double mouseDv = mouseDx * vx + mouseDy * vy;
    const bool isCenterMoveZone = std::abs(mouseDv) < (halfWidth - 8.0);

    const bool isHoveredBody = PointInQuad(mousePos, sTL, sTR, sBR, sBL);
    const bool isHoveredBodyCenter = isHoveredBody && isCenterMoveZone;
    const float distToDial = std::hypot(mousePos.x - sC.x, mousePos.y - sC.y);
    const float distToLeftHandle = std::hypot(mousePos.x - handleLeft.x, mousePos.y - handleLeft.y);
    const float distToRightHandle = std::hypot(mousePos.x - handleRight.x, mousePos.y - handleRight.y);

    const bool isHoveredDial = (distToDial <= dialRadiusPx);
    const bool isHoveredHandleLeft = (distToLeftHandle <= handleRadiusPx);
    const bool isHoveredHandleRight = (distToRightHandle <= handleRadiusPx);

    // Capture mouse in ImGui when interacting with the ruler body or handles to prevent stray canvas ink
    if (isHoveredDial || isHoveredHandleLeft || isHoveredHandleRight || isHoveredBodyCenter || m_dragMode != DragMode::None) {
        io.WantCaptureMouse = true;
    }

    if (isHoveredDial || isHoveredHandleLeft || isHoveredHandleRight ||
        m_dragMode == DragMode::RotateDial || m_dragMode == DragMode::RotateHandleLeft || m_dragMode == DragMode::RotateHandleRight) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (isHoveredBodyCenter || m_dragMode == DragMode::MoveBody) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // 5. Interactive Manipulation (Drag & Rotate State Machine)
    if (io.MouseClicked[0]) {
        if (isHoveredDial) {
            m_dragMode = DragMode::RotateDial;
            m_dragStartMouse = mousePos;
            m_dragStartAngleDeg = m_angleDeg;
            m_dragInitialMouseAngle = std::atan2(mousePos.y - sC.y, mousePos.x - sC.x) * (180.0 / M_PI);
        } else if (isHoveredHandleLeft) {
            m_dragMode = DragMode::RotateHandleLeft;
            m_dragStartMouse = mousePos;
            m_dragStartAngleDeg = m_angleDeg;
            m_dragInitialMouseAngle = std::atan2(mousePos.y - sC.y, mousePos.x - sC.x) * (180.0 / M_PI);
        } else if (isHoveredHandleRight) {
            m_dragMode = DragMode::RotateHandleRight;
            m_dragStartMouse = mousePos;
            m_dragStartAngleDeg = m_angleDeg;
            m_dragInitialMouseAngle = std::atan2(mousePos.y - sC.y, mousePos.x - sC.x) * (180.0 / M_PI);
        } else if (isHoveredBodyCenter) {
            m_dragMode = DragMode::MoveBody;
            m_dragStartMouse = mousePos;
            m_dragStartCenter = m_centerWorld;
        }
    }

    if (m_dragMode != DragMode::None) {
        if (io.MouseDown[0]) {
            if (m_dragMode == DragMode::MoveBody) {
                // Translation
                const float dxPx = mousePos.x - m_dragStartMouse.x;
                const float dyPx = mousePos.y - m_dragStartMouse.y;
                const Point2D dWorld = transform.ScreenDeltaToWorldDelta(dxPx, dyPx);
                m_centerWorld.x = m_dragStartCenter.x + dWorld.x;
                m_centerWorld.y = m_dragStartCenter.y + dWorld.y;
            } else {
                // Rotation
                const double currentMouseAngle = std::atan2(mousePos.y - sC.y, mousePos.x - sC.x) * (180.0 / M_PI);
                const double deltaAngle = currentMouseAngle - m_dragInitialMouseAngle;
                double newAngle = NormalizeAngleDeg(m_dragStartAngleDeg + deltaAngle);

                // Hold Shift to disable snap; otherwise snap to 15° increments when within 2.0°
                if (!io.KeyShift) {
                    newAngle = SnapAngleToIncrements(newAngle, 15.0, 2.0);
                }
                m_angleDeg = newAngle;
            }
        } else {
            m_dragMode = DragMode::None;
        }
    }

    // Mouse Wheel Rotation over Ruler body
    if (isHoveredBody && io.MouseWheel != 0.0f && m_dragMode == DragMode::None) {
        const double step = io.KeyShift ? 5.0 : 1.0;
        double newAngle = NormalizeAngleDeg(m_angleDeg + io.MouseWheel * step);
        if (!io.KeyShift) {
            newAngle = SnapAngleToIncrements(newAngle, 15.0, 2.0);
        }
        m_angleDeg = newAngle;
    }

    // 6. Aesthetic Theme Colors (Frosted Glass Acrylic)
    const ImU32 colShadow     = IM_COL32(0, 0, 0, isDarkMode ? 70 : 45);
    const ImU32 colBody       = isDarkMode ? IM_COL32(24, 28, 38, 228) : IM_COL32(250, 252, 255, 232);
    const ImU32 colBorder     = isDarkMode ? IM_COL32(75, 85, 105, 200) : IM_COL32(180, 190, 205, 220);
    const ImU32 colAccentEdge = IM_COL32(0, 120, 212, 180); // Precision blue line
    const ImU32 colTickMajor  = isDarkMode ? IM_COL32(230, 235, 245, 230) : IM_COL32(40, 50, 65, 230);
    const ImU32 colTickMinor  = isDarkMode ? IM_COL32(140, 150, 170, 160) : IM_COL32(130, 140, 155, 160);
    const ImU32 colText       = isDarkMode ? IM_COL32(240, 245, 255, 255) : IM_COL32(30, 40, 55, 255);
    const ImU32 colPillBg     = isDarkMode ? IM_COL32(15, 18, 26, 240) : IM_COL32(255, 255, 255, 245);

    // Pass A: Soft Drop Shadow
    const ImVec2 shadowOff(3.0f, 4.0f);
    drawList->AddQuadFilled(
        ImVec2(sTL.x + shadowOff.x, sTL.y + shadowOff.y),
        ImVec2(sTR.x + shadowOff.x, sTR.y + shadowOff.y),
        ImVec2(sBR.x + shadowOff.x, sBR.y + shadowOff.y),
        ImVec2(sBL.x + shadowOff.x, sBL.y + shadowOff.y),
        colShadow
    );

    // Pass B: Main Ruler Glassmorphism Body
    drawList->AddQuadFilled(sTL, sTR, sBR, sBL, colBody);
    drawList->AddQuad(sTL, sTR, sBR, sBL, colBorder, 1.5f);

    // Pass C: Precision Straightedge Guide Lines (Top & Bottom)
    drawList->AddLine(sTL, sTR, colAccentEdge, 1.8f);
    drawList->AddLine(sBL, sBR, colAccentEdge, 1.8f);

    // 7. Measurement Markings (Ticks & Labels)
    const double pxPerMm = transform.GetEffectiveScale();

    if (m_unit == RulerUnit::Centimeters) {
        // Metric: Centimeters & Millimeters
        const int totalMm = static_cast<int>(std::floor(m_lengthMm));
        const float tickMajorLenPx = (std::min)(16.0f, static_cast<float>(8.0 * pxPerMm));
        const float tickMedLenPx   = (std::min)(10.0f, static_cast<float>(5.0 * pxPerMm));
        const float tickMinorLenPx = (std::min)(6.0f,  static_cast<float>(3.0 * pxPerMm));

        for (int m = 0; m <= totalMm; ++m) {
            const double frac = static_cast<double>(m) / m_lengthMm;
            // Screen point on top straightedge
            const ImVec2 ptTop(sTL.x + static_cast<float>(frac) * (sTR.x - sTL.x),
                               sTL.y + static_cast<float>(frac) * (sTR.y - sTL.y));
            // Screen point on bottom straightedge
            const ImVec2 ptBot(sBL.x + static_cast<float>(frac) * (sBR.x - sBL.x),
                               sBL.y + static_cast<float>(frac) * (sBR.y - sBL.y));

            const bool isCm = (m % 10 == 0);
            const bool isHalfCm = (m % 5 == 0);

            const float curTickLen = isCm ? tickMajorLenPx : (isHalfCm ? tickMedLenPx : tickMinorLenPx);
            const ImU32 curTickCol = isCm ? colTickMajor : colTickMinor;

            // Draw tick mark extending inward from top edge (-vScreen)
            drawList->AddLine(ptTop, ImVec2(ptTop.x - vScreen.x * curTickLen, ptTop.y - vScreen.y * curTickLen), curTickCol, isCm ? 1.4f : 1.0f);
            // Draw tick mark extending inward from bottom edge (+vScreen)
            drawList->AddLine(ptBot, ImVec2(ptBot.x + vScreen.x * curTickLen, ptBot.y + vScreen.y * curTickLen), curTickCol, isCm ? 1.4f : 1.0f);

            // Numeric centimeter labels on major ticks (e.g. 0, 1, 2, 3...)
            if (isCm && lenPx > 180.0f) {
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", m / 10);
                const ImVec2 txtSize = ImGui::CalcTextSize(lbl);

                // Top label position slightly below tick
                const ImVec2 txtTopPos(ptTop.x - vScreen.x * (curTickLen + txtSize.y * 0.7f) - txtSize.x * 0.5f,
                                       ptTop.y - vScreen.y * (curTickLen + txtSize.y * 0.7f) - txtSize.y * 0.5f);
                drawList->AddText(txtTopPos, colText, lbl);
            }
        }
    } else {
        // Imperial: Inches & Fractional Subdivisions
        const double totalInches = m_lengthMm / 25.4;
        const int numInches = static_cast<int>(std::floor(totalInches));
        const float tickMajorLenPx = (std::min)(18.0f, static_cast<float>(8.0 * pxPerMm));
        const float tickHalfLenPx  = (std::min)(13.0f, static_cast<float>(6.0 * pxPerMm));
        const float tickQuarterPx  = (std::min)(9.0f,  static_cast<float>(4.5 * pxPerMm));
        const float tickEighthPx   = (std::min)(6.0f,  static_cast<float>(3.0 * pxPerMm));

        const int totalSixteenths = static_cast<int>(std::floor(totalInches * 16.0));
        for (int sixteenth = 0; sixteenth <= totalSixteenths; ++sixteenth) {
            const double frac = (static_cast<double>(sixteenth) * (25.4 / 16.0)) / m_lengthMm;
            if (frac > 1.0) break;

            const ImVec2 ptTop(sTL.x + static_cast<float>(frac) * (sTR.x - sTL.x),
                               sTL.y + static_cast<float>(frac) * (sTR.y - sTL.y));
            const ImVec2 ptBot(sBL.x + static_cast<float>(frac) * (sBR.x - sBL.x),
                               sBL.y + static_cast<float>(frac) * (sBR.y - sBL.y));

            float curTickLen = tickEighthPx;
            ImU32 curTickCol = colTickMinor;
            const bool isInch = (sixteenth % 16 == 0);
            const bool isHalf = (sixteenth % 8 == 0);
            const bool isQuarter = (sixteenth % 4 == 0);

            if (isInch) {
                curTickLen = tickMajorLenPx;
                curTickCol = colTickMajor;
            } else if (isHalf) {
                curTickLen = tickHalfLenPx;
                curTickCol = colTickMajor;
            } else if (isQuarter) {
                curTickLen = tickQuarterPx;
            }

            drawList->AddLine(ptTop, ImVec2(ptTop.x - vScreen.x * curTickLen, ptTop.y - vScreen.y * curTickLen), curTickCol, isInch ? 1.4f : 1.0f);
            drawList->AddLine(ptBot, ImVec2(ptBot.x + vScreen.x * curTickLen, ptBot.y + vScreen.y * curTickLen), curTickCol, isInch ? 1.4f : 1.0f);

            if (isInch && lenPx > 180.0f) {
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d\"", sixteenth / 16);
                const ImVec2 txtSize = ImGui::CalcTextSize(lbl);
                const ImVec2 txtTopPos(ptTop.x - vScreen.x * (curTickLen + txtSize.y * 0.7f) - txtSize.x * 0.5f,
                                       ptTop.y - vScreen.y * (curTickLen + txtSize.y * 0.7f) - txtSize.y * 0.5f);
                drawList->AddText(txtTopPos, colText, lbl);
            }
        }
    }

    // 8. End Rotation Handles (Grip Circles)
    const ImU32 colHandleFill = isHoveredHandleLeft || isHoveredHandleRight ?
                                (isDarkMode ? IM_COL32(0, 120, 212, 220) : IM_COL32(0, 120, 212, 200)) :
                                (isDarkMode ? IM_COL32(35, 42, 56, 210)  : IM_COL32(235, 240, 248, 210));
    drawList->AddCircleFilled(handleLeft, handleRadiusPx, colHandleFill);
    drawList->AddCircle(handleLeft, handleRadiusPx, colBorder, 0, 1.5f);
    drawList->AddCircleFilled(handleRight, handleRadiusPx, colHandleFill);
    drawList->AddCircle(handleRight, handleRadiusPx, colBorder, 0, 1.5f);

    // Grip icons inside handles (circular arrow / rotation hint)
    drawList->AddText(ImVec2(handleLeft.x - 4.0f, handleLeft.y - 7.0f), colText, "↻");
    drawList->AddText(ImVec2(handleRight.x - 4.0f, handleRight.y - 7.0f), colText, "↺");

    // 9. Center Protractor & Angle HUD Dial
    const ImU32 colDialBg = isHoveredDial ? (isDarkMode ? IM_COL32(0, 120, 212, 80) : IM_COL32(0, 120, 212, 50)) : colPillBg;
    drawList->AddCircleFilled(sC, dialRadiusPx, colDialBg);
    drawList->AddCircle(sC, dialRadiusPx, isHoveredDial ? IM_COL32(0, 120, 212, 255) : colBorder, 0, 1.8f);

    // Compass indicator needle from center pointing in uScreen direction
    drawList->AddLine(sC, ImVec2(sC.x + uScreen.x * (dialRadiusPx - 4.0f), sC.y + uScreen.y * (dialRadiusPx - 4.0f)), IM_COL32(0, 120, 212, 255), 2.0f);
    drawList->AddCircleFilled(sC, 3.5f, IM_COL32(0, 120, 212, 255));

    // Digital Angle Readout Pill in Center
    char angleStr[32];
    std::snprintf(angleStr, sizeof(angleStr), "%.1f°", std::abs(m_angleDeg));
    const ImVec2 angleSize = ImGui::CalcTextSize(angleStr);
    const float pillW = angleSize.x + 16.0f;
    const float pillH = angleSize.y + 8.0f;
    const ImVec2 pillPos(sC.x - pillW * 0.5f, sC.y - dialRadiusPx - pillH - 6.0f);

    drawList->AddRectFilled(pillPos, ImVec2(pillPos.x + pillW, pillPos.y + pillH), colPillBg, 6.0f);
    drawList->AddRect(pillPos, ImVec2(pillPos.x + pillW, pillPos.y + pillH), colBorder, 6.0f, 0, 1.0f);
    drawList->AddText(ImVec2(pillPos.x + 8.0f, pillPos.y + 4.0f), colText, angleStr);

    // Clicking angle pill resets to 0°
    if (io.MouseClicked[0] &&
        mousePos.x >= pillPos.x && mousePos.x <= pillPos.x + pillW &&
        mousePos.y >= pillPos.y && mousePos.y <= pillPos.y + pillH) {
        m_angleDeg = (m_angleDeg == 0.0) ? 90.0 : 0.0;
    }

    // 10. Interactive Buttons (Unit Toggle & Dismiss)
    // Unit Toggle Pill ("cm" / "in")
    const char* unitStr = (m_unit == RulerUnit::Centimeters) ? "cm" : "in";
    const ImVec2 unitSize = ImGui::CalcTextSize(unitStr);
    const float unitW = unitSize.x + 14.0f;
    const float unitH = unitSize.y + 6.0f;
    const ImVec2 unitPos(sC.x - dialRadiusPx - unitW - 8.0f, sC.y - unitH * 0.5f);

    const bool isHoveredUnit = (mousePos.x >= unitPos.x && mousePos.x <= unitPos.x + unitW &&
                                mousePos.y >= unitPos.y && mousePos.y <= unitPos.y + unitH);
    drawList->AddRectFilled(unitPos, ImVec2(unitPos.x + unitW, unitPos.y + unitH),
                            isHoveredUnit ? IM_COL32(0, 120, 212, 180) : colPillBg, 4.0f);
    drawList->AddRect(unitPos, ImVec2(unitPos.x + unitW, unitPos.y + unitH), colBorder, 4.0f, 0, 1.0f);
    drawList->AddText(ImVec2(unitPos.x + 7.0f, unitPos.y + 3.0f), isHoveredUnit ? IM_COL32(255, 255, 255, 255) : colText, unitStr);

    if (isHoveredUnit && io.MouseClicked[0]) {
        ToggleUnit();
    }

    // Dismiss Button ("✕")
    const float closeSize = 18.0f;
    const ImVec2 closePos(sC.x + dialRadiusPx + 8.0f, sC.y - closeSize * 0.5f);
    const bool isHoveredClose = (mousePos.x >= closePos.x && mousePos.x <= closePos.x + closeSize &&
                                 mousePos.y >= closePos.y && mousePos.y <= closePos.y + closeSize);
    drawList->AddRectFilled(closePos, ImVec2(closePos.x + closeSize, closePos.y + closeSize),
                            isHoveredClose ? IM_COL32(220, 53, 69, 220) : colPillBg, 4.0f);
    drawList->AddRect(closePos, ImVec2(closePos.x + closeSize, closePos.y + closeSize), colBorder, 4.0f, 0, 1.0f);
    drawList->AddText(ImVec2(closePos.x + 5.0f, closePos.y + 2.0f), isHoveredClose ? IM_COL32(255, 255, 255, 255) : colText, "✕");

    if (isHoveredClose && io.MouseClicked[0]) {
        m_enabled = false;
    }

    // 11. Live Inking Length Gauge Badge
    // When the user is drawing along a snapped edge, render a glowing distance badge near the cursor
    if (m_isDrawingAlongEdge && m_activeDrawnLengthMm > 0.0) {
        char distBuf[32];
        if (m_unit == RulerUnit::Centimeters) {
            std::snprintf(distBuf, sizeof(distBuf), "%.1f cm", m_activeDrawnLengthMm / 10.0);
        } else {
            std::snprintf(distBuf, sizeof(distBuf), "%.2f in", m_activeDrawnLengthMm / 25.4);
        }

        const ImVec2 tagSize = ImGui::CalcTextSize(distBuf);
        const float tagPadX = 8.0f;
        const float tagPadY = 4.0f;
        const ImVec2 tagPos(mousePos.x + 18.0f, mousePos.y - tagSize.y - 12.0f);

        drawList->AddRectFilled(
            tagPos,
            ImVec2(tagPos.x + tagSize.x + tagPadX * 2.0f, tagPos.y + tagSize.y + tagPadY * 2.0f),
            IM_COL32(0, 120, 212, 235),
            6.0f
        );
        drawList->AddText(
            ImVec2(tagPos.x + tagPadX, tagPos.y + tagPadY),
            IM_COL32(255, 255, 255, 255),
            distBuf
        );
    }
}

} // namespace Folio
