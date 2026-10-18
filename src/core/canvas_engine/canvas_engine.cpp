#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "core/gpu/gpu_ink_manager.hpp"
#include "utils/logger.hpp"
#include <folio_sketch/ink_generator.hpp>
#include <algorithm>
#include <cmath>

void CanvasEngine::Init(int initialW, int initialH, float displayDpi) {
    transform.SetDPI(displayDpi);
    Folio::GpuInkManager::Instance().Init();

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

    compositeSurface.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);

    glBindTexture(GL_TEXTURE_2D, glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedCapacityW, allocatedCapacityH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    if (viewportW > allocatedCapacityW || viewportH > allocatedCapacityH) {
        allocatedCapacityW = std::max(allocatedCapacityW * 2, viewportW);
        allocatedCapacityH = std::max(allocatedCapacityH * 2, viewportH);

        allocatedCapacityW = std::max(allocatedCapacityW, 3840);
        allocatedCapacityH = std::max(allocatedCapacityH, 2160);

        compositeSurface.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);

        glBindTexture(GL_TEXTURE_2D, glTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedCapacityW, allocatedCapacityH, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    }
#endif

    InvalidateLayer();
}

void CanvasEngine::SetDPI(float dpi) noexcept {
    transform.SetDPI(dpi);
    InvalidateLayer();
}

void CanvasEngine::InvalidateLayer() noexcept {
    isDirty = true;
    needsFullRebake = true;
    layerCompositor.InvalidateBakedCanvas();
}

void CanvasEngine::InvalidateLayerRect(const AABB& dirtyBounds) noexcept {
    isDirty = true;
    layerCompositor.InvalidateBakedCanvasRect(dirtyBounds);
}

Folio::ActionContext CanvasEngine::CreateActionContext(DocumentSession* session, double frameTimeSec) noexcept {
    Folio::ActionContext ctx;
    ctx.engine = this;
    ctx.session = session;
    ctx.page = session ? session->GetActivePage().get() : nullptr;
    ctx.history = session ? session->GetCommandManager() : nullptr;
    ctx.layerManager = &layerCompositor;
    ctx.frameTimeSeconds = frameTimeSec;
    ctx.onInvalidateLayer = [this]() {
        InvalidateLayer();
    };
    ctx.onInvalidateLayerRect = [this](const AABB& bounds) {
        InvalidateLayerRect(bounds);
    };
    return ctx;
}

void CanvasEngine::Update(double dt, DocumentSession* session) {
    auto ctx = CreateActionContext(session, 0.0);
    actionManager.Update(dt, ctx);
    if (session && session->UpdateTemporaryObjects(dt, isCurrentlyInking)) {
        InvalidateLayer();
    }
}

void CanvasEngine::Pan(double screenDx, double screenDy) noexcept {
    if (screenDx == 0.0 && screenDy == 0.0) return;
    transform.PanByScreenPixels(screenDx, screenDy);
    InvalidateLayer();
}

void CanvasEngine::ZoomAt(double screenX, double screenY, double factor) noexcept {
    transform.ZoomAtScreenPoint(screenX, screenY, factor);
    InvalidateLayer();
}

Viewport CanvasEngine::GetViewport() const noexcept {
    return transform.GetVisibleViewportMm(viewportW, viewportH);
}

void CanvasEngine::SetInfinityMode(CanvasInfinityMode mode) {
    infinityMode = mode;
    transform.infinityMode = mode;
    transform.ClampPan();
    InvalidateLayer();
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

    InvalidateLayer();
}

void CanvasEngine::Render(const std::vector<std::shared_ptr<CanvasObject>>& visibleBakedObjects, DocumentSession* session, double deltaTime) {
    // Process asynchronous tasks dispatched from background platform threads (e.g. SDL dialog callbacks)
    ProcessMainThreadTasks();

    if (viewportW <= 0 || viewportH <= 0) return;

    layerCompositor.GetBakedCanvasLayer().SetPaperTheme(canvasBgColor, gridLineColor, pageBorderColor);

    bool wasFullRebake = needsFullRebake;
    if (needsFullRebake) {
        layerCompositor.InvalidateBakedCanvas();
        needsFullRebake = false;
    }

    if (!isDirty && !layerCompositor.GetBakedCanvasLayer().IsDirty() && !layerCompositor.GetLiveInteractionLayer().HasActiveInteraction()) {
        double dtClamped = std::clamp(deltaTime, 0.001, 0.1);
        pcieWindowElapsedSec += dtClamped;
        if (pcieWindowElapsedSec >= 0.25) {
            pcieBandwidthMBps = (static_cast<double>(pcieBytesAccumulated) / (1024.0 * 1024.0)) / pcieWindowElapsedSec;
            pcieBytesAccumulated = 0;
            pcieWindowElapsedSec = 0.0;
        }
        return;
    }

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

    if (debugShowInkWireframe) {
        InkGenerator::SetDebugCapture(true);
    }

    if (devMode || debugShowInkWireframe) {
        liveLayer.SetCustomFeedbackRenderer([this, &visibleBakedObjects](BLContext& ctx, const Viewport& vp) {
            if (devMode) {
                RenderDevModeAABBs(ctx, visibleBakedObjects, transform);
            }
            if (debugShowInkWireframe) {
                ctx.save();
                ctx.set_transform(vp.worldToScreenMatrix);
                InkGenerator::RenderDebugOverlay(ctx, InkGenerator::GetLastDebugInfo(), vp.GetEffectiveScale());
                ctx.restore();
            }
        });
    } else {
        liveLayer.SetCustomFeedbackRenderer(nullptr);
    }

    // -------------------------------------------------------------------------
    // PCIe BUS BANDWIDTH OPTIMIZATION: Sub-image Dirty Rect Determination
    // -------------------------------------------------------------------------
    AABB currLiveBounds = liveLayer.GetInteractionScreenBounds(currentView);

    bool canUsePartialUpload = false;
    int dirtyX = 0, dirtyY = 0, dirtyW = 0, dirtyH = 0;

    if (!wasFullRebake && !layerCompositor.GetBakedCanvasLayer().IsFullRebakePending()) {
        AABB frameDirtyBox;
        if (currLiveBounds.IsValid() && !currLiveBounds.IsEmpty()) {
            frameDirtyBox.Merge(currLiveBounds);
        }
        if (m_prevLiveScreenBounds.IsValid() && !m_prevLiveScreenBounds.IsEmpty()) {
            frameDirtyBox.Merge(m_prevLiveScreenBounds);
        }

        const AABB& bakedDirtyWorld = layerCompositor.GetBakedCanvasLayer().GetLastRenderedDirtyWorldRegion();
        if (bakedDirtyWorld.IsValid() && !bakedDirtyWorld.IsEmpty()) {
            BLRect sRect = currentView.WorldToScreenRect(bakedDirtyWorld);
            frameDirtyBox.Merge(sRect.x, sRect.y);
            frameDirtyBox.Merge(sRect.x + sRect.w, sRect.y + sRect.h);
        }

        if (frameDirtyBox.IsValid() && !frameDirtyBox.IsEmpty()) {
            dirtyX = std::clamp(static_cast<int>(std::floor(frameDirtyBox.minX)), 0, viewportW - 1);
            dirtyY = std::clamp(static_cast<int>(std::floor(frameDirtyBox.minY)), 0, viewportH - 1);
            int dirtyR = std::clamp(static_cast<int>(std::ceil(frameDirtyBox.maxX)), 1, viewportW);
            int dirtyB = std::clamp(static_cast<int>(std::ceil(frameDirtyBox.maxY)), 1, viewportH);
            dirtyW = dirtyR - dirtyX;
            dirtyH = dirtyB - dirtyY;

            double dirtyArea = static_cast<double>(dirtyW) * static_cast<double>(dirtyH);
            double totalArea = static_cast<double>(viewportW) * static_cast<double>(viewportH);

            if (dirtyW > 0 && dirtyH > 0 && dirtyArea < totalArea * 0.70) {
                canUsePartialUpload = true;
            }
        }
    }

    BLContext compCtx(compositeSurface);
    if (canUsePartialUpload) {
        compCtx.clip_to_rect(BLRectI(dirtyX, dirtyY, dirtyW, dirtyH));
        Folio::GpuInkManager::Instance().SetActiveClipRect(dirtyX, dirtyY, dirtyW, dirtyH);
    } else {
        compCtx.clear_all();
        Folio::GpuInkManager::Instance().SetActiveClipRect(0, 0, viewportW, viewportH);
    }

    layerCompositor.RenderFrame(compCtx, activePage, currentView, activeAppObjects, deltaTime);
    Folio::GpuInkManager::Instance().ClearActiveClipRect();
    compCtx.end();

    if (inkColorInverted) {
        BLImageData invertData;
        compositeSurface.get_data(&invertData);
        uint8_t* pixels = static_cast<uint8_t*>(invertData.pixel_data);
        intptr_t stride = invertData.stride;
        int rowStart = canUsePartialUpload ? dirtyY : 0;
        int rowEnd   = canUsePartialUpload ? (dirtyY + dirtyH) : viewportH;
        int colStart = canUsePartialUpload ? dirtyX : 0;
        int colEnd   = canUsePartialUpload ? (dirtyX + dirtyW) : viewportW;

        for (int row = rowStart; row < rowEnd; ++row) {
            uint8_t* rowPtr = pixels + row * stride;
            for (int col = colStart; col < colEnd; ++col) {
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

    if (canUsePartialUpload) {
        lastUploadedBytes = static_cast<size_t>(dirtyW) * static_cast<size_t>(dirtyH) * 4;
        lastUploadWasPartial = true;
        lastDirtyW = dirtyW;
        lastDirtyH = dirtyH;
#if defined(__ANDROID__)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, dirtyX);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, dirtyY);
        glTexSubImage2D(GL_TEXTURE_2D, 0, dirtyX, dirtyY, dirtyW, dirtyH, GL_RGBA, GL_UNSIGNED_BYTE, imgData.pixel_data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#else
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, dirtyX);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, dirtyY);
        glTexSubImage2D(GL_TEXTURE_2D, 0, dirtyX, dirtyY, dirtyW, dirtyH, GL_BGRA, GL_UNSIGNED_BYTE, imgData.pixel_data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#endif
    } else {
        lastUploadedBytes = static_cast<size_t>(viewportW) * static_cast<size_t>(viewportH) * 4;
        lastUploadWasPartial = false;
        lastDirtyW = viewportW;
        lastDirtyH = viewportH;
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
    }

    m_prevLiveScreenBounds = currLiveBounds;
    isDirty = false;

    double dtClamped = std::clamp(deltaTime, 0.001, 0.1);
    pcieBytesAccumulated += lastUploadedBytes;
    pcieWindowElapsedSec += dtClamped;
    if (pcieWindowElapsedSec >= 0.25) {
        pcieBandwidthMBps = (static_cast<double>(pcieBytesAccumulated) / (1024.0 * 1024.0)) / pcieWindowElapsedSec;
        pcieBytesAccumulated = 0;
        pcieWindowElapsedSec = 0.0;
    }
}

/**
 * @brief Thread-safe dispatch of tasks to be executed on the main UI/rendering thread.
 *
 * Architecture & Working Process:
 * 1. Background threads (e.g. SDL3 dialog callbacks running on Android JNI thread) invoke this method.
 * 2. Acquires m_mainThreadTasksMutex and pushes the task functor into m_mainThreadTasks.
 * 3. Calls InvalidateLayer() to wake the render loop and schedule the next frame.
 *
 * @param task The lambda or std::function to execute on the main thread.
 */
void CanvasEngine::EnqueueMainThreadTask(std::function<void()> task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(m_mainThreadTasksMutex);
        m_mainThreadTasks.push_back(std::move(task));
    }
    InvalidateLayer();
}

/**
 * @brief Flushes and executes all pending main-thread tasks in FIFO order.
 *
 * Working Process:
 * 1. Quickly swaps the queued task list under m_mainThreadTasksMutex to minimize lock contention.
 * 2. Sequentially executes each task on the current calling thread (main render thread).
 * 3. Catches and logs any unexpected exceptions to guarantee application resilience.
 */
void CanvasEngine::ProcessMainThreadTasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mainThreadTasksMutex);
        if (m_mainThreadTasks.empty()) return;
        tasks.swap(m_mainThreadTasks);
    }

    for (auto& task : tasks) {
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                LOG_ERROR(CanvasEngine, std::string("Exception in MainThreadTask: ") + e.what());
            } catch (...) {
                LOG_ERROR(CanvasEngine, "Unknown exception in MainThreadTask");
            }
        }
    }
}
