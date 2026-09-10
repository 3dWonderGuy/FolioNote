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
#include "utils/uid_generator.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

    // Dev Mode (Rnote-style AABB & Collision Debugger)
    bool devMode = false;
    bool debugShowObjectAABB = true;
    bool debugShowSegmentAABB = true;
    bool debugShowQueryAABB = true;
    bool debugShowLabels = true;

    struct DebugCollisionInfo {
        bool active = false;
        Point2D queryCenter{0.0, 0.0};
        double queryRadius = 0.0;
        AABB queryBox{0.0, 0.0, 0.0, 0.0};
        std::vector<uint32_t> candidateUids;
        std::vector<uint32_t> hitUids;
    } debugCollision;

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

        if (devMode) {
            debugCollision.active = true;
            debugCollision.queryCenter = world;
            debugCollision.queryRadius = r;
            debugCollision.queryBox = queryBox;
            debugCollision.candidateUids.clear();
            debugCollision.hitUids.clear();
        }

        std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(queryBox);
        if (candidateUids.empty()) {
            if (devMode) {
                isDirty = true;
            }
            return false;
        }

        if (devMode) {
            debugCollision.candidateUids = candidateUids;
        }

        bool modified = false;
        for (uint32_t uid : candidateUids) {
            auto obj = activePage->FindObjectByUid(uid);
            if (!obj) continue;

            if (isStrokeEraser) {
                if (obj->HitTestCircle(world.x, world.y, r)) {
                    if (devMode) debugCollision.hitUids.push_back(obj->uid);
                    activePage->RemoveObject(obj);
                    modified = true;
                }
            } else {
                // Precision / Point eraser: slices vector ink strokes along eraser boundaries
                if (obj->type == ObjectType::InkContainer) {
                    auto ink = std::static_pointer_cast<InkContainer>(obj);
                    std::vector<std::shared_ptr<InkContainer>> newFragments;
                    if (ink && ink->SliceStrokeAt(world.x, world.y, r, newFragments)) {
                        if (devMode) debugCollision.hitUids.push_back(obj->uid);
                        if (ink->strokes.empty()) {
                            activePage->RemoveObject(ink);
                        } else {
                            activePage->UpdateObject(ink);
                        }
                        // Insert newly created surviving stroke fragments into the page
                        for (auto& frag : newFragments) {
                            frag->uid = UIDGenerator::Next();
                            activePage->AddObject(frag);
                        }
                        modified = true;
                    }
                } else {
                    // Non-stroke objects (e.g. image, text box, shape): delete on direct hit
                    if (obj->HitTestCircle(world.x, world.y, r)) {
                        if (devMode) debugCollision.hitUids.push_back(obj->uid);
                        activePage->RemoveObject(obj);
                        modified = true;
                    }
                }
            }
        }

        if (modified || devMode) {
            isDirty = true;
            if (modified) {
                needsFullRebake = true;
                ::Folio::UsageTracker::Instance().RecordEraserAction();
            }
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

        // 4. Dev Mode: Rnote-Style AABB & Collision Debugger (Screen Coordinates)
        if (devMode) {
            RenderDevModeAABBs(compCtx, visibleBakedObjects, transform);
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

    // -------------------------------------------------------------
    // DEV MODE: RNOTE-STYLE AABB & COLLISION INSPECTOR (Blend2D Overlay)
    // -------------------------------------------------------------
    void RenderDevModeAABBs(BLContext& ctx, const std::vector<std::shared_ptr<CanvasObject>>& visibleObjects, const CanvasTransform& tr) {
        ctx.save();

        // 1. Draw Object AABBs and optional segment mini-AABBs
        for (const auto& obj : visibleObjects) {
            if (!obj) continue;
            AABB box = obj->GetAABB();
            Point2D pMin = tr.WorldToScreen(box.minX, box.minY);
            Point2D pMax = tr.WorldToScreen(box.maxX, box.maxY);
            double sx = std::min(pMin.x, pMax.x);
            double sy = std::min(pMin.y, pMax.y);
            double sw = std::abs(pMax.x - pMin.x);
            double sh = std::abs(pMax.y - pMin.y);

            bool isHit = false;
            for (uint32_t hid : debugCollision.hitUids) {
                if (hid == obj->uid) { isHit = true; break; }
            }
            bool isCandidate = false;
            if (!isHit) {
                for (uint32_t cid : debugCollision.candidateUids) {
                    if (cid == obj->uid) { isCandidate = true; break; }
                }
            }

            BLRgba32 strokeColor;
            BLRgba32 fillColor;
            const char* typeTag = "Obj";

            if (isHit) {
                strokeColor = BLRgba32(0xFF, 0x17, 0x44, 0xE0); // Red
                fillColor   = BLRgba32(0xFF, 0x17, 0x44, 0x2A);
            } else if (isCandidate) {
                strokeColor = BLRgba32(0xFF, 0xEA, 0x00, 0xD0); // Amber/Yellow
                fillColor   = BLRgba32(0xFF, 0xEA, 0x00, 0x20);
            } else if (obj->type == ObjectType::InkContainer) {
                strokeColor = BLRgba32(0x00, 0xE5, 0xFF, 0x99); // Cyan
                fillColor   = BLRgba32(0x00, 0xE5, 0xFF, 0x0F);
                typeTag = "Ink";
            } else if (obj->type == ObjectType::Image) {
                strokeColor = BLRgba32(0x00, 0xE6, 0x76, 0x99); // Green
                fillColor   = BLRgba32(0x00, 0xE6, 0x76, 0x0F);
                typeTag = "Img";
            } else {
                strokeColor = BLRgba32(0xFF, 0x91, 0x00, 0x99); // Orange
                fillColor   = BLRgba32(0xFF, 0x91, 0x00, 0x0F);
                typeTag = "Box";
            }

            if (debugShowObjectAABB) {
                ctx.set_fill_style(fillColor);
                ctx.fill_rect(sx, sy, sw, sh);
                ctx.set_stroke_style(strokeColor);
                ctx.set_stroke_width(1.0);
                ctx.stroke_rect(sx, sy, sw, sh);

                // Corner brackets for CAD visual feel
                double bracketLen = std::min(6.0, std::min(sw, sh) * 0.3);
                if (bracketLen > 2.0) {
                    ctx.set_stroke_width(2.0);
                    ctx.stroke_line(sx, sy, sx + bracketLen, sy);
                    ctx.stroke_line(sx, sy, sx, sy + bracketLen);
                    ctx.stroke_line(sx + sw, sy + sh, sx + sw - bracketLen, sy + sh);
                    ctx.stroke_line(sx + sw, sy + sh, sx + sw, sy + sh - bracketLen);
                }

                if (debugShowLabels) {
                    char label[64];
                    int wMm = static_cast<int>(std::round(box.Width()));
                    int hMm = static_cast<int>(std::round(box.Height()));
                    std::snprintf(label, sizeof(label), "[%s #%u] %dx%d", typeTag, obj->uid, wMm, hMm);

                    double labelW = (std::strlen(label) * 6.5) + 6.0;
                    ctx.set_fill_style(BLRgba32(0x10, 0x14, 0x1E, 0xEE));
                    ctx.fill_round_rect(sx, sy - 14.0, labelW, 13.0, 2.0);
                    ctx.set_stroke_style(strokeColor);
                    ctx.set_stroke_width(0.8);
                    ctx.stroke_round_rect(sx, sy - 14.0, labelW, 13.0, 2.0);

                    SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(sx + 3.0), static_cast<float>(sy - 12.0), label);
                }
            }

            // On-The-Fly Narrowphase Segment mini-AABBs
            if (debugShowSegmentAABB && obj->type == ObjectType::InkContainer) {
                auto ink = std::static_pointer_cast<InkContainer>(obj);
                if (ink) {
                    ctx.set_stroke_style(isHit ? BLRgba32(0xFF, 0x52, 0x52, 0x88) : BLRgba32(0x76, 0xFF, 0x03, 0x55));
                    ctx.set_stroke_width(0.75);
                    for (const auto& stroke : ink->strokes) {
                        for (const auto& seg : stroke.segments) {
                            double r = static_cast<double>(seg.width) * 0.5;
                            double sMinX = std::min(seg.p0.x, seg.p1.x) - r;
                            double sMinY = std::min(seg.p0.y, seg.p1.y) - r;
                            double sMaxX = std::max(seg.p0.x, seg.p1.x) + r;
                            double sMaxY = std::max(seg.p0.y, seg.p1.y) + r;

                            Point2D sc1 = tr.WorldToScreen(sMinX, sMinY);
                            Point2D sc2 = tr.WorldToScreen(sMaxX, sMaxY);
                            double msx = std::min(sc1.x, sc2.x);
                            double msy = std::min(sc1.y, sc2.y);
                            double msw = std::abs(sc2.x - sc1.x);
                            double msh = std::abs(sc2.y - sc1.y);

                            ctx.stroke_rect(msx, msy, msw, msh);
                        }
                    }
                }
            }
        }

        // 2. Spatial Query / Eraser Kernel Visualization
        if (debugShowQueryAABB && debugCollision.active) {
            Point2D scMin = tr.WorldToScreen(debugCollision.queryBox.minX, debugCollision.queryBox.minY);
            Point2D scMax = tr.WorldToScreen(debugCollision.queryBox.maxX, debugCollision.queryBox.maxY);
            double qx = std::min(scMin.x, scMax.x);
            double qy = std::min(scMin.y, scMax.y);
            double qw = std::abs(scMax.x - scMin.x);
            double qh = std::abs(scMax.y - scMin.y);

            // Query AABB (Hot Pink / Magenta)
            ctx.set_fill_style(BLRgba32(0xF5, 0x00, 0x57, 0x22));
            ctx.fill_rect(qx, qy, qw, qh);
            ctx.set_stroke_style(BLRgba32(0xF5, 0x00, 0x57, 0xCC));
            ctx.set_stroke_width(1.5);
            ctx.stroke_rect(qx, qy, qw, qh);

            // Eraser query circle
            Point2D qCenter = tr.WorldToScreen(debugCollision.queryCenter.x, debugCollision.queryCenter.y);
            double qRadScreen = debugCollision.queryRadius * tr.zoom;
            ctx.set_stroke_style(BLRgba32(0xFF, 0x40, 0x81, 0xFF));
            ctx.set_stroke_width(2.0);
            ctx.stroke_circle(qCenter.x, qCenter.y, qRadScreen);

            // Crosshair center
            ctx.set_stroke_width(1.0);
            ctx.stroke_line(qCenter.x - 4.0, qCenter.y, qCenter.x + 4.0, qCenter.y);
            ctx.stroke_line(qCenter.x, qCenter.y - 4.0, qCenter.x, qCenter.y + 4.0);

            // Query label
            char qLabel[64];
            std::snprintf(qLabel, sizeof(qLabel), "[QUERY R:%.1f] Cands:%zu Hits:%zu",
                          debugCollision.queryRadius, debugCollision.candidateUids.size(), debugCollision.hitUids.size());
            ctx.set_fill_style(BLRgba32(0x20, 0x00, 0x10, 0xF0));
            double qlW = (std::strlen(qLabel) * 6.5) + 6.0;
            ctx.fill_round_rect(qx, qy - 15.0, qlW, 14.0, 2.0);
            ctx.set_stroke_style(BLRgba32(0xF5, 0x00, 0x57, 0xFF));
            ctx.stroke_round_rect(qx, qy - 15.0, qlW, 14.0, 2.0);
            SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(qx + 3.0), static_cast<float>(qy - 13.0), qLabel);
        }

        // 3. Top-Right HUD Badge: DEV MODE [F4]
        {
            char hudStr[80];
            std::snprintf(hudStr, sizeof(hudStr), "DEV[F4] Vis:%zu Hits:%zu",
                          visibleObjects.size(),
                          debugCollision.hitUids.size());
            float hudW = static_cast<float>(std::strlen(hudStr) * 6.5 + 16.0);
            float hudX = static_cast<float>(viewportW) - hudW - 16.0f;
            float hudY = 16.0f;

            ctx.set_fill_style(BLRgba32(0x12, 0x16, 0x24, 0xEE));
            ctx.fill_round_rect(hudX, hudY, hudW, 20.0f, 4.0f);
            ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0xBB));
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(hudX, hudY, hudW, 20.0f, 4.0f);

            // Indicator dot: Green if clean, Red if hits active
            BLRgba32 dotColor = debugCollision.hitUids.empty() ? BLRgba32(0x00, 0xE6, 0x76, 0xFF) : BLRgba32(0xFF, 0x17, 0x44, 0xFF);
            ctx.fill_circle(hudX + 8.0f, hudY + 10.0f, 3.0, dotColor);

            SelectionGizmo::DrawFallbackText(ctx, hudX + 16.0f, hudY + 5.0f, hudStr);
        }

        ctx.restore();
    }
};