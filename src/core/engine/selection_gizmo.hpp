#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <blend2d/blend2d.h>

#include "core/spatial/aabb.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "core/engine/canvas_transform.hpp"
#include "core/engine/gizmo_types.hpp"
#include "core/objects/canvas_object.hpp"

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
 * 3. Extensible hooks for custom object handles (e.g. SmartArrow endpoints, vertex editing).
 * 4. Crisp screen-space rendering via Blend2D on the canvas composite layer.
 */
class SelectionGizmo {
public:
    std::vector<std::shared_ptr<CanvasObject>> selectedObjects;
    AABB bounds{0.0, 0.0, 0.0, 0.0};
    bool hasSelection = false;

    [[nodiscard]] bool HasSelection() const noexcept { return hasSelection; }

    // Active drag state
    bool isDragging = false;
    HandleRole activeRole = HandleRole::None;
    int activeCustomId = 0;

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

    // Visual constants (in screen pixels)
    static constexpr float HANDLE_RADIUS = 5.0f;       // Visual half-size
    static constexpr float HIT_RADIUS = 12.0f;         // Generous touch/pen hit target
    static constexpr float ROTATION_ARM_LENGTH = 26.0f; // Distance above top edge for rotation knob

    SelectionGizmo() = default;

    /**
     * @brief Sets the list of actively selected objects and calculates their collective bounding box.
     */
    void SetSelectedObjects(const std::vector<std::shared_ptr<CanvasObject>>& objects) {
        selectedObjects.clear();
        for (const auto& obj : objects) {
            if (obj && obj->isSelected) {
                selectedObjects.push_back(obj);
            }
        }
        hasSelection = !selectedObjects.empty();
        RecalculateBounds();
    }

    /**
     * @brief Clears the current selection and marks all objects unselected.
     */
    void ClearSelection() {
        for (auto& obj : selectedObjects) {
            if (obj) obj->isSelected = 0;
        }
        selectedObjects.clear();
        hasSelection = false;
        isDragging = false;
        activeRole = HandleRole::None;
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

        // Standard 8-point Bounding Box Rendering
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

        if (width <= 0.0f && height <= 0.0f) return;

        ctx.save();

        // 1. Semi-transparent selection fill
        BLRgba32 fillCol(0x00, 0x78, 0xD4, 0x18); // Soft fluent accent blue 10%
        ctx.fill_rect(left, top, width, height, fillCol);

        // 2. Bounding box border
        BLRgba32 borderCol(0x00, 0x78, 0xD4, 0xD8); // Crisp accent blue
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
     */
    bool OnPointerDown(float screenX, float screenY, const CanvasTransform& transform) {
        if (!hasSelection) return false;

        GizmoHitResult hit = HitTest(screenX, screenY, transform);
        if (!hit.hit) return false;

        isDragging = true;
        activeRole = hit.role;
        activeCustomId = hit.customId;

        dragStartScreen = { screenX, screenY };
        dragStartWorld = transform.ScreenToWorld(screenX, screenY);
        lastDragWorld = dragStartWorld;
        initialBounds = bounds;
        initialCenterWorld = { (bounds.minX + bounds.maxX) * 0.5, (bounds.minY + bounds.maxY) * 0.5 };

        // Snapshot initial transforms
        initialStates.clear();
        for (const auto& obj : selectedObjects) {
            if (obj) {
                initialStates.push_back({ obj->uid, obj->transform, obj->bounds });
            }
        }

        return true;
    }

    /**
     * @brief Updates the transformation during an active drag.
     */
    bool OnPointerMove(float screenX, float screenY, const CanvasTransform& transform) {
        if (!isDragging || selectedObjects.empty()) return false;

        Point2D currentWorld = transform.ScreenToWorld(screenX, screenY);

        // 1. Move (Translation)
        if (activeRole == HandleRole::Body) {
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
            return true;
        }

        // 2. Rotation
        if (activeRole == HandleRole::Rotation) {
            double startAngle = std::atan2(dragStartWorld.y - initialCenterWorld.y, dragStartWorld.x - initialCenterWorld.x);
            double currAngle  = std::atan2(currentWorld.y - initialCenterWorld.y, currentWorld.x - initialCenterWorld.x);
            double deltaAngle = currAngle - startAngle;

            BLMatrix2D rotMatrix = BLMatrix2D::make_identity();
            rotMatrix.post_translate(-initialCenterWorld.x, -initialCenterWorld.y);
            rotMatrix.post_rotate(deltaAngle);
            rotMatrix.post_translate(initialCenterWorld.x, initialCenterWorld.y);

            // Apply to each object from its initial state snapshot
            for (size_t i = 0; i < selectedObjects.size(); ++i) {
                if (i < initialStates.size() && selectedObjects[i]) {
                    selectedObjects[i]->transform = initialStates[i].initialTransform;
                    selectedObjects[i]->ApplyTransform(rotMatrix);
                }
            }
            RecalculateBounds();
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
            bool scaleX = true;
            bool scaleY = true;

            switch (activeRole) {
                case HandleRole::TopLeft:
                    anchor = { initialBounds.maxX, initialBounds.maxY };
                    break;
                case HandleRole::TopCenter:
                    anchor = { (initialBounds.minX + initialBounds.maxX) * 0.5, initialBounds.maxY };
                    scaleX = false;
                    break;
                case HandleRole::TopRight:
                    anchor = { initialBounds.minX, initialBounds.maxY };
                    break;
                case HandleRole::RightCenter:
                    anchor = { initialBounds.minX, (initialBounds.minY + initialBounds.maxY) * 0.5 };
                    scaleY = false;
                    break;
                case HandleRole::BottomRight:
                    anchor = { initialBounds.minX, initialBounds.minY };
                    break;
                case HandleRole::BottomCenter:
                    anchor = { (initialBounds.minX + initialBounds.maxX) * 0.5, initialBounds.minY };
                    scaleX = false;
                    break;
                case HandleRole::BottomLeft:
                    anchor = { initialBounds.maxX, initialBounds.minY };
                    break;
                case HandleRole::LeftCenter:
                    anchor = { initialBounds.maxX, (initialBounds.minY + initialBounds.maxY) * 0.5 };
                    scaleY = false;
                    break;
                default:
                    return false;
            }

            double sx = 1.0;
            double sy = 1.0;

            if (scaleX) {
                double initDistX = std::abs(dragStartWorld.x - anchor.x);
                if (initDistX < 0.5) initDistX = initW;
                double currDistX = currentWorld.x - anchor.x;
                // Preserve direction
                if (dragStartWorld.x < anchor.x) currDistX = -currDistX;
                sx = std::max(0.05, currDistX / initDistX);
            }

            if (scaleY) {
                double initDistY = std::abs(dragStartWorld.y - anchor.y);
                if (initDistY < 0.5) initDistY = initH;
                double currDistY = currentWorld.y - anchor.y;
                if (dragStartWorld.y < anchor.y) currDistY = -currDistY;
                sy = std::max(0.05, currDistY / initDistY);
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

        // 4. Custom Handle Drag (Delegated to Object)
        if (activeRole == HandleRole::Custom && selectedObjects.size() == 1) {
            Point2D worldDelta = { currentWorld.x - lastDragWorld.x, currentWorld.y - lastDragWorld.y };
            if (selectedObjects[0]->OnGizmoHandleDrag(activeCustomId, currentWorld, worldDelta)) {
                lastDragWorld = currentWorld;
                RecalculateBounds();
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Finalizes the active drag operation.
     */
    void OnPointerUp() {
        if (!isDragging) return;
        isDragging = false;
        activeRole = HandleRole::None;
        initialStates.clear();
        RecalculateBounds();
    }

private:
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
