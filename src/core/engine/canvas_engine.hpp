#pragma once

// ANDROID: SDL_opengl.h pulls in desktop OpenGL headers which don't exist on Android.
// Android only supports OpenGL ES (GLESv2/GLESv3). We target GLES3 explicitly because:
//   - GL_RGBA8 (sized internal format) is GLES3-only
//   - GL_TEXTURE_SWIZZLE_* for B<->R channel remapping is GLES3-only
//   - VAOs (glBindVertexArray, used by ImGui) require GLES3
// SDL_opengles2.h only wraps GLES2 headers, so we include GLES3/gl3.h directly.
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include <blend2d/blend2d.h>
#include "core/objects/canvas_object.hpp"
#include "core/engine/canvas_transform.hpp"
#include "core/engine/live_layer_pipeline.hpp"
#include "core/document/document_session.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

enum class PaperStyle { Grid, Lined, Blank };

class CanvasEngine {
public:
    CanvasTransform transform;
    LiveLayerPipeline liveLayer;

    char pageTitle[128] = "Untitled page";
    std::string pageDateStr = "Tuesday, August 18, 2026";
    std::string pageTimeStr = "9:54 PM";
    PaperStyle currentPaperStyle = PaperStyle::Grid;

    // Grid spacing standard: 5.0 mm rule
    double gridSpacingMm = 5.0;

    bool isDirty = true;
    bool needsFullRebake = true;      // Background grid + all objects
    bool needsObjectRebake = false;   // Only re-stroke dirty InkContainers, skip background redraw

    BLImage staticCanvasLayer;
    BLImage liveInkingLayer;
    BLImage compositeSurface;
    GLuint glTexture = 0;
    int viewportW = 0;
    int viewportH = 0;

    // Dynamic capacity tracking to prevent repeated buffer allocations during resize
    int allocatedCapacityW = 0;
    int allocatedCapacityH = 0;

    void Init(int initialW, int initialH, float displayDpi = 96.0f) {
        transform.SetDPI(displayDpi);

        glGenTextures(1, &glTexture);
        glBindTexture(GL_TEXTURE_2D, glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        Resize(initialW, initialH);
    }

    void Resize(int width, int height) {
        if (width <= 0 || height <= 0) return;
        if (width == viewportW && height == viewportH) return;

        viewportW = width;
        viewportH = height;

#if defined(__ANDROID__)
        // ANDROID: The desktop 4K minimum buffer strategy (3840x2160 x 3 buffers x 4 bytes = ~96MB)
        // is catastrophic on mobile. It wastes RAM, causes slow texture uploads because
        // GL_UNPACK_ROW_LENGTH forces the GPU driver to stride through 3840 bytes per row
        // even for a 1440p viewport, and triggers flicker on Samsung/Qualcomm drivers.
        // On Android, screen size is fixed at boot — always allocate at exactly viewport size.
        // This makes stride == width*4, so GL_UNPACK_ROW_LENGTH is never needed.
        allocatedCapacityW = viewportW;
        allocatedCapacityH = viewportH;

        staticCanvasLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
        liveInkingLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
        compositeSurface.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);

        glBindTexture(GL_TEXTURE_2D, glTexture);
        // GL_RGBA8 + GL_RGBA are the GLES3 equivalents. Swizzle mask handles B<->R remap
        // (set once during Init). See Init() for the full explanation.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedCapacityW, allocatedCapacityH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
        // DESKTOP: Allocate buffers with headroom so small resizing deltas don't reallocate.
        if (viewportW > allocatedCapacityW || viewportH > allocatedCapacityH) {
            allocatedCapacityW = std::max(allocatedCapacityW * 2, viewportW);
            allocatedCapacityH = std::max(allocatedCapacityH * 2, viewportH);

            // Minimum buffer size of 4K to completely prevent VRAM reallocation spikes when going fullscreen
            allocatedCapacityW = std::max(allocatedCapacityW, 3840);
            allocatedCapacityH = std::max(allocatedCapacityH, 2160);

            staticCanvasLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
            liveInkingLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
            compositeSurface.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);

            glBindTexture(GL_TEXTURE_2D, glTexture);
            // DESKTOP: GL_BGRA matches Blend2D's native BL_FORMAT_PRGB32 byte layout directly.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedCapacityW, allocatedCapacityH, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        }
#endif

        isDirty = true;
        needsFullRebake = true;
    }

    void SetDPI(float dpi) noexcept {
        transform.SetDPI(dpi);
        isDirty = true;
        needsFullRebake = true;
    }

    void Pan(double screenDx, double screenDy) noexcept {
        if (screenDx == 0.0 && screenDy == 0.0) return;
        transform.PanByScreenPixels(screenDx, screenDy);
        isDirty = true;
        needsFullRebake = true;
    }

    void ZoomAt(double screenX, double screenY, double factor) noexcept {
        transform.ZoomAtScreenPoint(screenX, screenY, factor);
        isDirty = true;
        needsFullRebake = true;
    }

    [[nodiscard]] Viewport GetViewport() const noexcept {
        return transform.GetVisibleViewportMm(viewportW, viewportH);
    }

    // -------------------------------------------------------------
    // LIVE INGESTION HOOKS (Screen Px -> World mm)
    // -------------------------------------------------------------

    void OnPointerDown(float screenX, float screenY, float pressure, double timeSec, const PenTool& tool) {
        BLContext liveClear(liveInkingLayer);
        liveClear.clear_all();
        liveClear.end();

        Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
        liveLayer.BeginStroke(worldMm.x, worldMm.y, pressure, timeSec, tool);
        isDirty = true;
    }

    void OnPointerMove(float screenX, float screenY, float pressure, double timeSec) {
        Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
        liveLayer.AddStrokePoint(worldMm.x, worldMm.y, pressure, timeSec);
        isDirty = true;
    }

    // Finalizes the live stroke and hands the data to the DocumentSession
    void OnPointerUp(DocumentSession& session, const PenTool& tool) {
        FinishedStrokeData data = liveLayer.FinishStroke();

        BLContext liveClear(liveInkingLayer);
        liveClear.clear_all();
        liveClear.end();

        isDirty = true;
        needsObjectRebake = true;  // Only the newly added container needs re-stroking, background is unchanged

        // Direct handoff: Canvas -> DocumentSession (passes outlinePath + modeledPoints + segments)
        if (!data.outlinePath.is_empty() || !data.liveSegments.empty()) {
            session.CommitStroke(std::move(data), tool);
        }
    }

    void OnLassoDown(float screenX, float screenY) {
        Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
        liveLayer.BeginLasso(worldMm.x, worldMm.y);
        isDirty = true;
    }

    void OnLassoMove(float screenX, float screenY) {
        Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
        liveLayer.AddLassoPoint(worldMm.x, worldMm.y);
        isDirty = true;
    }

    std::vector<Point2D> OnLassoUp() {
        std::vector<Point2D> lasso = liveLayer.FinishLasso();
        isDirty = true;
        return lasso;
    }

    // -------------------------------------------------------------
    // RENDER PASSES (Static Layer Caching + Live Layer Composite)
    // -------------------------------------------------------------

    void Render(const std::vector<std::shared_ptr<CanvasObject>>& visibleBakedObjects) {
        if (viewportW <= 0 || viewportH <= 0) return;
        if (!isDirty && !needsFullRebake) return;

        Viewport currentView = GetViewport();
        BLMatrix2D renderMatrix = transform.GetBlend2DTransformMatrix();

        // 1. Static Baked Layer (Background grid + all visible objects)
        if (needsFullRebake) {
            BLContext staticCtx(staticCanvasLayer);
            staticCtx.clear_all();

            DrawTiledBackground(staticCtx, currentView);

            staticCtx.save();
            staticCtx.set_transform(renderMatrix);
            for (const auto& obj : visibleBakedObjects) {
                obj->Render(staticCtx, currentView);
            }
            staticCtx.restore();
            staticCtx.end();

            BLContext liveClear(liveInkingLayer);
            liveClear.clear_all();
            liveClear.end();

            needsFullRebake = false;
            needsObjectRebake = false;
        }
        // 2. Incremental object rebake: only re-stroke containers whose strokes changed.
        //    Background grid is already correct — we just composite dirty objects on top.
        else if (needsObjectRebake) {
            BLContext staticCtx(staticCanvasLayer);

            staticCtx.save();
            staticCtx.set_transform(renderMatrix);
            for (const auto& obj : visibleBakedObjects) {
                if (obj->type != ObjectType::InkContainer) continue;
                auto* ink = static_cast<const InkContainer*>(obj.get());
                if (!ink->renderDirty) continue;  // Skip clean containers
                obj->Render(staticCtx, currentView);
            }
            staticCtx.restore();
            staticCtx.end();

            needsObjectRebake = false;
        }

        // 2. Fast Composite Pass
        BLContext compCtx(compositeSurface);
        compCtx.blit_image(BLPoint(0, 0), staticCanvasLayer);

        compCtx.save();
        compCtx.set_transform(renderMatrix);

        // Draw active in-flight ink strokes as a continuous smooth closed polygon outline
        if (liveLayer.isStrokeActive) {
            compCtx.set_fill_rule(BL_FILL_RULE_NON_ZERO);
            compCtx.set_fill_style(liveLayer.activePenTool.color);

            if (!liveLayer.liveStrokeOutline.is_empty()) {
                compCtx.fill_path(liveLayer.liveStrokeOutline);
            }
            if (!liveLayer.predictedStrokeOutline.is_empty()) {
                compCtx.fill_path(liveLayer.predictedStrokeOutline);
            }
        }

        // Draw active lasso polygon trace
        if (liveLayer.isLassoActive && liveLayer.activeLassoPoints.size() >= 2) {
            BLPath lassoPath;
            lassoPath.move_to(liveLayer.activeLassoPoints[0].x, liveLayer.activeLassoPoints[0].y);
            for (size_t i = 1; i < liveLayer.activeLassoPoints.size(); ++i) {
                lassoPath.line_to(liveLayer.activeLassoPoints[i].x, liveLayer.activeLassoPoints[i].y);
            }
            lassoPath.line_to(liveLayer.activeLassoPoints[0].x, liveLayer.activeLassoPoints[0].y);

            compCtx.set_stroke_style(BLRgba32(0x99, 0xC2, 0xFF, 0x80));
            // 0.5mm cosmetic line thickness
            compCtx.set_stroke_width(0.5);
            compCtx.stroke_path(lassoPath);
        }

        compCtx.restore();
        compCtx.end();

        // 3. Upload Composite Buffer to GPU
        BLImageData imgData;
        compositeSurface.get_data(&imgData);
        glBindTexture(GL_TEXTURE_2D, glTexture);
#if defined(__ANDROID__)
        // ANDROID: Because we allocate buffers at exactly viewport size (no 4K headroom),
        // the Blend2D image stride is guaranteed to equal viewportW * 4 bytes — i.e. the
        // rows are tightly packed. This means GL_UNPACK_ROW_LENGTH is not needed and we
        // can skip it entirely. This avoids a known flicker bug on Samsung/Qualcomm drivers
        // where a stride mismatch between GL_UNPACK_ROW_LENGTH and the actual upload width
        // causes partial row reads, producing horizontal tearing during pan.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewportW, viewportH, GL_RGBA, GL_UNSIGNED_BYTE, imgData.pixel_data);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#else
        // DESKTOP: Buffers are preallocated at 4K capacity. GL_UNPACK_ROW_LENGTH tells
        // the driver to skip (allocatedCapacityW - viewportW) bytes at the end of each row.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewportW, viewportH, GL_BGRA, GL_UNSIGNED_BYTE, imgData.pixel_data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#endif

        isDirty = false;
    }

private:
    void DrawTiledBackground(BLContext& ctx, const Viewport& currentView) {
        // Base canvas paper tone
        ctx.fill_all(BLRgba32(0x10, 0x10, 0x12));

        if (currentPaperStyle == PaperStyle::Blank) {
            return;
        }

        const double stepMm = gridSpacingMm;
        const double scale = transform.GetEffectiveScale();

        // Screen-space margin boundary
        double screenOriginX = std::max(0.0, transform.panXMm * scale);
        double screenOriginY = std::max(0.0, transform.panYMm * scale);

        ctx.save();
        ctx.clip_to_rect(screenOriginX, screenOriginY, viewportW - screenOriginX, viewportH - screenOriginY);

        ctx.set_stroke_style(BLRgba32(0x1E, 0x22, 0x2A));
        ctx.set_stroke_width(1.0); // 1px thin cosmetic line

        // Grid snapping lines calculated directly from visible world coordinates
        double startX = std::floor(currentView.bounds.minX / stepMm) * stepMm;
        double endX   = std::ceil(currentView.bounds.maxX / stepMm) * stepMm;
        double startY = std::floor(currentView.bounds.minY / stepMm) * stepMm;
        double endY   = std::ceil(currentView.bounds.maxY / stepMm) * stepMm;

        // Draw vertical grid lines
        if (currentPaperStyle == PaperStyle::Grid) {
            for (double wx = startX; wx <= endX; wx += stepMm) {
                if (wx < 0.0) continue;
                Point2D sTop = transform.WorldToScreen(wx, currentView.bounds.minY);
                Point2D sBot = transform.WorldToScreen(wx, currentView.bounds.maxY);
                ctx.stroke_line(sTop.x, sTop.y, sBot.x, sBot.y);
            }
        }

        // Draw horizontal grid / ruled lines
        for (double wy = startY; wy <= endY; wy += stepMm) {
            if (wy < 0.0) continue;
            Point2D sLeft  = transform.WorldToScreen(currentView.bounds.minX, wy);
            Point2D sRight = transform.WorldToScreen(currentView.bounds.maxX, wy);
            ctx.stroke_line(sLeft.x, sLeft.y, sRight.x, sRight.y);
        }

        ctx.restore();
    }
};