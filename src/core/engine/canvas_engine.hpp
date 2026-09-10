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
#include "core/objects/ink_container.hpp"
#include "core/engine/canvas_transform.hpp"
#include "core/engine/live_layer_pipeline.hpp"
#include "core/engine/selection_gizmo.hpp"
#include "core/document/document_session.hpp"
#include "utils/usage_tracker.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

enum class PaperStyle { Grid, Lined, Blank, Dotted };
enum class PageBorderType { Automatic, Fixed };
enum class PageBorderStyle { Continuous, Dashed, Corners };
enum class PageSizeFormat { Letter, A4, A3, A5, Custom };

struct PageTemplateDefaults {
    PaperStyle paperStyle = PaperStyle::Grid;
    double gridSpacingMm = 5.0;
    BLRgba32 normalBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
    BLRgba32 invertedBgColor = BLRgba32(0x1E, 0x20, 0x26);
    BLRgba32 normalLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
    BLRgba32 invertedLineColor = BLRgba32(0x34, 0x38, 0x44);
    bool showBorder = false;
    BLRgba32 borderColor = BLRgba32(0xD0, 0xD4, 0xDC);
    double borderWidth = 1.5;
    PageBorderType borderType = PageBorderType::Automatic;
    PageBorderStyle borderStyle = PageBorderStyle::Continuous;
    PageSizeFormat pageSizeFormat = PageSizeFormat::Letter;
    bool pageIsLandscape = false;
    CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity;
    double calibrationDpi = 96.0;
};

class CanvasEngine {
public:
    CanvasTransform transform;
    LiveLayerPipeline liveLayer;
    SelectionGizmo selectionGizmo;

    // Tracking state
    Point2D lastInkingWorldMm{0.0, 0.0};
    bool isCurrentlyInking = false;

    char pageTitle[128] = "New Untitled";
    std::string pageDateStr = "Tuesday, August 18, 2026";
    std::string pageTimeStr = "9:54 PM";
    PaperStyle currentPaperStyle = PaperStyle::Grid;

    // Grid spacing standard: 5.0 mm rule
    double gridSpacingMm = 5.0;

    // Theme-driven canvas paper colors (defaults to light mode paper)
    BLRgba32 canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
    BLRgba32 gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);

    // Page Border settings
    bool showPageBorder = false;
    BLRgba32 pageBorderColor = BLRgba32(0xD0, 0xD4, 0xDC);
    double pageBorderWidth = 1.5;
    PageBorderType pageBorderType = PageBorderType::Automatic;
    PageBorderStyle pageBorderStyle = PageBorderStyle::Continuous;
    PageSizeFormat pageSizeFormat = PageSizeFormat::Letter;
    bool pageIsLandscape = false;
    double customPageWidthMm = 215.9;
    double customPageHeightMm = 279.4;

    // Content extents tracking (for Automatic border calculation)
    double contentMaxXMm = 0.0;
    double contentMaxYMm = 0.0;

    // Canvas Infinity Mode: SemiInfinity, FullInfinity, VerticalScroll, HorizontalScroll
    CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity;

    void SetInfinityMode(CanvasInfinityMode mode) {
        infinityMode = mode;
        transform.infinityMode = mode;
        transform.ClampPan();
        isDirty = true;
        needsFullRebake = true;
    }

    void GetStandardPageDimensionsMm(double& outW, double& outH) const {
        double w = 215.9, h = 279.4;
        switch (pageSizeFormat) {
            case PageSizeFormat::Letter: w = 215.9; h = 279.4; break;
            case PageSizeFormat::A4:     w = 210.0; h = 297.0; break;
            case PageSizeFormat::A3:     w = 297.0; h = 420.0; break;
            case PageSizeFormat::A5:     w = 148.0; h = 210.0; break;
            case PageSizeFormat::Custom: w = customPageWidthMm; h = customPageHeightMm; break;
        }
        if (pageIsLandscape) std::swap(w, h);
        outW = w;
        outH = h;
    }

    void GetCalculatedPageBoundsMm(double& outW, double& outH) const {
        double stdW, stdH;
        GetStandardPageDimensionsMm(stdW, stdH);
        if (pageBorderType == PageBorderType::Fixed) {
            outW = stdW;
            outH = stdH;
            return;
        }
        // Automatic: widest used space, bottom calculated to preserve aspect ratio
        double aspect = (stdW > 0.0) ? (stdH / stdW) : 1.2941;
        double usedW = std::max(stdW, contentMaxXMm);
        double calcH = usedW * aspect;
        if (contentMaxYMm > calcH) {
            usedW = contentMaxYMm / aspect;
            calcH = contentMaxYMm;
        }
        outW = usedW;
        outH = calcH;
    }

    // Page Template Defaults (applied to every newly created page)
    PageTemplateDefaults defaultTemplate;

    void ApplyDefaultTemplate() {
        currentPaperStyle = defaultTemplate.paperStyle;
        gridSpacingMm = defaultTemplate.gridSpacingMm;
        canvasBgColor = inkColorInverted ? defaultTemplate.invertedBgColor : defaultTemplate.normalBgColor;
        gridLineColor = inkColorInverted ? defaultTemplate.invertedLineColor : defaultTemplate.normalLineColor;
        showPageBorder = defaultTemplate.showBorder;
        pageBorderColor = defaultTemplate.borderColor;
        pageBorderWidth = defaultTemplate.borderWidth;
        pageBorderType = defaultTemplate.borderType;
        pageBorderStyle = defaultTemplate.borderStyle;
        pageSizeFormat = defaultTemplate.pageSizeFormat;
        pageIsLandscape = defaultTemplate.pageIsLandscape;
        SetInfinityMode(defaultTemplate.infinityMode);
        transform.SetDPI(static_cast<float>(defaultTemplate.calibrationDpi));
        isDirty = true;
        needsFullRebake = true;
    }

    bool isDirty = true;
    bool needsFullRebake = true;      // Background grid + all objects
    bool needsObjectRebake = false;   // Only re-stroke dirty InkContainers, skip background redraw

    // Canvas-level ink color invert (dark mode trick: keeps ink readable without changing presets)
    bool inkColorInverted = false;

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
        ::Folio::UsageTracker::Instance().RecordPanGesture();
    }

    void ZoomAt(double screenX, double screenY, double factor) noexcept {
        transform.ZoomAtScreenPoint(screenX, screenY, factor);
        isDirty = true;
        needsFullRebake = true;
        ::Folio::UsageTracker::Instance().RecordZoomGesture();
    }

    [[nodiscard]] Viewport GetViewport() const noexcept {
        return transform.GetVisibleViewportMm(viewportW, viewportH);
    }

    // -------------------------------------------------------------
    // LIVE INGESTION HOOKS (Screen Px -> World mm)
    // -------------------------------------------------------------

    void OnPointerDown(float screenX, float screenY, float pressure, double timeSec, const PenTool& tool, float tiltX = 0.0f, float tiltY = 0.0f) {
        BLContext liveClear(liveInkingLayer);
        liveClear.clear_all();
        liveClear.end();

        Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
        lastInkingWorldMm = worldMm;
        isCurrentlyInking = true;
        liveLayer.BeginStroke(worldMm.x, worldMm.y, pressure, timeSec, tool, static_cast<float>(transform.GetEffectiveScale()), tiltX, tiltY);
        isDirty = true;
    }

    void OnPointerMove(float screenX, float screenY, float pressure, double timeSec, float tiltX = 0.0f, float tiltY = 0.0f) {
        Point2D worldMm = transform.ScreenToWorld(screenX, screenY);
        if (isCurrentlyInking) {
            double dx = worldMm.x - lastInkingWorldMm.x;
            double dy = worldMm.y - lastInkingWorldMm.y;
            double distMm = std::sqrt(dx * dx + dy * dy);
            ::Folio::UsageTracker::Instance().RecordInkingDistance(distMm);
            lastInkingWorldMm = worldMm;
        }
        liveLayer.AddStrokePoint(worldMm.x, worldMm.y, pressure, timeSec, static_cast<float>(transform.GetEffectiveScale()), tiltX, tiltY);
        isDirty = true;
    }

    // Finalizes the live stroke and hands the data to the DocumentSession
    void OnPointerUp(DocumentSession& session, const PenTool& tool) {
        isCurrentlyInking = false;
        FinishedStrokeData data = liveLayer.FinishStroke();

        BLContext liveClear(liveInkingLayer);
        liveClear.clear_all();
        liveClear.end();

        isDirty = true;
        needsObjectRebake = true;  // Only the newly added container needs re-stroking, background is unchanged

        // Direct handoff: Canvas -> DocumentSession (passes outlinePath + modeledPoints + segments)
        if (!data.outlinePath.is_empty() || !data.liveSegments.empty()) {
            session.CommitStroke(std::move(data), tool);
            ::Folio::UsageTracker::Instance().RecordStrokeCommitted();
            ::Folio::UsageTracker::Instance().RecordObjectCreated();
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

    std::vector<Point2D> OnLassoUp(DocumentSession* session = nullptr) {
        std::vector<Point2D> lasso = liveLayer.FinishLasso();
        isDirty = true;
        if (session && lasso.size() >= 3) {
            auto activePage = session->GetActivePage();
            if (activePage) {
                double minX = lasso[0].x, maxX = lasso[0].x;
                double minY = lasso[0].y, maxY = lasso[0].y;
                for (const auto& pt : lasso) {
                    minX = std::min(minX, pt.x);
                    maxX = std::max(maxX, pt.x);
                    minY = std::min(minY, pt.y);
                    maxY = std::max(maxY, pt.y);
                }
                AABB lassoBox(minX, minY, maxX, maxY);

                std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(lassoBox);
                for (uint32_t uid : candidateUids) {
                    auto obj = activePage->FindObjectByUid(uid);
                    if (obj && obj->Intersects(lassoBox)) {
                        obj->isSelected = 1;
                    }
                }
                selectionGizmo.SetSelectedObjects(activePage->objects);
                needsFullRebake = true;
            }
        }
        return lasso;
    }

    bool DeleteSelectedObjects(DocumentSession* session = nullptr) {
        if (!session) return false;
        auto activePage = session->GetActivePage();
        if (!activePage) return false;

        std::vector<std::shared_ptr<CanvasObject>> toRemove;
        for (const auto& obj : activePage->objects) {
            if (obj && obj->isSelected) {
                toRemove.push_back(obj);
            }
        }
        if (toRemove.empty()) {
            if (!activePage->objects.empty()) {
                toRemove.push_back(activePage->objects.back());
            } else {
                return false;
            }
        }

        for (const auto& obj : toRemove) {
            activePage->RemoveObject(obj);
        }
        selectionGizmo.ClearSelection();
        needsFullRebake = true;
        isDirty = true;
        return true;
    }

    bool EraseAt(float screenX, float screenY, double radiusMm, DocumentSession& session, bool isStrokeEraser = true) {
        auto activePage = session.GetActivePage();
        if (!activePage) return false;

        Point2D world = transform.ScreenToWorld(screenX, screenY);
        double r = std::max(0.5, radiusMm);
        AABB queryBox(world.x - r, world.y - r, world.x + r, world.y + r);
        std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(queryBox);
        if (candidateUids.empty()) return false;

        bool modified = false;
        for (uint32_t uid : candidateUids) {
            auto obj = activePage->FindObjectByUid(uid);
            if (!obj) continue;

            if (isStrokeEraser) {
                if (obj->HitTest(world.x, world.y) || obj->Intersects(queryBox)) {
                    activePage->RemoveObject(obj);
                    modified = true;
                }
            } else {
                // Precision / Point eraser: slices vector ink strokes along eraser boundaries
                if (obj->type == ObjectType::InkContainer) {
                    auto ink = std::static_pointer_cast<InkContainer>(obj);
                    if (ink && ink->SliceStrokeAt(world.x, world.y, r)) {
                        if (ink->strokes.empty()) {
                            activePage->RemoveObject(ink);
                        } else {
                            activePage->UpdateObject(ink);
                        }
                        modified = true;
                    }
                } else {
                    // Non-stroke objects (e.g. image, text box, shape): delete on direct hit
                    if (obj->HitTest(world.x, world.y) || obj->Intersects(queryBox)) {
                        activePage->RemoveObject(obj);
                        modified = true;
                    }
                }
            }
        }

        if (modified) {
            isDirty = true;
            needsFullRebake = true;
            ::Folio::UsageTracker::Instance().RecordEraserAction();
        }
        return modified;
    }

    // -------------------------------------------------------------
    // RENDER PASSES (Static Layer Caching + Live Layer Composite)
    // -------------------------------------------------------------

    void Render(const std::vector<std::shared_ptr<CanvasObject>>& visibleBakedObjects) {
        if (viewportW <= 0 || viewportH <= 0) return;
        if (!isDirty && !needsFullRebake) return;

        // Track content bounds for automatic page border
        double maxX = 0.0, maxY = 0.0;
        for (const auto& obj : visibleBakedObjects) {
            if (!obj) continue;
            const AABB& b = obj->bounds;
            if (b.minX <= b.maxX && b.minY <= b.maxY) {
                maxX = std::max(maxX, b.maxX);
                maxY = std::max(maxY, b.maxY);
            }
        }
        contentMaxXMm = maxX;
        contentMaxYMm = maxY;

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

        // 3. Selection Gizmo Overlay Pass (Screen Coordinates)
        if (selectionGizmo.HasSelection()) {
            selectionGizmo.Render(compCtx, transform);
        }

        compCtx.end();

        // 3a. Optional ink color invert pass (canvas-level, export-safe)
        if (inkColorInverted) {
            BLImageData invertData;
            compositeSurface.get_data(&invertData);
            uint8_t* pixels = static_cast<uint8_t*>(invertData.pixel_data);
            int totalRows = viewportH;
            intptr_t stride = invertData.stride;
            for (int row = 0; row < totalRows; ++row) {
                uint8_t* rowPtr = pixels + row * stride;
                for (int col = 0; col < viewportW; ++col) {
                    uint8_t* px = rowPtr + col * 4;
                    px[0] = 255 - px[0]; // B
                    px[1] = 255 - px[1]; // G
                    px[2] = 255 - px[2]; // R
                }
            }
        }

        BLImageData imgData;
        compositeSurface.get_data(&imgData);
        glBindTexture(GL_TEXTURE_2D, glTexture);
#if defined(__ANDROID__)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewportW, viewportH, GL_RGBA, GL_UNSIGNED_BYTE, imgData.pixel_data);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#else
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
        double pageWMm = 215.9;
        double pageHMm = 279.4;
        GetCalculatedPageBoundsMm(pageWMm, pageHMm);
        const double scale = transform.GetEffectiveScale();
        Point2D originScreen = transform.WorldToScreen(0.0, 0.0);
        Point2D cornerScreen = transform.WorldToScreen(pageWMm, pageHMm);
        double rectScreenW = cornerScreen.x - originScreen.x;
        double rectScreenH = cornerScreen.y - originScreen.y;

        // Visual backdrop setup based on infinity mode
        if (infinityMode == CanvasInfinityMode::VerticalScroll) {
            // Exterior desk shading
            BLRgba32 deskCol = (canvasBgColor.r() > 128)
                ? BLRgba32(static_cast<uint8_t>(canvasBgColor.r() * 0.93),
                           static_cast<uint8_t>(canvasBgColor.g() * 0.93),
                           static_cast<uint8_t>(canvasBgColor.b() * 0.94))
                : BLRgba32(static_cast<uint8_t>(std::min(255, (int)(canvasBgColor.r() * 1.25 + 10))),
                           static_cast<uint8_t>(std::min(255, (int)(canvasBgColor.g() * 1.25 + 10))),
                           static_cast<uint8_t>(std::min(255, (int)(canvasBgColor.b() * 1.25 + 12))));
            ctx.fill_all(deskCol);
            // Continuous vertical paper roll
            ctx.fill_rect(originScreen.x, std::max(0.0, originScreen.y), rectScreenW, viewportH - std::max(0.0, originScreen.y), canvasBgColor);
        } else if (infinityMode == CanvasInfinityMode::HorizontalScroll) {
            BLRgba32 deskCol = (canvasBgColor.r() > 128)
                ? BLRgba32(static_cast<uint8_t>(canvasBgColor.r() * 0.93),
                           static_cast<uint8_t>(canvasBgColor.g() * 0.93),
                           static_cast<uint8_t>(canvasBgColor.b() * 0.94))
                : BLRgba32(static_cast<uint8_t>(std::min(255, (int)(canvasBgColor.r() * 1.25 + 10))),
                           static_cast<uint8_t>(std::min(255, (int)(canvasBgColor.g() * 1.25 + 10))),
                           static_cast<uint8_t>(std::min(255, (int)(canvasBgColor.b() * 1.25 + 12))));
            ctx.fill_all(deskCol);
            // Continuous horizontal drafting roll
            ctx.fill_rect(std::max(0.0, originScreen.x), originScreen.y, viewportW - std::max(0.0, originScreen.x), rectScreenH, canvasBgColor);
        } else {
            // FullInfinity or SemiInfinity
            ctx.fill_all(canvasBgColor);
        }

        // Draw page border if enabled
        if (showPageBorder) {
            ctx.save();
            ctx.set_stroke_style(pageBorderColor);
            double strokePx = std::max(1.0, pageBorderWidth * (scale / transform.pixelsPerMm));
            ctx.set_stroke_width(strokePx);

            double bx = originScreen.x;
            double by = originScreen.y;
            double bw = rectScreenW;
            double bh = rectScreenH;

            if (pageBorderStyle == PageBorderStyle::Dashed) {
                BLArray<double> dashArray;
                dashArray.append(8.0);
                dashArray.append(6.0);
                ctx.set_stroke_dash_array(dashArray);
            }

            if (pageBorderStyle == PageBorderStyle::Corners) {
                double arm = std::min(std::min(bw, bh) * 0.25, 20.0 * (scale / transform.pixelsPerMm));
                if (arm > 2.0) {
                    // Top-Left corner
                    ctx.stroke_line(bx, by + arm, bx, by);
                    ctx.stroke_line(bx, by, bx + arm, by);
                    // Top-Right corner
                    ctx.stroke_line(bx + bw - arm, by, bx + bw, by);
                    ctx.stroke_line(bx + bw, by, bx + bw, by + arm);
                    // Bottom-Left corner
                    ctx.stroke_line(bx, by + bh - arm, bx, by + bh);
                    ctx.stroke_line(bx, by + bh, bx + arm, by + bh);
                    // Bottom-Right corner
                    ctx.stroke_line(bx + bw - arm, by + bh, bx + bw, by + bh);
                    ctx.stroke_line(bx + bw, by + bh, bx + bw, by + bh - arm);
                }
            } else {
                // Continuous or Dashed rectangle
                ctx.stroke_rect(bx, by, bw, bh);
            }
            ctx.restore();
        }

        if (currentPaperStyle == PaperStyle::Blank) {
            return;
        }

        const double stepMm = gridSpacingMm;

        ctx.save();
        // Snapping lines calculated directly from visible world coordinates
        double startX = std::floor(currentView.bounds.minX / stepMm) * stepMm;
        double endX   = std::ceil(currentView.bounds.maxX / stepMm) * stepMm;
        double startY = std::floor(currentView.bounds.minY / stepMm) * stepMm;
        double endY   = std::ceil(currentView.bounds.maxY / stepMm) * stepMm;

        ctx.set_stroke_style(gridLineColor);
        ctx.set_stroke_width(1.0); // 1px thin cosmetic line

        // Draw dotted grid
        if (currentPaperStyle == PaperStyle::Dotted) {
            ctx.set_fill_style(gridLineColor);
            for (double wy = startY; wy <= endY; wy += stepMm) {
                if (infinityMode != CanvasInfinityMode::FullInfinity && wy < 0.0) continue;
                if (infinityMode == CanvasInfinityMode::HorizontalScroll && wy > pageHMm) continue;
                for (double wx = startX; wx <= endX; wx += stepMm) {
                    if (infinityMode != CanvasInfinityMode::FullInfinity && wx < 0.0) continue;
                    if (infinityMode == CanvasInfinityMode::VerticalScroll && wx > pageWMm) continue;
                    Point2D pt = transform.WorldToScreen(wx, wy);
                    ctx.fill_circle(pt.x, pt.y, 1.2);
                }
            }
        }
        else {
            // Draw vertical grid lines
            if (currentPaperStyle == PaperStyle::Grid) {
                for (double wx = startX; wx <= endX; wx += stepMm) {
                    if (infinityMode != CanvasInfinityMode::FullInfinity && wx < 0.0) continue;
                    if (infinityMode == CanvasInfinityMode::VerticalScroll && wx > pageWMm) continue;
                    double minY = currentView.bounds.minY;
                    double maxY = currentView.bounds.maxY;
                    if (infinityMode != CanvasInfinityMode::FullInfinity) minY = std::max(0.0, minY);
                    if (infinityMode == CanvasInfinityMode::HorizontalScroll) maxY = std::min(pageHMm, maxY);
                    if (minY < maxY) {
                        Point2D sTop = transform.WorldToScreen(wx, minY);
                        Point2D sBot = transform.WorldToScreen(wx, maxY);
                        ctx.stroke_line(sTop.x, sTop.y, sBot.x, sBot.y);
                    }
                }
            }

            // Draw horizontal grid / ruled lines
            for (double wy = startY; wy <= endY; wy += stepMm) {
                if (infinityMode != CanvasInfinityMode::FullInfinity && wy < 0.0) continue;
                if (infinityMode == CanvasInfinityMode::HorizontalScroll && wy > pageHMm) continue;
                double minX = currentView.bounds.minX;
                double maxX = currentView.bounds.maxX;
                if (infinityMode != CanvasInfinityMode::FullInfinity) minX = std::max(0.0, minX);
                if (infinityMode == CanvasInfinityMode::VerticalScroll) maxX = std::min(pageWMm, maxX);
                if (minX < maxX) {
                    Point2D sLeft  = transform.WorldToScreen(minX, wy);
                    Point2D sRight = transform.WorldToScreen(maxX, wy);
                    ctx.stroke_line(sLeft.x, sLeft.y, sRight.x, sRight.y);
                }
            }
        }

        ctx.restore();
    }
};