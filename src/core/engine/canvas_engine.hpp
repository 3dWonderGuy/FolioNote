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
#include "core/objects/image_container.hpp"
#include "core/objects/shape_container.hpp"
#include "core/objects/pdf_container.hpp"
#include "core/storage/pdf_storage.hpp"
#include "core/engine/canvas_transform.hpp"
#include "core/engine/live_layer_pipeline.hpp"
#include "core/engine/selection_gizmo.hpp"
#include "core/document/document_session.hpp"
#include "utils/usage_tracker.hpp"
#include "utils/uid_generator.hpp"
#include "utils/guid_generator.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <SDL3/SDL_dialog.h>

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

    struct EraserVisualState {
        bool isVisible = false;
        float screenX = 0.0f;
        float screenY = 0.0f;
        double radiusMm = 3.0;
        bool isDown = false;
        bool isStrokeEraser = true;
    } eraserVisual;

    void SetEraserCursor(float screenX, float screenY, double radiusMm, bool isDown, bool isStroke) {
        bool wasVisible = eraserVisual.isVisible;
        float oldX = eraserVisual.screenX;
        float oldY = eraserVisual.screenY;
        eraserVisual.isVisible = true;
        eraserVisual.screenX = screenX;
        eraserVisual.screenY = screenY;
        eraserVisual.radiusMm = radiusMm;
        eraserVisual.isDown = isDown;
        eraserVisual.isStrokeEraser = isStroke;
        if (!wasVisible || std::abs(oldX - screenX) > 0.5f || std::abs(oldY - screenY) > 0.5f) {
            isDirty = true;
        }
    }

    void HideEraserCursor() {
        if (eraserVisual.isVisible) {
            eraserVisual.isVisible = false;
            isDirty = true;
        }
    }

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
    SDL_Window* sdlWindow = nullptr;

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

    struct MarqueeBoxState {
        bool isActive = false;
        Point2D startWorld{0.0, 0.0};
        Point2D currentWorld{0.0, 0.0};
        float startScreenX = 0.0f;
        float startScreenY = 0.0f;
        float currentScreenX = 0.0f;
        float currentScreenY = 0.0f;
    } marqueeBox;

    void OnBoxSelectDown(float screenX, float screenY) {
        marqueeBox.isActive = true;
        marqueeBox.startScreenX = screenX;
        marqueeBox.startScreenY = screenY;
        marqueeBox.currentScreenX = screenX;
        marqueeBox.currentScreenY = screenY;
        marqueeBox.startWorld = transform.ScreenToWorld(screenX, screenY);
        marqueeBox.currentWorld = marqueeBox.startWorld;
        isDirty = true;
    }

    void OnBoxSelectMove(float screenX, float screenY) {
        if (!marqueeBox.isActive) return;
        marqueeBox.currentScreenX = screenX;
        marqueeBox.currentScreenY = screenY;
        marqueeBox.currentWorld = transform.ScreenToWorld(screenX, screenY);
        isDirty = true;
    }

    void OnBoxSelectUp(DocumentSession* session = nullptr) {
        if (!marqueeBox.isActive) return;
        marqueeBox.isActive = false;
        isDirty = true;

        if (session) {
            auto activePage = session->GetActivePage();
            if (activePage) {
                double minX = std::min(marqueeBox.startWorld.x, marqueeBox.currentWorld.x);
                double maxX = std::max(marqueeBox.startWorld.x, marqueeBox.currentWorld.x);
                double minY = std::min(marqueeBox.startWorld.y, marqueeBox.currentWorld.y);
                double maxY = std::max(marqueeBox.startWorld.y, marqueeBox.currentWorld.y);

                // If dragged at least a small threshold (> 2mm)
                if ((maxX - minX) > 2.0 || (maxY - minY) > 2.0) {
                    AABB box(minX, minY, maxX, maxY);
                    std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(box);
                    for (uint32_t uid : candidateUids) {
                        auto obj = activePage->FindObjectByUid(uid);
                        if (obj && obj->Intersects(box)) {
                            obj->isSelected = 1;
                        }
                    }
                    selectionGizmo.SetSelectedObjects(activePage->objects);
                    needsFullRebake = true;
                }
            }
        }
    }

    enum class SelectionMode {
        Box,
        Lasso
    };
    SelectionMode selectionMode = SelectionMode::Box;

    void ClearSelection(DocumentSession* session = nullptr) {
        if (session) {
            auto activePage = session->GetActivePage();
            if (activePage) {
                for (auto& obj : activePage->objects) {
                    if (obj) obj->isSelected = 0;
                }
            }
        }
        selectionGizmo.ClearSelection();
        needsFullRebake = true;
        isDirty = true;
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

    struct ShapeCreationState {
        bool isActive = false;
        bool isDragging = false;
        Folio::ShapeType shapeType = Folio::ShapeType::Rectangle;
        bool lockDrawingMode = false;
        Point2D startWorld{0.0, 0.0};
        Point2D currentWorld{0.0, 0.0};
        float startScreenX = 0.0f;
        float startScreenY = 0.0f;
        float currentScreenX = 0.0f;
        float currentScreenY = 0.0f;

        // Current default properties for newly created shapes
        Folio::ShapeFillType defaultFillType = Folio::ShapeFillType::SemiTransparent;
        BLRgba32 defaultFillColor = BLRgba32(0x00, 0x78, 0xD4, 0x40);
        Folio::ShapeOutlineType defaultOutlineType = Folio::ShapeOutlineType::Solid;
        BLRgba32 defaultOutlineColor = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
        double defaultStrokeWidth = 1.0;
    } shapeCreation;

    void StartShapeCreation(Folio::ShapeType type, bool lockMode = false) {
        shapeCreation.isActive = true;
        shapeCreation.isDragging = false;
        shapeCreation.shapeType = type;
        shapeCreation.lockDrawingMode = lockMode;
        LOG_INFO(CanvasEngine, "Started shape creation mode for type=" + std::to_string(static_cast<int>(type)) +
                 (lockMode ? " [LOCKED]" : " [ONE-SHOT]"));
    }

    void CancelShapeCreation() {
        shapeCreation.isActive = false;
        shapeCreation.isDragging = false;
    }

    void OnShapeDrawDown(float screenX, float screenY) {
        shapeCreation.isDragging = true;
        shapeCreation.startScreenX = screenX;
        shapeCreation.startScreenY = screenY;
        shapeCreation.currentScreenX = screenX;
        shapeCreation.currentScreenY = screenY;
        shapeCreation.startWorld = transform.ScreenToWorld(screenX, screenY);
        shapeCreation.currentWorld = shapeCreation.startWorld;
        isDirty = true;
    }

    void OnShapeDrawMove(float screenX, float screenY) {
        if (!shapeCreation.isDragging) return;
        shapeCreation.currentScreenX = screenX;
        shapeCreation.currentScreenY = screenY;
        shapeCreation.currentWorld = transform.ScreenToWorld(screenX, screenY);
        isDirty = true;
    }

    std::shared_ptr<Folio::ShapeObject> OnShapeDrawUp(DocumentSession* session) {
        if (!shapeCreation.isDragging) return nullptr;
        shapeCreation.isDragging = false;

        if (!session) return nullptr;
        auto activePage = session->GetActivePage();
        if (!activePage) return nullptr;

        double minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
        double maxX = std::max(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
        double minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
        double maxY = std::max(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
        double w = maxX - minX;
        double h = maxY - minY;

        // If clicked/tapped without dragging (less than 2mm threshold), create a standard default size centered at tap
        if (w < 2.0 && h < 2.0) {
            w = 50.0;
            h = 35.0;
            minX = shapeCreation.startWorld.x - w * 0.5;
            minY = shapeCreation.startWorld.y - h * 0.5;
        }

        auto shp = std::make_shared<Folio::ShapeObject>(shapeCreation.shapeType, minX, minY, w, h);
        shp->guuid = GUIDGenerator::GenerateV4();
        shp->uid = UIDGenerator::Next();
        shp->fillType = shapeCreation.defaultFillType;
        shp->fillColor = shapeCreation.defaultFillColor;
        shp->outlineType = shapeCreation.defaultOutlineType;
        shp->strokeColor = shapeCreation.defaultOutlineColor;
        shp->strokeWidth = shapeCreation.defaultStrokeWidth;
        shp->UpdateBounds();

        activePage->AddObject(shp);

        // Select the newly created shape
        ClearSelection(session);
        shp->isSelected = 1;
        selectionGizmo.SetSelectedObjects(activePage->objects);

        needsFullRebake = true;
        isDirty = true;

        LOG_INFO(CanvasEngine, "Created vector shape by drag (type=" + std::to_string(static_cast<int>(shapeCreation.shapeType)) +
                 ", uid=" + std::to_string(shp->uid) + ", bounds=[" + std::to_string(minX) + "," + std::to_string(minY) + " " + std::to_string(w) + "x" + std::to_string(h) + "])");

        if (!shapeCreation.lockDrawingMode) {
            shapeCreation.isActive = false;
        }

        return shp;
    }

    std::shared_ptr<Folio::ShapeObject> GetSelectedShape(DocumentSession* session) const {
        if (!session) return nullptr;
        auto activePage = session->GetActivePage();
        if (!activePage) return nullptr;
        for (const auto& obj : activePage->objects) {
            if (obj && obj->isSelected && obj->type == ObjectType::Shape) {
                return std::dynamic_pointer_cast<Folio::ShapeObject>(obj);
            }
        }
        return nullptr;
    }

    std::shared_ptr<Folio::ShapeObject> InsertShape(Folio::ShapeType type, DocumentSession* session, double worldX = 0.0, double worldY = 0.0) {
        if (!session) return nullptr;
        auto activePage = session->GetActivePage();
        if (!activePage) return nullptr;

        // If default coordinates, place at viewport center
        if (worldX == 0.0 && worldY == 0.0) {
            Point2D center = transform.ScreenToWorld(viewportW * 0.5f, viewportH * 0.5f);
            worldX = center.x - 30.0;
            worldY = center.y - 20.0;
        }

        auto shp = std::make_shared<Folio::ShapeObject>(type, worldX, worldY, 60.0, 40.0);
        shp->guuid = GUIDGenerator::GenerateV4();
        shp->uid = UIDGenerator::Next();
        shp->fillType = shapeCreation.defaultFillType;
        shp->fillColor = shapeCreation.defaultFillColor;
        shp->outlineType = shapeCreation.defaultOutlineType;
        shp->strokeColor = shapeCreation.defaultOutlineColor;
        shp->strokeWidth = shapeCreation.defaultStrokeWidth;
        shp->UpdateBounds();

        activePage->AddObject(shp);

        // Select the newly inserted shape so the user can immediately transform/drag it
        ClearSelection(session);
        shp->isSelected = 1;
        selectionGizmo.SetSelectedObjects(activePage->objects);

        needsFullRebake = true;
        isDirty = true;

        LOG_INFO(CanvasEngine, "Inserted vector shape (type=" + std::to_string(static_cast<int>(type)) + ", uid=" + std::to_string(shp->uid) + ")");
        return shp;
    }

    /**
     * @brief Synchronizes all currently selected objects to the active page spatial index.
     * Updates object bounds and re-indexes into the R-Tree so queries (e.g. eraser collision)
     * instantly recognize the modified/moved geometries.
     */
    void SyncSelectionToSpatialIndex(DocumentSession* session) {
        if (!session) return;
        auto activePage = session->GetActivePage();
        if (!activePage) return;
        for (const auto& obj : selectionGizmo.selectedObjects) {
            if (obj) {
                activePage->UpdateObject(obj);
            }
        }
    }

    /**
     * @brief Deduplicates an imported image file or raw memory buffer into the active notebook's
     * imports/images/ directory, returning the relative package path (e.g. "imports/images/img_<hash>.ext").
     */
    static std::string DeduplicateAndSaveImage(const std::string& srcPath, const void* data, size_t size, DocumentSession* session, const std::string& ext = ".png") {
        if (!session) return "";
        auto activeNb = session->workspace.GetActiveNotebook();
        if (!activeNb || activeNb->filePath.empty()) return "";

        std::error_code ec;
        std::filesystem::path pkgPath(activeNb->filePath);
        std::filesystem::path imgDir = pkgPath / "imports" / "images";
        std::filesystem::create_directories(imgDir, ec);

        // FNV-1a 64-bit content hash for fast deduplication
        uint64_t hash = 14695981039346656037ULL;
        if (data && size > 0) {
            const uint8_t* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= p[i];
                hash *= 1099511628211ULL;
            }
        } else if (!srcPath.empty() && std::filesystem::exists(srcPath, ec)) {
            std::ifstream f(srcPath, std::ios::binary);
            char buf[8192];
            while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
                for (std::streamsize i = 0; i < f.gcount(); ++i) {
                    hash ^= static_cast<uint8_t>(buf[i]);
                    hash *= 1099511628211ULL;
                }
            }
        } else {
            return "";
        }

        char hashStr[32];
        std::snprintf(hashStr, sizeof(hashStr), "%016llx", static_cast<unsigned long long>(hash));
        std::string filename = std::string("img_") + hashStr + ext;
        std::filesystem::path destFile = imgDir / filename;

        // Deduplicate: write/copy only if target doesn't already exist
        if (!std::filesystem::exists(destFile, ec)) {
            if (!srcPath.empty() && std::filesystem::exists(srcPath, ec)) {
                std::filesystem::copy_file(srcPath, destFile, std::filesystem::copy_options::overwrite_existing, ec);
            } else if (data && size > 0) {
                std::ofstream out(destFile, std::ios::binary | std::ios::trunc);
                if (out.is_open()) {
                    out.write(static_cast<const char*>(data), size);
                }
            }
        }

        return (std::filesystem::path("imports") / "images" / filename).string();
    }

    struct ImageFileDialogContext {
        CanvasEngine* canvas = nullptr;
        DocumentSession* session = nullptr;
        Point2D insertPosWorld{0.0, 0.0};
    };

    static void SDLCALL OnImageFileSelected(void* userdata, const char* const* filelist, int /*filter*/) {
        auto* ctx = static_cast<ImageFileDialogContext*>(userdata);
        if (!ctx) return;

        if (filelist && filelist[0] && filelist[0][0] != '\0') {
            std::string selectedPath = filelist[0];
            std::vector<uint8_t> rawBytes;
            std::ifstream file(selectedPath, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                size_t sz = static_cast<size_t>(file.tellg());
                file.seekg(0, std::ios::beg);
                rawBytes.resize(sz);
                file.read(reinterpret_cast<char*>(rawBytes.data()), sz);
            }

            BLImage blImg;
            if (blImg.read_from_file(selectedPath.c_str()) == BL_SUCCESS && !blImg.is_empty()) {
                std::string ext = std::filesystem::path(selectedPath).extension().string();
                if (ext.empty()) ext = ".png";
                std::string storedPath = DeduplicateAndSaveImage(selectedPath, rawBytes.data(), rawBytes.size(), ctx->session, ext);

                auto img = std::make_shared<Folio::ImageContainer>();
                img->SetImage(blImg, storedPath.empty() ? selectedPath : storedPath, 120.0);
                img->embeddedData = std::move(rawBytes);
                img->worldX = ctx->insertPosWorld.x - img->worldWidth * 0.5;
                img->worldY = ctx->insertPosWorld.y - img->worldHeight * 0.5;
                img->UpdateBounds();

                ctx->session->AddImage(img);
                ctx->canvas->needsFullRebake = true;
                ctx->canvas->isDirty = true;
                LOG_INFO(CanvasEngine, "Imported image from '" + selectedPath + "' -> '" + (storedPath.empty() ? selectedPath : storedPath) + "'");
            }
        }

        delete ctx;
    }

    /**
     * @brief Opens a native file dialog (Pictures on Android, Explorer on Desktop) to pick and insert an image.
     */
    void OpenImageFileDialog(SDL_Window* parentWin, DocumentSession* session) {
        if (!session) return;
        Point2D centerWorld = transform.ScreenToWorld(static_cast<float>(viewportW) * 0.5f, static_cast<float>(viewportH) * 0.5f);

        auto* ctx = new ImageFileDialogContext{ this, session, centerWorld };

        static const SDL_DialogFileFilter imageFilters[] = {
            { "Image Files (*.png;*.jpg;*.jpeg;*.webp;*.bmp)", "png;jpg;jpeg;webp;bmp" },
            { "PNG Images (*.png)", "png" },
            { "JPEG Images (*.jpg;*.jpeg)", "jpg;jpeg" },
            { "All Files (*.*)", "*" }
        };

        LOG_INFO(CanvasEngine, "Opening native image file dialog...");
        SDL_ShowOpenFileDialog(OnImageFileSelected, ctx, parentWin ? parentWin : sdlWindow, imageFilters, 4, nullptr, false);
    }

    struct PdfFileDialogContext {
        CanvasEngine* canvas = nullptr;
        DocumentSession* session = nullptr;
        Point2D insertPosWorld{0.0, 0.0};
        bool asBackground = false;
        Folio::PdfImportMode importMode = Folio::PdfImportMode::LocalCopy;
    };

    // Callback invoked when a PDF file is chosen via OpenPdfFileDialog or file drag-and-drop
    std::function<void(const std::string& filePath, DocumentSession* session)> onPdfImportRequested = nullptr;

    static void SDLCALL OnPdfFileSelected(void* userdata, const char* const* filelist, int /*filter*/) {
        auto* ctx = static_cast<PdfFileDialogContext*>(userdata);
        if (!ctx) return;

        if (filelist && filelist[0] && filelist[0][0] != '\0') {
            std::string selectedPath = filelist[0];
            if (ctx->canvas && ctx->canvas->onPdfImportRequested) {
                ctx->canvas->onPdfImportRequested(selectedPath, ctx->session);
            } else {
                Folio::PdfDocumentInfo docInfo;
                if (Folio::PdfStorage::IngestPdf(selectedPath, ctx->session, ctx->importMode, docInfo)) {
                    if (docInfo.isLongDocument) {
                        LOG_INFO(CanvasEngine, "[PDF Recommendation] " + docInfo.warningMessage);
                    }

                    auto activePage = ctx->session->GetActivePage();
                    if (activePage) {
                        auto pdfObj = std::make_shared<Folio::PdfContainer>(
                            docInfo.packagePath, docInfo.originalFileName, 0, docInfo.pageCount,
                            ctx->insertPosWorld.x - 105.0, ctx->insertPosWorld.y - 148.5, 210.0, 297.0, ctx->asBackground
                        );
                        pdfObj->isExternalLink = docInfo.isExternal;
                        pdfObj->guuid = GUIDGenerator::GenerateV4();
                        pdfObj->uid = UIDGenerator::Next();
                        pdfObj->UpdateBounds();

                        activePage->AddObject(pdfObj);
                        ctx->canvas->needsFullRebake = true;
                        ctx->canvas->isDirty = true;
                    }
                }
            }
        }

        delete ctx;
    }

    /**
     * @brief Opens a native file dialog to pick and import a PDF document.
     */
    void OpenPdfFileDialog(SDL_Window* parentWin, DocumentSession* session, bool asBackground = false, Folio::PdfImportMode mode = Folio::PdfImportMode::LocalCopy) {
        if (!session) return;
        Point2D centerWorld = transform.ScreenToWorld(static_cast<float>(viewportW) * 0.5f, static_cast<float>(viewportH) * 0.5f);

        auto* ctx = new PdfFileDialogContext{ this, session, centerWorld, asBackground, mode };

        static const SDL_DialogFileFilter pdfFilters[] = {
            { "PDF Documents (*.pdf)", "pdf" },
            { "All Files (*.*)", "*" }
        };

        LOG_INFO(CanvasEngine, "Opening native PDF file dialog...");
        SDL_ShowOpenFileDialog(OnPdfFileSelected, ctx, parentWin ? parentWin : sdlWindow, pdfFilters, 2, nullptr, false);
    }

    /**
     * @brief Inserts an image from the OS clipboard directly onto the canvas.
     * Returns true if an image was found and inserted; false if no clipboard image exists (no bloat/sample generated).
     */
    bool InsertImageFromClipboard(DocumentSession* session) {
        if (!session) return false;
        auto activePage = session->GetActivePage();
        if (!activePage) return false;

        const char* mimeTypes[] = { "image/png", "image/jpeg", "image/bmp" };
        for (const char* mime : mimeTypes) {
            if (SDL_HasClipboardData(mime)) {
                size_t dataSize = 0;
                void* clipData = SDL_GetClipboardData(mime, &dataSize);
                if (clipData && dataSize > 0) {
                    BLImage clipImg;
                    if (clipImg.read_from_data(clipData, dataSize) == BL_SUCCESS && !clipImg.is_empty()) {
                        std::string ext = (std::strcmp(mime, "image/jpeg") == 0) ? ".jpg" :
                                          (std::strcmp(mime, "image/bmp") == 0) ? ".bmp" : ".png";
                        std::string relPath = DeduplicateAndSaveImage("", clipData, dataSize, session, ext);

                        auto img = std::make_shared<Folio::ImageContainer>();
                        img->SetImage(clipImg, relPath, 120.0);
                        img->embeddedData.assign(static_cast<const uint8_t*>(clipData), static_cast<const uint8_t*>(clipData) + dataSize);

                        Point2D centerWorld = transform.ScreenToWorld(static_cast<float>(viewportW) * 0.5f, static_cast<float>(viewportH) * 0.5f);
                        img->worldX = centerWorld.x - img->worldWidth * 0.5;
                        img->worldY = centerWorld.y - img->worldHeight * 0.5;
                        img->UpdateBounds();

                        session->AddImage(img);
                        needsFullRebake = true;
                        isDirty = true;
                        SDL_free(clipData);
                        LOG_INFO(CanvasEngine, "Pasted image from clipboard (" + std::to_string(clipImg.width()) + "x" + std::to_string(clipImg.height()) + " px)");
                        return true;
                    }
                    SDL_free(clipData);
                }
            }
        }
        return false;
    }

    bool EraseSegment(float screenX0, float screenY0, float screenX1, float screenY1,
                      double radiusMm, DocumentSession& session, bool isStrokeEraser = true) {
        auto activePage = session.GetActivePage();
        if (!activePage) return false;

        Point2D w0 = transform.ScreenToWorld(screenX0, screenY0);
        Point2D w1 = transform.ScreenToWorld(screenX1, screenY1);
        double r = std::max(0.5, radiusMm);

        AABB sweptBox(
            std::min(w0.x, w1.x) - r,
            std::min(w0.y, w1.y) - r,
            std::max(w0.x, w1.x) + r,
            std::max(w0.y, w1.y) + r
        );

        if (devMode) {
            debugCollision.active = true;
            debugCollision.queryCenter = w1;
            debugCollision.queryRadius = r;
            debugCollision.queryBox = sweptBox;
            debugCollision.candidateUids.clear();
            debugCollision.hitUids.clear();
        }

        std::vector<uint32_t> candidateUids = activePage->spatialIndex.Query(sweptBox);
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
                if (obj->HitTestSwept(w0, w1, r)) {
                    if (devMode) debugCollision.hitUids.push_back(obj->uid);
                    activePage->RemoveObject(obj);
                    modified = true;
                }
            } else {
                // Precision / Point eraser: slices vector ink strokes along continuous swept path
                if (obj->type == ObjectType::InkContainer) {
                    auto ink = std::static_pointer_cast<InkContainer>(obj);
                    double segLen = std::hypot(w1.x - w0.x, w1.y - w0.y);
                    int steps = std::clamp(static_cast<int>(std::ceil(segLen / (r * 0.6))), 1, 30);

                    bool inkModified = false;
                    std::vector<std::shared_ptr<InkContainer>> newFragments;

                    for (int s = (steps > 1 ? 0 : 1); s <= steps; ++s) {
                        double t = (steps == 1) ? 1.0 : (static_cast<double>(s) / steps);
                        double curX = w0.x + t * (w1.x - w0.x);
                        double curY = w0.y + t * (w1.y - w0.y);

                        std::vector<std::shared_ptr<InkContainer>> stepFrags;
                        if (ink->SliceStrokeAt(curX, curY, r, stepFrags)) {
                            inkModified = true;
                            for (auto& frag : stepFrags) {
                                newFragments.push_back(std::move(frag));
                            }
                        }
                    }

                    if (inkModified) {
                        if (devMode) debugCollision.hitUids.push_back(obj->uid);
                        if (ink->strokes.empty()) {
                            activePage->RemoveObject(ink);
                        } else {
                            activePage->UpdateObject(ink);
                        }
                        for (auto& frag : newFragments) {
                            frag->uid = UIDGenerator::Next();
                            activePage->AddObject(frag);
                        }
                        modified = true;
                    }
                } else {
                    // Non-stroke objects (e.g. image, text box, shape): delete on hit
                    if (obj->HitTestSwept(w0, w1, r)) {
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
                LOG_INFO(CanvasEngine, "Erased content on page (strokeEraser=" + std::string(isStrokeEraser ? "true" : "false") + ")");
            }
        }
        return modified;
    }

    bool EraseAt(float screenX, float screenY, double radiusMm, DocumentSession& session, bool isStrokeEraser = true) {
        return EraseSegment(screenX, screenY, screenX, screenY, radiusMm, session, isStrokeEraser);
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

        // Draw active vector shape drag preview (World Coordinates)
        if (shapeCreation.isDragging) {
            double minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
            double maxX = std::max(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
            double minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
            double maxY = std::max(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
            double w = std::max(0.5, maxX - minX);
            double h = std::max(0.5, maxY - minY);

            Folio::ShapeObject preview(shapeCreation.shapeType, minX, minY, w, h);
            preview.fillType = shapeCreation.defaultFillType;
            preview.fillColor = shapeCreation.defaultFillColor;
            preview.outlineType = shapeCreation.defaultOutlineType;
            preview.strokeColor = shapeCreation.defaultOutlineColor;
            preview.strokeWidth = shapeCreation.defaultStrokeWidth;

            Viewport vp;
            vp.zoom = transform.zoom;
            preview.Render(compCtx, vp);
        }

        compCtx.restore();

        // 3. Selection Gizmo Overlay Pass (Screen Coordinates)
        if (selectionGizmo.HasSelection()) {
            selectionGizmo.Render(compCtx, transform);
        }

        // 3b. Marquee Box Selection Overlay (Screen Coordinates)
        if (marqueeBox.isActive) {
            float bx = std::min(marqueeBox.startScreenX, marqueeBox.currentScreenX);
            float by = std::min(marqueeBox.startScreenY, marqueeBox.currentScreenY);
            float bw = std::abs(marqueeBox.currentScreenX - marqueeBox.startScreenX);
            float bh = std::abs(marqueeBox.currentScreenY - marqueeBox.startScreenY);

            compCtx.set_fill_style(BLRgba32(0x00, 0x78, 0xD4, 0x24));
            compCtx.fill_rect(bx, by, bw, bh);

            compCtx.set_stroke_style(BLRgba32(0x00, 0x78, 0xD4, 0xDD));
            compCtx.set_stroke_width(1.5);
            compCtx.stroke_rect(bx, by, bw, bh);
        }

        // 4. Dev Mode: Rnote-Style AABB & Collision Debugger (Screen Coordinates)
        if (devMode) {
            RenderDevModeAABBs(compCtx, visibleBakedObjects, transform);
        }

        // 5. Live Eraser Circular Cursor Reticle (Screen Coordinates)
        if (eraserVisual.isVisible) {
            double radiusPx = eraserVisual.radiusMm * transform.GetEffectiveScale();
            float cx = eraserVisual.screenX;
            float cy = eraserVisual.screenY;

            if (eraserVisual.isStrokeEraser) {
                // Stroke Eraser: ring with centered crosshair
                compCtx.set_stroke_style(BLRgba32(0xFF, 0x40, 0x81, 0xDD));
                compCtx.set_stroke_width(1.5);
                compCtx.stroke_circle(cx, cy, std::max(4.0, radiusPx));
                compCtx.set_fill_style(eraserVisual.isDown ? BLRgba32(0xFF, 0x40, 0x81, 0x2E) : BLRgba32(0xFF, 0x40, 0x81, 0x12));
                compCtx.fill_circle(cx, cy, std::max(4.0, radiusPx));

                compCtx.stroke_line(cx - 3.5, cy, cx + 3.5, cy);
                compCtx.stroke_line(cx, cy - 3.5, cx, cy + 3.5);
            } else {
                // Point Eraser: accurate physical circle showing the exact deletion footprint!
                compCtx.set_stroke_style(BLRgba32(0x29, 0xB6, 0xF6, 0xEE));
                compCtx.set_stroke_width(1.5);
                compCtx.stroke_circle(cx, cy, radiusPx);
                compCtx.set_fill_style(eraserVisual.isDown ? BLRgba32(0x29, 0xB6, 0xF6, 0x3A) : BLRgba32(0x29, 0xB6, 0xF6, 0x14));
                compCtx.fill_circle(cx, cy, radiusPx);

                // Precise center dot
                compCtx.fill_circle(cx, cy, 1.2, BLRgba32(0x29, 0xB6, 0xF6, 0xFF));
            }
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

            // On-The-Fly Narrowphase Segment mini-AABBs (Correctly transformed with stroke affine matrix)
            if (debugShowSegmentAABB && obj->type == ObjectType::InkContainer) {
                auto ink = std::static_pointer_cast<InkContainer>(obj);
                if (ink) {
                    ctx.set_stroke_style(isHit ? BLRgba32(0xFF, 0x52, 0x52, 0x88) : BLRgba32(0x76, 0xFF, 0x03, 0x55));
                    ctx.set_stroke_width(0.75);
                    double scale = std::hypot(ink->transform.m00, ink->transform.m01);
                    for (const auto& stroke : ink->strokes) {
                        for (const auto& seg : stroke.segments) {
                            // Map local segment endpoints through the object's affine transform
                            BLPoint wp0 = ink->transform.map_point(seg.p0.x, seg.p0.y);
                            BLPoint wp1 = ink->transform.map_point(seg.p1.x, seg.p1.y);
                            double worldR = (static_cast<double>(seg.width) * 0.5) * scale;
                            double sMinX = std::min(wp0.x, wp1.x) - worldR;
                            double sMinY = std::min(wp0.y, wp1.y) - worldR;
                            double sMaxX = std::max(wp0.x, wp1.x) + worldR;
                            double sMaxY = std::max(wp0.y, wp1.y) + worldR;

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

            Point2D qCenter = tr.WorldToScreen(debugCollision.queryCenter.x, debugCollision.queryCenter.y);
            double qRadScreen = debugCollision.queryRadius * tr.zoom;

            // Primary Eraser Circle (accurate circular footprint)
            ctx.set_fill_style(BLRgba32(0xF5, 0x00, 0x57, 0x30));
            ctx.fill_circle(qCenter.x, qCenter.y, qRadScreen);
            ctx.set_stroke_style(BLRgba32(0xF5, 0x00, 0x57, 0xFF));
            ctx.set_stroke_width(2.0);
            ctx.stroke_circle(qCenter.x, qCenter.y, qRadScreen);

            // Subtle ghost reference bounds for swept broadphase culling inspection
            ctx.set_stroke_style(BLRgba32(0xF5, 0x00, 0x57, 0x44));
            ctx.set_stroke_width(0.75);
            ctx.stroke_rect(qx, qy, qw, qh);

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

        // 3. Canvas Origin Visualizer (World 0,0 mm)
        Point2D originScreen = tr.WorldToScreen(0.0, 0.0);
        if (originScreen.x >= -150.0 && originScreen.x <= static_cast<double>(viewportW) + 150.0 &&
            originScreen.y >= -150.0 && originScreen.y <= static_cast<double>(viewportH) + 150.0) {
            double ox = originScreen.x;
            double oy = originScreen.y;

            // Target reticle
            ctx.set_stroke_style(BLRgba32(0xFF, 0xD6, 0x00, 0xCC)); // Gold
            ctx.set_stroke_width(1.5);
            ctx.stroke_circle(ox, oy, 8.0);
            ctx.stroke_circle(ox, oy, 16.0);
            ctx.set_fill_style(BLRgba32(0xFF, 0xD6, 0x00, 0x33));
            ctx.fill_circle(ox, oy, 4.0);

            // Axes (+X in Red, +Y in Green)
            ctx.set_stroke_style(BLRgba32(0xFF, 0x17, 0x44, 0xEE)); // +X
            ctx.set_stroke_width(2.0);
            ctx.stroke_line(ox, oy, ox + 45.0, oy);

            ctx.set_stroke_style(BLRgba32(0x00, 0xE6, 0x76, 0xEE)); // +Y
            ctx.set_stroke_width(2.0);
            ctx.stroke_line(ox, oy, ox, oy + 45.0);

            SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(ox + 48.0), static_cast<float>(oy - 4.0), "+X (mm)");
            SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(ox - 6.0), static_cast<float>(oy + 48.0), "+Y (mm)");

            char originLabel[48];
            std::snprintf(originLabel, sizeof(originLabel), "ORIGIN (0,0) mm");
            ctx.set_fill_style(BLRgba32(0x10, 0x14, 0x1E, 0xEE));
            double olW = (std::strlen(originLabel) * 6.5) + 6.0;
            ctx.fill_round_rect(ox + 10.0, oy - 20.0, olW, 14.0, 2.0);
            ctx.set_stroke_style(BLRgba32(0xFF, 0xD6, 0x00, 0xAA));
            ctx.set_stroke_width(0.8);
            ctx.stroke_round_rect(ox + 10.0, oy - 20.0, olW, 14.0, 2.0);
            SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(ox + 13.0), static_cast<float>(oy - 18.0), originLabel);
        }

        // 4. Viewport Center Reticle with World Coordinates
        float screenMidX = static_cast<float>(viewportW) * 0.5f;
        float screenMidY = static_cast<float>(viewportH) * 0.5f;
        Point2D centerWorld = tr.ScreenToWorld(screenMidX, screenMidY);

        ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0x55));
        ctx.set_stroke_width(1.0);
        ctx.stroke_line(screenMidX - 14.0f, screenMidY, screenMidX + 14.0f, screenMidY);
        ctx.stroke_line(screenMidX, screenMidY - 14.0f, screenMidX, screenMidY + 14.0f);
        ctx.stroke_circle(screenMidX, screenMidY, 3.5);

        char centerLabel[64];
        std::snprintf(centerLabel, sizeof(centerLabel), "Ctr: (%.1f, %.1f) mm", centerWorld.x, centerWorld.y);
        ctx.set_fill_style(BLRgba32(0x10, 0x14, 0x1E, 0x99));
        double clW = (std::strlen(centerLabel) * 6.5) + 6.0;
        ctx.fill_round_rect(screenMidX + 6.0, screenMidY + 6.0, clW, 13.0, 2.0);
        SelectionGizmo::DrawFallbackText(ctx, screenMidX + 9.0f, screenMidY + 7.0f, centerLabel);

        // 5. Top-Right HUD Badge: DEV MODE & VIEWPORT TELEMETRY [F4]
        {
            Point2D vpMinWorld = tr.ScreenToWorld(0.0, 0.0);
            Point2D vpMaxWorld = tr.ScreenToWorld(viewportW, viewportH);
            double vpWorldW = vpMaxWorld.x - vpMinWorld.x;
            double vpWorldH = vpMaxWorld.y - vpMinWorld.y;

            const char* modeStr = "Semi-Inf";
            if (tr.infinityMode == CanvasInfinityMode::FullInfinity) modeStr = "Full-Inf";
            else if (tr.infinityMode == CanvasInfinityMode::VerticalScroll) modeStr = "VertScroll";
            else if (tr.infinityMode == CanvasInfinityMode::HorizontalScroll) modeStr = "HorizScroll";

            char line0[96], line1[96], line2[96], line3[96], line4[96], line5[96];
            std::snprintf(line0, sizeof(line0), "DEV[F4] %s | Vis:%zu Hits:%zu",
                          modeStr, visibleObjects.size(), debugCollision.hitUids.size());
            std::snprintf(line1, sizeof(line1), "Zoom: %.1f%% (Scale: %.2f px/mm)",
                          tr.zoom * 100.0, tr.GetEffectiveScale());
            std::snprintf(line2, sizeof(line2), "Pan: (%.1f, %.1f) mm%s",
                          tr.panXMm, tr.panYMm,
                          (tr.infinityMode == CanvasInfinityMode::SemiInfinity && (tr.panXMm == 0.0 || tr.panYMm == 0.0)) ? " [CLAMP]" : "");
            std::snprintf(line3, sizeof(line3), "World Ctr: (%.1f, %.1f) mm",
                          centerWorld.x, centerWorld.y);
            std::snprintf(line4, sizeof(line4), "View Bounds: [%.1f, %.1f] -> [%.1f, %.1f]",
                          vpMinWorld.x, vpMinWorld.y, vpMaxWorld.x, vpMaxWorld.y);
            std::snprintf(line5, sizeof(line5), "View Size: %.1fx%.1f mm (%dx%d px)",
                          vpWorldW, vpWorldH, viewportW, viewportH);

            const char* lines[6] = { line0, line1, line2, line3, line4, line5 };
            float cardW = 325.0f;
            float lineH = 15.0f;
            float cardH = 6 * lineH + 12.0f;
            float cardX = static_cast<float>(viewportW) - cardW - 16.0f;
            float cardY = 16.0f;

            ctx.set_fill_style(BLRgba32(0x0E, 0x12, 0x1C, 0xF2));
            ctx.fill_round_rect(cardX, cardY, cardW, cardH, 5.0f);
            ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0xAA));
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(cardX, cardY, cardW, cardH, 5.0f);

            // Indicator dot: Green if clean, Red if hits active
            BLRgba32 dotColor = debugCollision.hitUids.empty() ? BLRgba32(0x00, 0xE6, 0x76, 0xFF) : BLRgba32(0xFF, 0x17, 0x44, 0xFF);
            ctx.fill_circle(cardX + 11.0f, cardY + 11.0f, 3.5, dotColor);

            // Divider line after header
            ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0x44));
            ctx.stroke_line(cardX + 6.0f, cardY + lineH + 5.0f, cardX + cardW - 6.0f, cardY + lineH + 5.0f);

            for (int i = 0; i < 6; ++i) {
                float textX = cardX + (i == 0 ? 20.0f : 10.0f);
                float textY = cardY + 5.0f + (i * lineH);
                SelectionGizmo::DrawFallbackText(ctx, textX, textY, lines[i]);
            }
        }

        ctx.restore();
    }
};