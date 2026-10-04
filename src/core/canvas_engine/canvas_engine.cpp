#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "utils/usage_tracker.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>

void CanvasEngine::Init(int initialW, int initialH, float displayDpi) {
    transform.SetDPI(displayDpi);

    glGenTextures(1, &glTexture);
    glBindTexture(GL_TEXTURE_2D, glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    Resize(initialW, initialH);
}

void CanvasEngine::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == viewportW && height == viewportH) return;

    viewportW = width;
    viewportH = height;

    layerCompositor.SetSurfaceSize(viewportW, viewportH);

#if defined(__ANDROID__)
    allocatedCapacityW = viewportW;
    allocatedCapacityH = viewportH;

    staticCanvasLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
    liveInkingLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
    compositeSurface.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);

    glBindTexture(GL_TEXTURE_2D, glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedCapacityW, allocatedCapacityH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    if (viewportW > allocatedCapacityW || viewportH > allocatedCapacityH) {
        allocatedCapacityW = std::max(allocatedCapacityW * 2, viewportW);
        allocatedCapacityH = std::max(allocatedCapacityH * 2, viewportH);

        allocatedCapacityW = std::max(allocatedCapacityW, 3840);
        allocatedCapacityH = std::max(allocatedCapacityH, 2160);

        staticCanvasLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
        liveInkingLayer.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);
        compositeSurface.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);

        glBindTexture(GL_TEXTURE_2D, glTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedCapacityW, allocatedCapacityH, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    }
#endif

    isDirty = true;
    needsFullRebake = true;
}

void CanvasEngine::SetDPI(float dpi) noexcept {
    transform.SetDPI(dpi);
    isDirty = true;
    needsFullRebake = true;
}

void CanvasEngine::Pan(double screenDx, double screenDy) noexcept {
    if (screenDx == 0.0 && screenDy == 0.0) return;
    transform.PanByScreenPixels(screenDx, screenDy);
    isDirty = true;
    needsFullRebake = true;
    ::Folio::UsageTracker::Instance().RecordPanGesture();
}

void CanvasEngine::ZoomAt(double screenX, double screenY, double factor) noexcept {
    transform.ZoomAtScreenPoint(screenX, screenY, factor);
    isDirty = true;
    needsFullRebake = true;
    ::Folio::UsageTracker::Instance().RecordZoomGesture();
}

Viewport CanvasEngine::GetViewport() const noexcept {
    return transform.GetVisibleViewportMm(viewportW, viewportH);
}

void CanvasEngine::SetInfinityMode(CanvasInfinityMode mode) {
    infinityMode = mode;
    transform.infinityMode = mode;
    transform.ClampPan();
    isDirty = true;
    needsFullRebake = true;
}

void CanvasEngine::HomeOrCenterPage() noexcept {
    double pageWMm = 215.9, pageHMm = 279.4;
    GetCalculatedPageBoundsMm(pageWMm, pageHMm);
    if (pageWMm <= 0.0) pageWMm = 215.9;

    double vpW = (viewportW > 0) ? static_cast<double>(viewportW) : 1200.0;
    double pxPerMm = transform.pixelsPerMm;
    if (pxPerMm <= 0.0) pxPerMm = 3.7795;

    double targetPagePx = vpW * 0.75;
    double targetZoom = targetPagePx / (pageWMm * pxPerMm);
    if (targetZoom < 0.8) targetZoom = 0.8;
    if (targetZoom > 1.8) targetZoom = 1.8;

    transform.zoom = targetZoom;
    double effectiveScale = pxPerMm * transform.zoom;

    double remainingPx = vpW - (pageWMm * effectiveScale);
    if (remainingPx > 0.0) {
        transform.panXMm = (remainingPx * 0.5) / effectiveScale;
    } else {
        transform.panXMm = 10.0;
    }
    transform.panYMm = 15.0;

    isDirty = true;
    needsFullRebake = true;
}

void CanvasEngine::Render(const std::vector<std::shared_ptr<CanvasObject>>& visibleBakedObjects, DocumentSession* session, double deltaTime) {
    if (viewportW <= 0 || viewportH <= 0) return;
    if (!isDirty && !needsFullRebake) return;

    Viewport currentView = GetViewport();
    CanvasPage* activePage = session ? session->GetActivePage().get() : nullptr;

    std::unordered_map<uint32_t, const CanvasObject*> activeAppObjects;
    if (activePage) {
        for (const auto& obj : activePage->objects) {
            if (obj && obj->isVisible && obj->SupportsOverlay()) {
                activeAppObjects[obj->uid] = obj.get();
            }
        }
    }

    BLContext compCtx(compositeSurface);
    compCtx.clear_all();

    auto& liveLayer = layerCompositor.GetLiveInteractionLayer();

    if (selectionGizmo.HasSelection() && !shapeCreation.isActive && !shapeCreation.isDragging) {
        selectionGizmo.lockToGrid = shapeCreation.lockToGrid;
        selectionGizmo.gridSpacingMm = gridSpacingMm;
        liveLayer.BindSelectionGizmo(&selectionGizmo, &transform);
    } else {
        liveLayer.ClearSelectionGizmo();
    }

    if (marqueeBox.isActive) {
        float bx0 = (std::min)(marqueeBox.startScreenX, marqueeBox.currentScreenX);
        float by0 = (std::min)(marqueeBox.startScreenY, marqueeBox.currentScreenY);
        float bx1 = (std::max)(marqueeBox.startScreenX, marqueeBox.currentScreenX);
        float by1 = (std::max)(marqueeBox.startScreenY, marqueeBox.currentScreenY);
        liveLayer.SetScreenMarquee(bx0, by0, bx1, by1);
    } else {
        liveLayer.ClearScreenMarquee();
    }

    if (eraserVisual.isVisible) {
        double radiusPx = eraserVisual.radiusMm * transform.GetEffectiveScale();
        liveLayer.SetEraserReticle(eraserVisual.screenX, eraserVisual.screenY, radiusPx,
                                   eraserVisual.isDown, eraserVisual.isStrokeEraser);
    } else {
        liveLayer.ClearEraserReticle();
    }

    if (devMode) {
        liveLayer.SetCustomFeedbackRenderer([this, &visibleBakedObjects](BLContext& ctx, const Viewport& /*vp*/) {
            RenderDevModeAABBs(ctx, visibleBakedObjects, transform);
        });
    } else {
        liveLayer.SetCustomFeedbackRenderer(nullptr);
    }

    layerCompositor.RenderFrame(compCtx, activePage, currentView, activeAppObjects, deltaTime);
    compCtx.end();

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
                px[0] = 255 - px[0];
                px[1] = 255 - px[1];
                px[2] = 255 - px[2];
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
