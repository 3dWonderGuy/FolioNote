#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <blend2d/blend2d.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "core/spatial/aabb.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "core/engine/canvas_transform.hpp"
#include "core/engine/gizmo_types.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

/**
 * @brief Hit-test result indicating whether a click intersected a gizmo handle or the body.
 */
struct GizmoHitResult {
    bool hit = false;
    HandleRole role = HandleRole::None;
    int customId = 0;
};

/**
 * @brief Interactive Blend2D-powered selection manipulator for canvas objects.
 * 
 * Provides:
 * 1. Automatic 8-point bounding box with corner & edge resize grips + 1 rotation stem.
 * 2. Real-time interactive translation, scaling (with anchor pinning), and rotation.
 * 3. Rotated bounding box during active rotation with automatic snap-back to AABB on release.
 * 4. Dual-mode rotation: 45° angle snapping when cursor is close to box, continuous precision when further out.
 * 5. High-fidelity Blend2D visual cues: rotation pivot, snap circle, 45° ticks, sweep arc, and degree HUD badge.
 * 6. Extensible hooks for custom object handles (e.g. SmartArrow endpoints, vertex editing).
 * 7. Crisp screen-space rendering via Blend2D on the canvas composite layer.
 */
class SelectionGizmo {
public:
    std::vector<std::shared_ptr<CanvasObject>> selectedObjects;
    AABB bounds{0.0, 0.0, 0.0, 0.0};
    bool hasSelection = false;

    [[nodiscard]] bool HasSelection() const noexcept { return hasSelection; }

    // Grid lock / snapping configuration
    bool lockToGrid = false;          ///< When true, snaps coordinates to gridSpacingMm during move/resize
    double gridSpacingMm = 5.0;       ///< Grid interval in world millimeters (from background paper grid)

    /**
     * @brief Determines whether the current selection is eligible for grid snapping.
     *
     * Mathematical & Behavioral Rules:
     * 1. lockToGrid must be active and gridSpacingMm > 0.001 mm.
     * 2. Ink strokes (ObjectType::InkContainer) are explicitly excluded: freehand strokes must
     *    never be snapped to a rigid grid when moved or resized.
     * 3. Geometric vector shapes, text boxes, images, connectors, and other structured objects
     *    snap cleanly to paper grid intervals.
     *
     * @return true if grid snapping should be applied to the active selection, false otherwise.
     */
    [[nodiscard]] bool ShouldSnapToGrid() const noexcept {
        if (!lockToGrid || gridSpacingMm <= 0.001) return false;
        for (const auto& obj : selectedObjects) {
            if (obj && obj->type == ObjectType::InkContainer) {
                return false; // Ink strokes bypass grid lock
            }
        }
        return true;
    }

    // Active drag state
    bool isDragging = false;
    HandleRole activeRole = HandleRole::None;
    int activeCustomId = 0;

    // Rotation state & dynamic snapping
    double currentRotationAngle = 0.0;    // Current rotation delta in radians
    double displayAngleDeg = 0.0;         // Normalized degrees in [-180, 180]
    bool isAngleSnapped = false;          // True when cursor is within snap threshold radius
    float currentSnapRadius = 0.0f;       // Screen pixels threshold between 45° snap and free rotation
    Point2D currentPointerScreen{0.0, 0.0};

    // Drag origin snapshots for stable delta calculation
    Point2D dragStartScreen{0.0, 0.0};
    Point2D dragStartWorld{0.0, 0.0};
    Point2D lastDragWorld{0.0, 0.0};
    AABB initialBounds{0.0, 0.0, 0.0, 0.0};
    Point2D initialCenterWorld{0.0, 0.0};

    // Stored initial object matrices to avoid accumulating numeric drift during drag
    struct ObjectInitialState {
        uint32_t uid = 0;
        BLMatrix2D initialTransform = BLMatrix2D::make_identity();
        AABB initialBounds;
    };
    std::vector<ObjectInitialState> initialStates;
    std::unordered_map<uint32_t, std::shared_ptr<CanvasObject>> initialClones; ///< Deep clones for undo/redo

    // Cached HUD font for degree readout
    mutable BLFont hudFont;
    mutable bool fontAttempted = false;

    // Visual constants (in screen pixels)
    static constexpr float HANDLE_RADIUS = 5.0f;        // Visual half-size
    static constexpr float HIT_RADIUS = 12.0f;          // Generous touch/pen hit target
    static constexpr float ROTATION_ARM_LENGTH = 26.0f;  // Distance above top edge for rotation knob

    SelectionGizmo() = default;

    /**
     * @brief Sets the list of actively selected objects and calculates their collective bounding box.
     */
    void SetSelectedObjects(const std::vector<std::shared_ptr<CanvasObject>>& objects) {
        selectedObjects.clear();
        std::unordered_set<std::string> activeGroupIds;

        // 1. Identify directly selected objects and their associated group IDs
        for (const auto& obj : objects) {
            if (obj && obj->isSelected && obj->IsGrouped()) {
                activeGroupIds.insert(obj->groupId);
            }
        }

        // 2. Expand selection to encompass all co-members sharing active group IDs
        for (const auto& obj : objects) {
            if (obj) {
                if (obj->isSelected || (!obj->groupId.empty() && activeGroupIds.count(obj->groupId) > 0)) {
                    obj->isSelected = 1;
                    selectedObjects.push_back(obj);
                }
            }
        }

        hasSelection = !selectedObjects.empty();
        currentRotationAngle = 0.0;
        displayAngleDeg = 0.0;
        isAngleSnapped = false;
        RecalculateBounds();
        if (hasSelection) {
            LOG_INFO(CanvasEngine, "Selection Gizmo selected " + std::to_string(selectedObjects.size()) + " object(s)");
        }
    }

    /**
     * @brief Clears the current selection and marks all objects unselected.
     */
    void ClearSelection() {
        if (hasSelection) {
            LOG_INFO(CanvasEngine, "Selection Gizmo cleared selection (" + std::to_string(selectedObjects.size()) + " objects deselected)");
        }
        for (auto& obj : selectedObjects) {
            if (obj) obj->isSelected = 0;
        }
        selectedObjects.clear();
        initialClones.clear();
        hasSelection = false;
        isDragging = false;
        activeRole = HandleRole::None;
        currentRotationAngle = 0.0;
        displayAngleDeg = 0.0;
        isAngleSnapped = false;
        bounds = AABB(0.0, 0.0, 0.0, 0.0);
    }

    /**
     * @brief Recalculates the world union AABB of all selected objects.
     */
    void RecalculateBounds() {
        if (selectedObjects.empty()) {
            hasSelection = false;
            bounds = AABB(0.0, 0.0, 0.0, 0.0);
            return;
        }

        hasSelection = true;
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto& obj : selectedObjects) {
            if (!obj) continue;
            obj->UpdateBounds();
            const AABB& b = obj->bounds;
            if (b.minX <= b.maxX && b.minY <= b.maxY) {
                minX = std::min(minX, b.minX);
                minY = std::min(minY, b.minY);
                maxX = std::max(maxX, b.maxX);
                maxY = std::max(maxY, b.maxY);
            }
        }

        if (minX <= maxX && minY <= maxY) {
            bounds = AABB(minX, minY, maxX, maxY);
        } else {
            bounds = AABB(0.0, 0.0, 0.0, 0.0);
        }
    }

    /**
     * @brief Renders the selection frame, handles, and rotation pin via Blend2D in screen coordinates.
     */
    void Render(BLContext& ctx, const CanvasTransform& transform) const {
        if (!hasSelection || selectedObjects.empty()) return;

        // Check if single selected object provides custom handles and custom rendering
        if (selectedObjects.size() == 1) {
            std::vector<GizmoHandle> customHandles;
            if (selectedObjects[0]->GetCustomGizmoHandles(customHandles, transform)) {
                // Let the object draw its custom selection graphics if desired
                selectedObjects[0]->RenderCustomSelection(ctx, transform);

                // Render custom handles in screen space
                for (const auto& h : customHandles) {
                    Point2D screen = transform.WorldToScreen(h.worldPos.x, h.worldPos.y);
                    DrawHandle(ctx, static_cast<float>(screen.x), static_cast<float>(screen.y), false);
                }
                return;
            }
        }

        ctx.save();

        BLRgba32 fillCol(0x00, 0x78, 0xD4, 0x18);   // Soft fluent accent blue 10%
        BLRgba32 borderCol(0x00, 0x78, 0xD4, 0xD8); // Crisp accent blue

        // If actively rotating, render rotated bounding box and handles + visual cues
        if (activeRole == HandleRole::Rotation && isDragging) {
            auto RotateWorld = [&](double wx, double wy) -> Point2D {
                double cosA = std::cos(currentRotationAngle);
                double sinA = std::sin(currentRotationAngle);
                double dx = wx - initialCenterWorld.x;
                double dy = wy - initialCenterWorld.y;
                return { initialCenterWorld.x + dx * cosA - dy * sinA,
                         initialCenterWorld.y + dx * sinA + dy * cosA };
            };

            // 4 Rotated world corners
            Point2D rc0 = RotateWorld(initialBounds.minX, initialBounds.minY); // TL
            Point2D rc1 = RotateWorld(initialBounds.maxX, initialBounds.minY); // TR
            Point2D rc2 = RotateWorld(initialBounds.maxX, initialBounds.maxY); // BR
            Point2D rc3 = RotateWorld(initialBounds.minX, initialBounds.maxY); // BL

            // Project corners to screen coords
            Point2D sc0 = transform.WorldToScreen(rc0.x, rc0.y);
            Point2D sc1 = transform.WorldToScreen(rc1.x, rc1.y);
            Point2D sc2 = transform.WorldToScreen(rc2.x, rc2.y);
            Point2D sc3 = transform.WorldToScreen(rc3.x, rc3.y);

            // 1. Draw Rotated Bounding Box
            BLPath boxPath;
            boxPath.move_to(sc0.x, sc0.y);
            boxPath.line_to(sc1.x, sc1.y);
            boxPath.line_to(sc2.x, sc2.y);
            boxPath.line_to(sc3.x, sc3.y);
            boxPath.close();

            ctx.fill_path(boxPath, fillCol);
            ctx.set_stroke_style(borderCol);
            ctx.set_stroke_width(1.5);
            ctx.stroke_path(boxPath);

            // Midpoints for edge resize grips
            Point2D sm01 = { (sc0.x + sc1.x) * 0.5, (sc0.y + sc1.y) * 0.5 }; // TopCenter
            Point2D sm12 = { (sc1.x + sc2.x) * 0.5, (sc1.y + sc2.y) * 0.5 }; // RightCenter
            Point2D sm23 = { (sc2.x + sc3.x) * 0.5, (sc2.y + sc3.y) * 0.5 }; // BottomCenter
            Point2D sm30 = { (sc3.x + sc0.x) * 0.5, (sc3.y + sc0.y) * 0.5 }; // LeftCenter

            // Compute outward normal from top edge for rotation stem & knob
            float edgeX = static_cast<float>(sc1.x - sc0.x);
            float edgeY = static_cast<float>(sc1.y - sc0.y);
            float edgeLen = std::hypot(edgeX, edgeY);
            if (edgeLen < 0.001f) edgeLen = 1.0f;
            float nx = edgeY / edgeLen;
            float ny = -edgeX / edgeLen;

            float stemStartX = static_cast<float>(sm01.x);
            float stemStartY = static_cast<float>(sm01.y);
            float knobX = stemStartX + nx * ROTATION_ARM_LENGTH;
            float knobY = stemStartY + ny * ROTATION_ARM_LENGTH;

            // Rotation stem line
            ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xB0));
            ctx.set_stroke_width(1.2);
            ctx.stroke_line(stemStartX, stemStartY, knobX, knobY);

            // 8 Rotated handles
            DrawHandle(ctx, static_cast<float>(sc0.x),  static_cast<float>(sc0.y),  false);
            DrawHandle(ctx, static_cast<float>(sm01.x), static_cast<float>(sm01.y), false);
            DrawHandle(ctx, static_cast<float>(sc1.x),  static_cast<float>(sc1.y),  false);
            DrawHandle(ctx, static_cast<float>(sm12.x), static_cast<float>(sm12.y), false);
            DrawHandle(ctx, static_cast<float>(sc2.x),  static_cast<float>(sc2.y),  false);
            DrawHandle(ctx, static_cast<float>(sm23.x), static_cast<float>(sm23.y), false);
            DrawHandle(ctx, static_cast<float>(sc3.x),  static_cast<float>(sc3.y),  false);
            DrawHandle(ctx, static_cast<float>(sm30.x), static_cast<float>(sm30.y), false);

            // Rotation circular knob
            ctx.fill_circle(knobX, knobY, 6.0, BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
            ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xFF));
            ctx.set_stroke_width(2.0);
            ctx.stroke_circle(knobX, knobY, 6.0);
            ctx.fill_circle(knobX, knobY, 2.5, BLRgba32(0x00, 0x78, 0xD4, 0xFF));

            // 2. Draw Rotation Visual Cues & Degree HUD
            RenderRotationVisualCues(ctx, transform, knobX, knobY);
            ctx.restore();
            return;
        }

        // Custom handles rendering if single selected object provides them (e.g. Line & Arrow draggable endpoints)
        if (selectedObjects.size() == 1) {
            std::vector<GizmoHandle> customHandles;
            if (selectedObjects[0]->GetCustomGizmoHandles(customHandles, transform)) {
                for (const auto& h : customHandles) {
                    Point2D screen = transform.WorldToScreen(h.worldPos.x, h.worldPos.y);
                    float hx = static_cast<float>(screen.x);
                    float hy = static_cast<float>(screen.y);
                    bool isHandleActive = (activeRole == HandleRole::Custom && activeCustomId == h.customId);
                    DrawHandle(ctx, hx, hy, isHandleActive);
                }
                ctx.restore();
                return;
            }
        }

        // Standard Axis-Aligned 8-point Bounding Box Rendering
        Point2D sMin = transform.WorldToScreen(bounds.minX, bounds.minY);
        Point2D sMax = transform.WorldToScreen(bounds.maxX, bounds.maxY);

        float left   = static_cast<float>(std::min(sMin.x, sMax.x));
        float top    = static_cast<float>(std::min(sMin.y, sMax.y));
        float right  = static_cast<float>(std::max(sMin.x, sMax.x));
        float bottom = static_cast<float>(std::max(sMin.y, sMax.y));
        float midX   = (left + right) * 0.5f;
        float midY   = (top + bottom) * 0.5f;
        float width  = right - left;
        float height = bottom - top;

        if (width <= 0.0f && height <= 0.0f) {
            ctx.restore();
            return;
        }

        // 1. Semi-transparent selection fill
        ctx.fill_rect(left, top, width, height, fillCol);

        // 2. Bounding box border
        ctx.set_stroke_style(borderCol);
        ctx.set_stroke_width(1.5);
        ctx.stroke_rect(left, top, width, height);

        // 3. Rotation stem line and knob
        float rotStemY = top - ROTATION_ARM_LENGTH;
        ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xB0));
        ctx.set_stroke_width(1.2);
        ctx.stroke_line(midX, top, midX, rotStemY);

        // Rotation circle knob
        ctx.fill_circle(midX, rotStemY, 5.5, BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xFF));
        ctx.set_stroke_width(1.8);
        ctx.stroke_circle(midX, rotStemY, 5.5);
        ctx.fill_circle(midX, rotStemY, 2.0, BLRgba32(0x00, 0x78, 0xD4, 0xFF)); // Center indicator

        // 4. Eight resize handles
        DrawHandle(ctx, left,  top,    false); // TopLeft
        DrawHandle(ctx, midX,  top,    false); // TopCenter
        DrawHandle(ctx, right, top,    false); // TopRight
        DrawHandle(ctx, right, midY,   false); // RightCenter
        DrawHandle(ctx, right, bottom, false); // BottomRight
        DrawHandle(ctx, midX,  bottom, false); // BottomCenter
        DrawHandle(ctx, left,  bottom, false); // BottomLeft
        DrawHandle(ctx, left,  midY,   false); // LeftCenter

        ctx.restore();
    }

    /**
     * @brief Hit-tests the gizmo at the given screen pixel coordinates.
     */
    GizmoHitResult HitTest(float screenX, float screenY, const CanvasTransform& transform) const {
        GizmoHitResult res;
        if (!hasSelection || selectedObjects.empty()) return res;

        // Custom handles hit-test if single object provides them
        if (selectedObjects.size() == 1) {
            std::vector<GizmoHandle> customHandles;
            if (selectedObjects[0]->GetCustomGizmoHandles(customHandles, transform)) {
                for (const auto& h : customHandles) {
                    Point2D screen = transform.WorldToScreen(h.worldPos.x, h.worldPos.y);
                    float dx = screenX - static_cast<float>(screen.x);
                    float dy = screenY - static_cast<float>(screen.y);
                    if ((dx * dx + dy * dy) <= (HIT_RADIUS * HIT_RADIUS)) {
                        res.hit = true;
                        res.role = HandleRole::Custom;
                        res.customId = h.customId;
                        return res;
                    }
                }
            }
        }

        // Standard 8-point Bounding Box Hit-Testing
        Point2D sMin = transform.WorldToScreen(bounds.minX, bounds.minY);
        Point2D sMax = transform.WorldToScreen(bounds.maxX, bounds.maxY);

        float left   = static_cast<float>(std::min(sMin.x, sMax.x));
        float top    = static_cast<float>(std::min(sMin.y, sMax.y));
        float right  = static_cast<float>(std::max(sMin.x, sMax.x));
        float bottom = static_cast<float>(std::max(sMin.y, sMax.y));
        float midX   = (left + right) * 0.5f;
        float midY   = (top + bottom) * 0.5f;

        auto HitCircle = [&](float cx, float cy) -> bool {
            float dx = screenX - cx;
            float dy = screenY - cy;
            return (dx * dx + dy * dy) <= (HIT_RADIUS * HIT_RADIUS);
        };

        // 1. Rotation knob
        if (HitCircle(midX, top - ROTATION_ARM_LENGTH)) {
            res.hit = true;
            res.role = HandleRole::Rotation;
            return res;
        }

        // 2. Corner and edge handles
        if (HitCircle(left,  top))    { res.hit = true; res.role = HandleRole::TopLeft;      return res; }
        if (HitCircle(midX,  top))    { res.hit = true; res.role = HandleRole::TopCenter;    return res; }
        if (HitCircle(right, top))    { res.hit = true; res.role = HandleRole::TopRight;     return res; }
        if (HitCircle(right, midY))   { res.hit = true; res.role = HandleRole::RightCenter;  return res; }
        if (HitCircle(right, bottom)) { res.hit = true; res.role = HandleRole::BottomRight;  return res; }
        if (HitCircle(midX,  bottom)) { res.hit = true; res.role = HandleRole::BottomCenter; return res; }
        if (HitCircle(left,  bottom)) { res.hit = true; res.role = HandleRole::BottomLeft;   return res; }
        if (HitCircle(left,  midY))   { res.hit = true; res.role = HandleRole::LeftCenter;   return res; }

        // 3. Interior body (move)
        const float PADDING = 4.0f;
        if (screenX >= (left - PADDING) && screenX <= (right + PADDING) &&
            screenY >= (top - PADDING)  && screenY <= (bottom + PADDING)) {
            res.hit = true;
            res.role = HandleRole::Body;
            return res;
        }

        return res;
    }

    /**
     * @brief Begins an interactive transform drag if a handle or the body was hit.
     *
     * @param screenX Pointer X in viewport screen pixels.
     * @param screenY Pointer Y in viewport screen pixels.
     * @param transform Coordinate conversion between screen pixels and world millimeters.
     * @param inLockToGrid Whether grid lock is currently enabled on canvas.
     * @param inGridSpacingMm Background paper grid spacing in millimeters.
     * @return true if a handle or body was hit and drag began, false otherwise.
     */
    bool OnPointerDown(float screenX, float screenY, const CanvasTransform& transform,
                       bool inLockToGrid = false, double inGridSpacingMm = 5.0) {
        if (!hasSelection) return false;

        lockToGrid = inLockToGrid;
        gridSpacingMm = inGridSpacingMm;

        GizmoHitResult hit = HitTest(screenX, screenY, transform);
        if (!hit.hit) return false;

        isDragging = true;
        activeRole = hit.role;
        activeCustomId = hit.customId;

        const char* roleStr = (activeRole == HandleRole::Body) ? "Body Move" :
                              (activeRole == HandleRole::Rotation) ? "Rotation" : "Resize Handle";
        LOG_INFO(CanvasEngine, "Selection Gizmo drag started (operation=" + std::string(roleStr) +
                 ", handleRole=" + std::to_string(static_cast<int>(activeRole)) +
                 ", selectedCount=" + std::to_string(selectedObjects.size()) +
                 ", gridLock=" + (ShouldSnapToGrid() ? "ON" : "OFF") + ")");

        dragStartScreen = { screenX, screenY };
        dragStartWorld = transform.ScreenToWorld(screenX, screenY);
        lastDragWorld = dragStartWorld;
        initialBounds = bounds;
        initialCenterWorld = { (bounds.minX + bounds.maxX) * 0.5, (bounds.minY + bounds.maxY) * 0.5 };

        currentRotationAngle = 0.0;
        displayAngleDeg = 0.0;
        isAngleSnapped = false;
        currentPointerScreen = { screenX, screenY };

        // Compute initial snap radius
        Point2D sMin = transform.WorldToScreen(initialBounds.minX, initialBounds.minY);
        Point2D sMax = transform.WorldToScreen(initialBounds.maxX, initialBounds.maxY);
        float boxHalfDiag = 0.5f * std::hypot(static_cast<float>(sMax.x - sMin.x), static_cast<float>(sMax.y - sMin.y));
        currentSnapRadius = std::max(boxHalfDiag + 45.0f, 100.0f);

        // Snapshot initial transforms & deep clones for transactional undo
        initialStates.clear();
        initialClones.clear();
        for (const auto& obj : selectedObjects) {
            if (obj) {
                initialStates.push_back({ obj->uid, obj->transform, obj->bounds });
                initialClones[obj->uid] = std::shared_ptr<CanvasObject>(obj->Clone().release());
            }
        }

        return true;
    }

    /**
     * @brief Updates the transformation during an active drag with optional grid locking.
     *
     * Mathematical Process:
     * - When ShouldSnapToGrid() is true:
     *   * Move: Target bounds top-left snaps to nearest grid multiple:
     *     snapped = round((initMin + delta) / G) * G.
     *     Translation applied from initialStates snapshots, avoiding numeric drift.
     *   * Resize: Active handle world coordinate snaps to grid line:
     *     snappedHandle = round((initHandle + delta) / G) * G.
     *     Dimensions scale relative to fixed opposing anchor, guaranteeing integer cell widths.
     *   * Custom handles: Target endpoint coordinate snaps to nearest grid vertex.
     * - When ShouldSnapToGrid() is false (or ink stroke selected):
     *   * Free floating subpixel continuous manipulation with aspect-ratio corner preservation.
     */
    bool OnPointerMove(float screenX, float screenY, const CanvasTransform& transform,
                       bool inLockToGrid = false, double inGridSpacingMm = 5.0) {
        if (!isDragging || selectedObjects.empty()) return false;

        lockToGrid = inLockToGrid;
        gridSpacingMm = inGridSpacingMm;

        Point2D currentWorld = transform.ScreenToWorld(screenX, screenY);

        // 1. Move (Translation)
        if (activeRole == HandleRole::Body) {
            if (ShouldSnapToGrid()) {
                // Snap top-left of the bounding box to the nearest paper grid intersection
                double targetX = initialBounds.minX + (currentWorld.x - dragStartWorld.x);
                double targetY = initialBounds.minY + (currentWorld.y - dragStartWorld.y);
                double snappedX = std::round(targetX / gridSpacingMm) * gridSpacingMm;
                double snappedY = std::round(targetY / gridSpacingMm) * gridSpacingMm;
                double totalDx = snappedX - initialBounds.minX;
                double totalDy = snappedY - initialBounds.minY;

                BLMatrix2D trans = BLMatrix2D::make_translation(totalDx, totalDy);
                for (size_t i = 0; i < selectedObjects.size(); ++i) {
                    if (i < initialStates.size() && selectedObjects[i]) {
                        selectedObjects[i]->transform = initialStates[i].initialTransform;
                        selectedObjects[i]->ApplyTransform(trans);
                    }
                }
                lastDragWorld = currentWorld;
                RecalculateBounds();
            } else {
                double dx = currentWorld.x - lastDragWorld.x;
                double dy = currentWorld.y - lastDragWorld.y;
                if (dx != 0.0 || dy != 0.0) {
                    BLMatrix2D trans = BLMatrix2D::make_translation(dx, dy);
                    for (auto& obj : selectedObjects) {
                        if (obj) obj->ApplyTransform(trans);
                    }
                    lastDragWorld = currentWorld;
                    RecalculateBounds();
                }
            }
            return true;
        }

        // 2. Rotation (Dual-Mode: 45° Snapping when close, continuous when far)
        if (activeRole == HandleRole::Rotation) {
            currentPointerScreen = { screenX, screenY };

            Point2D centerScreen = transform.WorldToScreen(initialCenterWorld.x, initialCenterWorld.y);
            float mouseDistScreen = std::hypot(screenX - static_cast<float>(centerScreen.x),
                                               screenY - static_cast<float>(centerScreen.y));

            // Dynamic snap radius based on bounding box size
            Point2D sMin = transform.WorldToScreen(initialBounds.minX, initialBounds.minY);
            Point2D sMax = transform.WorldToScreen(initialBounds.maxX, initialBounds.maxY);
            float boxHalfDiag = 0.5f * std::hypot(static_cast<float>(sMax.x - sMin.x), static_cast<float>(sMax.y - sMin.y));
            currentSnapRadius = std::max(boxHalfDiag + 45.0f, 100.0f);

            double startAngle = std::atan2(dragStartWorld.y - initialCenterWorld.y, dragStartWorld.x - initialCenterWorld.x);
            double currAngle  = std::atan2(currentWorld.y - initialCenterWorld.y, currentWorld.x - initialCenterWorld.x);
            double rawDeltaAngle = currAngle - startAngle;

            // Handle branch cut [-PI, PI]
            while (rawDeltaAngle > M_PI) rawDeltaAngle -= 2.0 * M_PI;
            while (rawDeltaAngle < -M_PI) rawDeltaAngle += 2.0 * M_PI;

            // Close to box: locks rotation to 45 degree increments
            // Moving mouse a bit out: full continuous precision control
            if (mouseDistScreen < currentSnapRadius) {
                isAngleSnapped = true;
                double rawDeg = rawDeltaAngle * (180.0 / M_PI);
                double snappedDeg = std::round(rawDeg / 45.0) * 45.0;
                currentRotationAngle = snappedDeg * (M_PI / 180.0);
                displayAngleDeg = snappedDeg;
            } else {
                isAngleSnapped = false;
                currentRotationAngle = rawDeltaAngle;
                displayAngleDeg = rawDeltaAngle * (180.0 / M_PI);
            }

            while (displayAngleDeg > 180.0) displayAngleDeg -= 360.0;
            while (displayAngleDeg <= -180.0) displayAngleDeg += 360.0;

            BLMatrix2D rotMatrix = BLMatrix2D::make_identity();
            rotMatrix.post_translate(-initialCenterWorld.x, -initialCenterWorld.y);
            rotMatrix.post_rotate(currentRotationAngle);
            rotMatrix.post_translate(initialCenterWorld.x, initialCenterWorld.y);

            // Apply to each object from its initial state snapshot
            for (size_t i = 0; i < selectedObjects.size(); ++i) {
                if (i < initialStates.size() && selectedObjects[i]) {
                    selectedObjects[i]->transform = initialStates[i].initialTransform;
                    selectedObjects[i]->ApplyTransform(rotMatrix);
                }
            }
            // Do NOT recalculate AABB bounds during active rotation; box rotates along with contents!
            return true;
        }

        // 3. Scaling / Stretching (8 Bounding Box Grips)
        if (activeRole >= HandleRole::TopLeft && activeRole <= HandleRole::LeftCenter) {
            double initW = initialBounds.maxX - initialBounds.minX;
            double initH = initialBounds.maxY - initialBounds.minY;
            if (initW <= 0.001) initW = 1.0;
            if (initH <= 0.001) initH = 1.0;

            // Determine fixed anchor point opposite to the active handle
            Point2D anchor;
            Point2D handleInit;
            bool scaleX = true;
            bool scaleY = true;

            switch (activeRole) {
                case HandleRole::TopLeft:
                    anchor = { initialBounds.maxX, initialBounds.maxY };
                    handleInit = { initialBounds.minX, initialBounds.minY };
                    break;
                case HandleRole::TopCenter:
                    anchor = { (initialBounds.minX + initialBounds.maxX) * 0.5, initialBounds.maxY };
                    handleInit = { anchor.x, initialBounds.minY };
                    scaleX = false;
                    break;
                case HandleRole::TopRight:
                    anchor = { initialBounds.minX, initialBounds.maxY };
                    handleInit = { initialBounds.maxX, initialBounds.minY };
                    break;
                case HandleRole::RightCenter:
                    anchor = { initialBounds.minX, (initialBounds.minY + initialBounds.maxY) * 0.5 };
                    handleInit = { initialBounds.maxX, anchor.y };
                    scaleY = false;
                    break;
                case HandleRole::BottomRight:
                    anchor = { initialBounds.minX, initialBounds.minY };
                    handleInit = { initialBounds.maxX, initialBounds.maxY };
                    break;
                case HandleRole::BottomCenter:
                    anchor = { (initialBounds.minX + initialBounds.maxX) * 0.5, initialBounds.minY };
                    handleInit = { anchor.x, initialBounds.maxY };
                    scaleX = false;
                    break;
                case HandleRole::BottomLeft:
                    anchor = { initialBounds.maxX, initialBounds.minY };
                    handleInit = { initialBounds.minX, initialBounds.maxY };
                    break;
                case HandleRole::LeftCenter:
                    anchor = { initialBounds.maxX, (initialBounds.minY + initialBounds.maxY) * 0.5 };
                    handleInit = { initialBounds.minX, anchor.y };
                    scaleY = false;
                    break;
                default:
                    return false;
            }

            double sx = 1.0;
            double sy = 1.0;

            if (ShouldSnapToGrid()) {
                // Snap moving handle coordinates directly to paper grid intervals
                double rawHandleX = handleInit.x + (currentWorld.x - dragStartWorld.x);
                double rawHandleY = handleInit.y + (currentWorld.y - dragStartWorld.y);
                double snappedHandleX = std::round(rawHandleX / gridSpacingMm) * gridSpacingMm;
                double snappedHandleY = std::round(rawHandleY / gridSpacingMm) * gridSpacingMm;

                if (scaleX) {
                    double signedDistX = snappedHandleX - anchor.x;
                    if (handleInit.x < anchor.x) signedDistX = -signedDistX;
                    signedDistX = (std::max)(gridSpacingMm, signedDistX); // Clamped to minimum 1 grid unit
                    sx = std::clamp(signedDistX / initW, 0.05, 50.0);
                }

                if (scaleY) {
                    double signedDistY = snappedHandleY - anchor.y;
                    if (handleInit.y < anchor.y) signedDistY = -signedDistY;
                    signedDistY = (std::max)(gridSpacingMm, signedDistY); // Clamped to minimum 1 grid unit
                    sy = std::clamp(signedDistY / initH, 0.05, 50.0);
                }
            } else {
                if (scaleX) {
                    // Vector along X from anchor to current cursor vs anchor to initial handle
                    double signedDistX = currentWorld.x - anchor.x;
                    if (dragStartWorld.x < anchor.x) signedDistX = -signedDistX;
                    sx = std::clamp(signedDistX / initW, 0.05, 50.0);
                }

                if (scaleY) {
                    // Vector along Y from anchor to current cursor vs anchor to initial handle
                    double signedDistY = currentWorld.y - anchor.y;
                    if (dragStartWorld.y < anchor.y) signedDistY = -signedDistY;
                    sy = std::clamp(signedDistY / initH, 0.05, 50.0);
                }

                // Aspect-Ratio Lock for Corner Grips (TopLeft, TopRight, BottomRight, BottomLeft)
                // When both scaleX and scaleY are active, it is a corner handle.
                // Ratio lock ensures the width-to-height ratio does not change while sizing.
                if (scaleX && scaleY) {
                    // Uniform scale factor preserving aspect ratio without axis flipping or runaway jumps
                    double s = 0.5 * (sx + sy);
                    s = std::clamp(s, 0.05, 50.0);
                    sx = s;
                    sy = s;
                }
            }

            BLMatrix2D scaleMatrix = BLMatrix2D::make_identity();
            scaleMatrix.post_translate(-anchor.x, -anchor.y);
            scaleMatrix.post_scale(sx, sy);
            scaleMatrix.post_translate(anchor.x, anchor.y);

            // Apply to each object from initial snapshot
            for (size_t i = 0; i < selectedObjects.size(); ++i) {
                if (i < initialStates.size() && selectedObjects[i]) {
                    selectedObjects[i]->transform = initialStates[i].initialTransform;
                    selectedObjects[i]->ApplyTransform(scaleMatrix);
                }
            }
            RecalculateBounds();
            return true;
        }

        // 4. Custom Handle Drag (Delegated to Object, e.g. SmartArrow endpoints)
        if (activeRole == HandleRole::Custom && selectedObjects.size() == 1) {
            Point2D effectiveWorld = currentWorld;
            if (ShouldSnapToGrid()) {
                effectiveWorld.x = std::round(effectiveWorld.x / gridSpacingMm) * gridSpacingMm;
                effectiveWorld.y = std::round(effectiveWorld.y / gridSpacingMm) * gridSpacingMm;
            }
            Point2D worldDelta = { effectiveWorld.x - lastDragWorld.x, effectiveWorld.y - lastDragWorld.y };
            if (selectedObjects[0]->OnGizmoHandleDrag(activeCustomId, effectiveWorld, worldDelta)) {
                lastDragWorld = effectiveWorld;
                RecalculateBounds();
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Finalizes the active drag operation. Snaps rotated box back to axis-aligned AABB.
     * @param session Optional DocumentSession pointer to record TransformObjectsCommand for undo/redo.
     */
    void OnPointerUp(DocumentSession* session = nullptr) {
        if (!isDragging) return;
        const char* roleStr = (activeRole == HandleRole::Body) ? "Body Move" :
                              (activeRole == HandleRole::Rotation) ? "Rotation" : "Resize Handle";
        LOG_INFO(CanvasEngine, "Selection Gizmo drag completed (operation=" + std::string(roleStr) +
                 ", finalBounds=[" + std::to_string(bounds.minX) + ", " + std::to_string(bounds.minY) +
                 " to " + std::to_string(bounds.maxX) + ", " + std::to_string(bounds.maxY) + "])");

        // Bake transforms into intrinsic coordinates for objects that support it (e.g. ShapeObject)
        for (auto& obj : selectedObjects) {
            if (obj) {
                obj->BakeTransform();
            }
        }

        // Record transformation command into page history for undo/redo
        if (session && !initialClones.empty()) {
            auto activePage = session->GetActivePage();
            if (activePage) {
                std::vector<Folio::TransformObjectsCommand::Entry> entries;
                for (const auto& obj : selectedObjects) {
                    if (!obj) continue;
                    auto it = initialClones.find(obj->uid);
                    if (it != initialClones.end()) {
                        entries.push_back({
                            obj->uid,
                            it->second,
                            std::shared_ptr<CanvasObject>(obj->Clone().release())
                        });
                    }
                }
                if (!entries.empty()) {
                    activePage->history.RecordCommand(std::make_unique<Folio::TransformObjectsCommand>(std::move(entries)));
                    activePage->isModified = true;
                }
            }
        }
        initialClones.clear();

        isDragging = false;
        activeRole = HandleRole::None;
        currentRotationAngle = 0.0;
        displayAngleDeg = 0.0;
        isAngleSnapped = false;
        initialStates.clear();
        RecalculateBounds(); // Snaps selection box back to axis-aligned bounding box!
    }

private:
    /**
     * @brief Renders the visual cues during rotation: center pivot, snap ring, 45° ticks, sweep arc, and degree HUD badge.
     */
    void RenderRotationVisualCues(BLContext& ctx, const CanvasTransform& transform, float knobX, float knobY) const {
        Point2D centerScreen = transform.WorldToScreen(initialCenterWorld.x, initialCenterWorld.y);
        float cx = static_cast<float>(centerScreen.x);
        float cy = static_cast<float>(centerScreen.y);
        float snapR = currentSnapRadius;

        // 1. Center of Rotation Pivot (Crosshair + concentric circles)
        ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xAA));
        ctx.set_stroke_width(1.2);
        ctx.stroke_line(cx - 7.0f, cy, cx + 7.0f, cy);
        ctx.stroke_line(cx, cy - 7.0f, cx, cy + 7.0f);
        ctx.fill_circle(cx, cy, 3.5, BLRgba32(0x00, 0x78, 0xD4, 0xFF));
        ctx.set_stroke_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.set_stroke_width(1.0);
        ctx.stroke_circle(cx, cy, 3.5);

        // 2. Snap Circle Guide
        // Soft tint inside snap region
        ctx.fill_circle(cx, cy, snapR, BLRgba32(0x00, 0x78, 0xD4, isAngleSnapped ? 0x0E : 0x05));
        
        // Ring boundary
        ctx.set_stroke_style(isAngleSnapped ? BLRgba32(0x00, 0xA4, 0xEF, 0x90) : BLRgba32(0x00, 0x78, 0xD4, 0x40));
        ctx.set_stroke_width(isAngleSnapped ? 1.5 : 1.0);
        ctx.stroke_circle(cx, cy, snapR);

        // 3. 45-degree Radial Ticks on the Snap Ring
        Point2D unrotMidWorld = { (initialBounds.minX + initialBounds.maxX) * 0.5, initialBounds.minY };
        Point2D unrotMidScreen = transform.WorldToScreen(unrotMidWorld.x, unrotMidWorld.y);
        double baseAngle = std::atan2(unrotMidScreen.y - cy, unrotMidScreen.x - cx);

        for (int i = 0; i < 8; ++i) {
            double tickAngle = baseAngle + i * (M_PI * 0.25);
            float cosT = static_cast<float>(std::cos(tickAngle));
            float sinT = static_cast<float>(std::sin(tickAngle));

            bool isMajor = (i % 2 == 0); // 0, 90, 180, 270
            float tickInner = isMajor ? (snapR - 9.0f) : (snapR - 5.0f);
            float tickOuter = isMajor ? (snapR + 9.0f) : (snapR + 5.0f);

            // Check if current rotation matches this tick
            double angleDiff = std::remainder(currentRotationAngle - i * (M_PI * 0.25), 2.0 * M_PI);
            bool isActiveTick = isAngleSnapped && (std::abs(angleDiff) < 0.08);

            if (isActiveTick) {
                // Ray from center to snapped tick
                ctx.set_stroke_style(BLRgba32(0x00, 0xD2, 0xFF, 0x70));
                ctx.set_stroke_width(1.5);
                ctx.stroke_line(cx, cy, cx + cosT * snapR, cy + sinT * snapR);

                // Snapped tick mark highlight
                ctx.set_stroke_style(BLRgba32(0x00, 0xD2, 0xFF, 0xFF));
                ctx.set_stroke_width(2.5);
                ctx.stroke_line(cx + cosT * tickInner, cy + sinT * tickInner,
                                cx + cosT * tickOuter, cy + sinT * tickOuter);
                ctx.fill_circle(cx + cosT * snapR, cy + sinT * snapR, 3.5, BLRgba32(0x00, 0xD2, 0xFF, 0xFF));
            } else {
                ctx.set_stroke_style(isMajor ? BLRgba32(0xFF, 0xFF, 0xFF, 0x80) : BLRgba32(0xFF, 0xFF, 0xFF, 0x38));
                ctx.set_stroke_width(isMajor ? 1.5 : 1.0);
                ctx.stroke_line(cx + cosT * tickInner, cy + sinT * tickInner,
                                cx + cosT * tickOuter, cy + sinT * tickOuter);
            }
        }

        // 4. Angle Arc Sweep (from 0 to currentRotationAngle)
        float arcRadius = std::min(snapR * 0.45f, 44.0f);
        if (std::abs(currentRotationAngle) > 0.01) {
            ctx.set_stroke_style(isAngleSnapped ? BLRgba32(0x00, 0xD2, 0xFF, 0xDD) : BLRgba32(0x00, 0x78, 0xD4, 0xBB));
            ctx.set_stroke_width(2.0);
            ctx.stroke_arc(cx, cy, arcRadius, baseAngle, currentRotationAngle);
        }

        // 5. Connecting line from center to active rotation knob
        ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0x50));
        ctx.set_stroke_width(1.0);
        ctx.stroke_line(cx, cy, knobX, knobY);

        // 6. Degree HUD Badge
        EnsureFontLoaded();

        char textBuf[64];
        if (isAngleSnapped) {
            snprintf(textBuf, sizeof(textBuf), "%d°", static_cast<int>(std::round(displayAngleDeg)));
        } else {
            snprintf(textBuf, sizeof(textBuf), "%.1f°", displayAngleDeg);
        }

        float badgeW = isAngleSnapped ? 46.0f : 56.0f;
        float badgeH = 22.0f;
        float badgeX = knobX + 16.0f;
        float badgeY = knobY - 26.0f;

        // Background pill
        ctx.fill_round_rect(badgeX, badgeY, badgeW, badgeH, 5.0, 5.0, BLRgba32(0x18, 0x1B, 0x22, 0xF4));
        ctx.set_stroke_style(isAngleSnapped ? BLRgba32(0x00, 0xD2, 0xFF, 0xFF) : BLRgba32(0x45, 0x55, 0x68, 0xCC));
        ctx.set_stroke_width(isAngleSnapped ? 1.5 : 1.0);
        ctx.stroke_round_rect(badgeX, badgeY, badgeW, badgeH, 5.0, 5.0);

        if (hudFont.is_valid()) {
            float textX = badgeX + (isAngleSnapped ? 8.0f : 6.0f);
            float textY = badgeY + 15.5f;
            ctx.fill_utf8_text(BLPoint(textX, textY), hudFont, textBuf, SIZE_MAX, BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
            if (isAngleSnapped) {
                // Snap indicator dot
                ctx.fill_circle(badgeX + badgeW - 9.0f, badgeY + 11.0f, 2.5, BLRgba32(0x00, 0xD2, 0xFF, 0xFF));
            }
        } else {
            DrawFallbackText(ctx, badgeX + 8.0f, badgeY + 6.0f, textBuf);
        }
    }

    /**
     * @brief Tries to lazily load a clean font for the HUD degree display across all operating systems.
     */
    void EnsureFontLoaded() const {
        if (fontAttempted) return;
        fontAttempted = true;

        const char* candidatePaths[] = {
            "assets/fonts/Roboto-Medium.ttf",
            "../assets/fonts/Roboto-Medium.ttf",
            "../../assets/fonts/Roboto-Medium.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/arial.ttf",
            "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/system/fonts/Roboto-Regular.ttf"
        };

        BLFontFace face;
        for (const char* p : candidatePaths) {
            if (face.create_from_file(p) == BL_SUCCESS) {
                hudFont.create_from_face(face, 12.0f);
                break;
            }
        }
    }

public:
    /**
     * @brief Robust vector stroke text fallback for numerals, letters, and symbols.
     */
    static void DrawFallbackText(BLContext& ctx, float x, float y, const char* str) {
        ctx.set_stroke_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.set_stroke_width(1.2);
        float curX = x;
        for (const char* p = str; *p; ++p) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                int d = c - '0';
                static const uint8_t segs[10] = {
                    0b00111111, // 0
                    0b00000110, // 1
                    0b01011011, // 2
                    0b01001111, // 3
                    0b01100110, // 4
                    0b01101101, // 5
                    0b01111101, // 6
                    0b00000111, // 7
                    0b01111111, // 8
                    0b01101111  // 9
                };
                uint8_t s = segs[d];
                float w = 5.0f, h = 9.0f, mh = 4.5f;
                if (s & (1 << 0)) ctx.stroke_line(curX, y, curX + w, y);
                if (s & (1 << 1)) ctx.stroke_line(curX + w, y, curX + w, y + mh);
                if (s & (1 << 2)) ctx.stroke_line(curX + w, y + mh, curX + w, y + h);
                if (s & (1 << 3)) ctx.stroke_line(curX, y + h, curX + w, y + h);
                if (s & (1 << 4)) ctx.stroke_line(curX, y + mh, curX, y + h);
                if (s & (1 << 5)) ctx.stroke_line(curX, y, curX, y + mh);
                if (s & (1 << 6)) ctx.stroke_line(curX, y + mh, curX + w, y + mh);
                curX += w + 2.5f;
            } else if (c == '-') {
                ctx.stroke_line(curX, y + 4.5f, curX + 4.0f, y + 4.5f);
                curX += 6.0f;
            } else if (c == '.') {
                ctx.fill_circle(curX + 1.0f, y + 8.5f, 0.8, BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
                curX += 3.5f;
            } else if (c == ':') {
                ctx.fill_circle(curX + 1.0f, y + 2.5f, 0.8, BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
                ctx.fill_circle(curX + 1.0f, y + 6.5f, 0.8, BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
                curX += 3.5f;
            } else if (c == '#') {
                ctx.stroke_line(curX + 1.5f, y, curX + 1.5f, y + 9);
                ctx.stroke_line(curX + 4.0f, y, curX + 4.0f, y + 9);
                ctx.stroke_line(curX, y + 3.0f, curX + 5.5f, y + 3.0f);
                ctx.stroke_line(curX, y + 6.0f, curX + 5.5f, y + 6.0f);
                curX += 7.0f;
            } else if (c == '[') {
                ctx.stroke_line(curX + 2.5f, y, curX, y);
                ctx.stroke_line(curX, y, curX, y + 9);
                ctx.stroke_line(curX, y + 9, curX + 2.5f, y + 9);
                curX += 4.5f;
            } else if (c == ']') {
                ctx.stroke_line(curX, y, curX + 2.5f, y);
                ctx.stroke_line(curX + 2.5f, y, curX + 2.5f, y + 9);
                ctx.stroke_line(curX + 2.5f, y + 9, curX, y + 9);
                curX += 4.5f;
            } else if (c == 'x') {
                ctx.stroke_line(curX, y + 3.0f, curX + 4.5f, y + 9.0f);
                ctx.stroke_line(curX + 4.5f, y + 3.0f, curX, y + 9.0f);
                curX += 6.0f;
            } else if (c == ' ') {
                curX += 4.0f;
            } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
                DrawChar(ctx, curX, y, upper);
                curX += 6.5f;
            } else {
                ctx.stroke_circle(curX + 2.0f, y + 2.0f, 1.5);
                curX += 5.0f;
            }
        }
    }

private:
    static void DrawChar(BLContext& ctx, float x, float y, char c) {
        float w = 4.5f, h = 9.0f, mh = 4.5f;
        switch (c) {
            case 'A':
                ctx.stroke_line(x, y + h, x + w * 0.5f, y);
                ctx.stroke_line(x + w * 0.5f, y, x + w, y + h);
                ctx.stroke_line(x + 1.0f, y + mh, x + w - 1.0f, y + mh);
                break;
            case 'B':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y, x + w, y + 2.0f);
                ctx.stroke_line(x + w, y + 2.0f, x, y + mh);
                ctx.stroke_line(x, y + mh, x + w, y + h - 2.0f);
                ctx.stroke_line(x + w, y + h - 2.0f, x, y + h);
                break;
            case 'C':
                ctx.stroke_line(x + w, y, x, y);
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y + h, x + w, y + h);
                break;
            case 'D':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y, x + w - 1.0f, y + mh);
                ctx.stroke_line(x + w - 1.0f, y + mh, x, y + h);
                break;
            case 'E':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y, x + w, y);
                ctx.stroke_line(x, y + mh, x + w - 1.0f, y + mh);
                ctx.stroke_line(x, y + h, x + w, y + h);
                break;
            case 'F':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y, x + w, y);
                ctx.stroke_line(x, y + mh, x + w - 1.0f, y + mh);
                break;
            case 'G':
                ctx.stroke_line(x + w, y, x, y);
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y + h, x + w, y + h);
                ctx.stroke_line(x + w, y + h, x + w, y + mh);
                ctx.stroke_line(x + w, y + mh, x + mh, y + mh);
                break;
            case 'H':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x + w, y, x + w, y + h);
                ctx.stroke_line(x, y + mh, x + w, y + mh);
                break;
            case 'I':
                ctx.stroke_line(x + w * 0.5f, y, x + w * 0.5f, y + h);
                ctx.stroke_line(x, y, x + w, y);
                ctx.stroke_line(x, y + h, x + w, y + h);
                break;
            case 'K':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x + w, y, x, y + mh);
                ctx.stroke_line(x, y + mh, x + w, y + h);
                break;
            case 'L':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y + h, x + w, y + h);
                break;
            case 'M':
                ctx.stroke_line(x, y + h, x, y);
                ctx.stroke_line(x, y, x + w * 0.5f, y + mh);
                ctx.stroke_line(x + w * 0.5f, y + mh, x + w, y);
                ctx.stroke_line(x + w, y, x + w, y + h);
                break;
            case 'N':
                ctx.stroke_line(x, y + h, x, y);
                ctx.stroke_line(x, y, x + w, y + h);
                ctx.stroke_line(x + w, y + h, x + w, y);
                break;
            case 'O':
                ctx.stroke_rect(x, y, w, h);
                break;
            case 'P':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y, x + w, y);
                ctx.stroke_line(x + w, y, x + w, y + mh);
                ctx.stroke_line(x + w, y + mh, x, y + mh);
                break;
            case 'R':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y, x + w, y);
                ctx.stroke_line(x + w, y, x + w, y + mh);
                ctx.stroke_line(x + w, y + mh, x, y + mh);
                ctx.stroke_line(x, y + mh, x + w, y + h);
                break;
            case 'S':
                ctx.stroke_line(x + w, y, x, y);
                ctx.stroke_line(x, y, x, y + mh);
                ctx.stroke_line(x, y + mh, x + w, y + mh);
                ctx.stroke_line(x + w, y + mh, x + w, y + h);
                ctx.stroke_line(x + w, y + h, x, y + h);
                break;
            case 'T':
                ctx.stroke_line(x, y, x + w, y);
                ctx.stroke_line(x + w * 0.5f, y, x + w * 0.5f, y + h);
                break;
            case 'U':
                ctx.stroke_line(x, y, x, y + h);
                ctx.stroke_line(x, y + h, x + w, y + h);
                ctx.stroke_line(x + w, y + h, x + w, y);
                break;
            case 'V':
                ctx.stroke_line(x, y, x + w * 0.5f, y + h);
                ctx.stroke_line(x + w * 0.5f, y + h, x + w, y);
                break;
            default:
                ctx.stroke_rect(x, y, w, h);
                break;
        }
    }

    static void DrawHandle(BLContext& ctx, float x, float y, bool isRound) {
        if (isRound) {
            ctx.fill_circle(x, y, HANDLE_RADIUS, BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
            ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xFF));
            ctx.set_stroke_width(1.5);
            ctx.stroke_circle(x, y, HANDLE_RADIUS);
        } else {
            float sz = HANDLE_RADIUS * 2.0f;
            float hx = x - HANDLE_RADIUS;
            float hy = y - HANDLE_RADIUS;
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
            ctx.fill_round_rect(hx, hy, sz, sz, 1.5, 1.5);
            ctx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xFF));
            ctx.set_stroke_width(1.5);
            ctx.stroke_round_rect(hx, hy, sz, sz, 1.5, 1.5);
        }
    }
};
