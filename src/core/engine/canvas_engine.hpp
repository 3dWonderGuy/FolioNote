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
#include "core/objects/connectors/smart_arrow_container.hpp"
#include "core/objects/attachment_container.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/objects/text/text_editor_state.hpp"
#include "core/storage/pdf_storage.hpp"
#include "core/engine/canvas_transform.hpp"
#include "core/engine/live_layer_pipeline.hpp"
#include "core/engine/selection_gizmo.hpp"
#include "core/document/document_session.hpp"
#include "core/history/canvas_command.hpp"
#include "utils/usage_tracker.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"
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
#include <chrono>
#include <SDL3/SDL_dialog.h>

// On Windows: include Win32 common dialog (GetOpenFileNameW) for reliable
// native file pickers that always show — SDL3's IFileDialog path can silently
// fail if COM initialisation or filter string parsing hits an edge case.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>  // GetOpenFileNameW / OPENFILENAMEW
#endif

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

/**
 * @struct EphemeralStroke
 * @brief Represents a transient presentation stroke (e.g. Laser Pointer) that decays quadratically over time.
 *
 * MATHEMATICAL PROCESS & DECAY MODEL:
 * - Ephemeral strokes are transient annotations designed for presentations, teaching, and screen-sharing.
 * - They reside entirely in volatile memory and are NEVER committed to SQLite storage or CommandHistory.
 * - Temporal Quadratic Decay:
 *     Given elapsed time Δt = t_current - t_start and lifetime T = durationMs:
 *       progress = Δt / T,  progress ∈ [0.0, 1.0]
 *       decay_factor = (1.0 - progress)^2
 *       α(t) = α_base * decay_factor
 * - The quadratic model maintains strong visibility during the initial stroke gesture, then drops off
 *   cleanly and smoothly toward zero without an abrupt linear pop.
 */
struct EphemeralStroke {
    BLPath outlinePath;
    BLRgba32 color;
    uint64_t startTimeMs = 0;
    uint32_t durationMs = 2500;
};

class CanvasEngine {
public:
    CanvasTransform transform;
    LiveLayerPipeline liveLayer;
    SelectionGizmo selectionGizmo;
    Folio::TextEditorState textEditor;

    // -------------------------------------------------------------------------
    // DEFAULT TYPOGRAPHY SETTINGS (Basic Text Ribbon Group & Click-to-Type)
    // -------------------------------------------------------------------------
    std::string defaultTextFontFamily = "Segoe UI";
    float defaultTextFontSize = 14.0f;
    bool defaultTextBold = false;
    bool defaultTextItalic = false;
    bool defaultTextUnderline = false;
    bool defaultTextStrikethrough = false;
    BLRgba32 defaultTextColor{0x1F, 0x29, 0x37, 0xFF};
    BLRgba32 defaultTextHighlightColor{0x00, 0x00, 0x00, 0x00};
    uint8_t defaultTextAlignment = 0; // 0: Left, 1: Center, 2: Right

    // Ephemeral Presentation Ink / Laser Pointer storage
    std::vector<EphemeralStroke> ephemeralStrokes;

    /**
     * @brief Appends a laser pointer or presentation stroke for temporal quadratic fading.
     * @param path Outline geometry in world coordinates (millimeters).
     * @param color Base stroke color including alpha.
     * @param durationMs Duration in milliseconds before stroke completely vanishes (default: 2500ms).
     */
    void AddEphemeralStroke(BLPath path, BLRgba32 color, uint32_t durationMs = 2500) {
        auto nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        ephemeralStrokes.push_back(EphemeralStroke{std::move(path), color, nowMs, durationMs});
        isDirty = true;
    }

    /**
     * @brief Instantly purges all active ephemeral presentation strokes from RAM.
     */
    void ClearEphemeralStrokes() {
        ephemeralStrokes.clear();
        isDirty = true;
    }

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
            if (tool.penType == PenType::LaserPointer) {
                // Laser Pointer / Ephemeral Presentation Ink:
                // Transient visual feedback that decays quadratically over 2.5s and is never committed
                // to persistent SQLite storage or CommandHistory.
                if (session.HasEphemeralStrokeSink()) {
                    session.CommitEphemeralStroke(std::move(data), tool, 2500);
                } else {
                    AddEphemeralStroke(std::move(data.outlinePath), tool.color, 2500);
                }
            } else {
                session.CommitStroke(std::move(data), tool);
                ::Folio::UsageTracker::Instance().RecordStrokeCommitted();
                ::Folio::UsageTracker::Instance().RecordObjectCreated();
            }
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
                    if (obj && obj->isVisible && obj->isSelectable && obj->bounds.Intersects(lassoBox)) {
                        double objArea = obj->bounds.Area();
                        if (objArea > 1e-4) {
                            double isectArea = lassoBox.IntersectionArea(obj->bounds);
                            double coverageRatio = isectArea / objArea;
                            if (coverageRatio >= 0.50) {
                                obj->isSelected = 1;
                            }
                        } else {
                            // Degenerate/point-sized object fully inside lassoBox
                            if (lassoBox.Contains((obj->bounds.minX + obj->bounds.maxX) * 0.5,
                                                  (obj->bounds.minY + obj->bounds.maxY) * 0.5)) {
                                obj->isSelected = 1;
                            }
                        }
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
                        if (obj && obj->isVisible && obj->isSelectable && obj->bounds.Intersects(box)) {
                            double objArea = obj->bounds.Area();
                            if (objArea > 1e-4) {
                                double isectArea = box.IntersectionArea(obj->bounds);
                                double coverageRatio = isectArea / objArea;
                                if (coverageRatio >= 0.50) {
                                    obj->isSelected = 1;
                                }
                            } else {
                                if (box.Contains((obj->bounds.minX + obj->bounds.maxX) * 0.5,
                                                 (obj->bounds.minY + obj->bounds.maxY) * 0.5)) {
                                    obj->isSelected = 1;
                                }
                            }
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

    /**
     * @brief Deletes all currently selected objects on the active page via DocumentSession.
     * Safe operation: If nothing is selected, returns false without modifying any objects.
     *
     * @param session Pointer to DocumentSession.
     * @return true if objects were selected and deleted, false if selection was empty.
     */
    bool DeleteSelectedObjects(DocumentSession* session = nullptr) {
        if (!session) return false;
        size_t deletedCount = session->DeleteSelection();
        if (deletedCount > 0) {
            selectionGizmo.ClearSelection();
            needsFullRebake = true;
            isDirty = true;
            return true;
        }
        return false;
    }

    /**
     * @brief Selects all visible, selectable, unlocked objects on the active page and attaches the SelectionGizmo.
     * @param session Pointer to DocumentSession.
     * @return Number of objects selected.
     */
    size_t SelectAll(DocumentSession* session = nullptr) {
        if (!session) return 0;
        size_t count = session->SelectAll();
        if (count > 0) {
            selectionGizmo.SetSelectedObjects(session->GetSelectedObjects());
            isDirty = true;
        }
        return count;
    }

    struct ShapeCreationState {
        bool isActive = false;
        bool isDragging = false;
        Folio::ShapeType shapeType = Folio::ShapeType::Rectangle;
        bool lockDrawingMode = false;
        bool lockToGrid = false;          ///< When true, snaps coordinates to canvas.gridSpacingMm (graph paper grid)
        int polygonSides = 6;             ///< Number of sides for RegularPolygon / Hexagon (default 6)

        // Ellipse multi-step creation state:
        // step 0: waiting/dragging major radius from center
        // step 1: major radius fixed, moving mouse to adjust minor radius (thickness), click to finalize
        int ellipseStep = 0;
        Point2D ellipseCenter{0.0, 0.0};
        Point2D ellipseMajorPoint{0.0, 0.0};
        double ellipseMajorRadius = 0.0;
        double ellipseMinorRadius = 0.0;

        Point2D startWorld{0.0, 0.0};
        Point2D currentWorld{0.0, 0.0};
        float startScreenX = 0.0f;
        float startScreenY = 0.0f;
        float currentScreenX = 0.0f;
        float currentScreenY = 0.0f;

        // Current default properties for newly created shapes
        Folio::ShapeFillType defaultFillType = Folio::ShapeFillType::None; // By default shapes are non-colored
        BLRgba32 defaultFillColor = BLRgba32(0x00, 0x78, 0xD4, 0x40);
        Folio::ShapeOutlineType defaultOutlineType = Folio::ShapeOutlineType::Solid;
        BLRgba32 defaultOutlineColor = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
        double defaultStrokeWidth = 1.0;
        Folio::ArrowHeadType defaultEndArrow = Folio::ArrowHeadType::Triangle;
        // Magnetic snap & smart connector state
        bool isSnapped = false;
        Point2D snapAnchorPoint{0.0, 0.0};
        Folio::ConnectorStyle defaultConnectorStyle = Folio::ConnectorStyle::Straight;
    } shapeCreation;

    Point2D SnapToGridIfNeeded(const Point2D& pt) const {
        if (!shapeCreation.lockToGrid || gridSpacingMm <= 0.001) return pt;
        return Point2D(
            std::round(pt.x / gridSpacingMm) * gridSpacingMm,
            std::round(pt.y / gridSpacingMm) * gridSpacingMm
        );
    }

    void StartShapeCreation(Folio::ShapeType type, bool lockMode = false, DocumentSession* session = nullptr) {
        if (session) {
            ClearSelection(session);
        }
        selectionGizmo.ClearSelection();
        shapeCreation.isActive = true;
        shapeCreation.isDragging = false;
        shapeCreation.ellipseStep = 0;
        shapeCreation.shapeType = type;
        shapeCreation.lockDrawingMode = lockMode;
        shapeCreation.isSnapped = false;
        isDirty = true;
        LOG_INFO(CanvasEngine, "Started shape creation mode for type=" + std::to_string(static_cast<int>(type)) +
                 (lockMode ? " [LOCKED]" : " [ONE-SHOT]"));
    }

    void CancelShapeCreation() {
        shapeCreation.isActive = false;
        shapeCreation.isDragging = false;
        shapeCreation.ellipseStep = 0;
        shapeCreation.lockDrawingMode = false;
        shapeCreation.isSnapped = false;
        isDirty = true;
    }

    void OnShapeDrawDown(float screenX, float screenY, DocumentSession* session = nullptr) {
        if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1) {
            // Clicking during step 1 finalizes the ellipse; handled in OnShapeDrawUp.
            shapeCreation.isDragging = true;
            return;
        }

        shapeCreation.isDragging = true;
        shapeCreation.startScreenX = screenX;
        shapeCreation.startScreenY = screenY;
        shapeCreation.currentScreenX = screenX;
        shapeCreation.currentScreenY = screenY;
        Point2D rawStart = transform.ScreenToWorld(screenX, screenY);
        shapeCreation.startWorld = SnapToGridIfNeeded(rawStart);

        // Magnetic snap for line/arrow start point
        if ((shapeCreation.shapeType == Folio::ShapeType::Line || shapeCreation.shapeType == Folio::ShapeType::LineArrow) && session) {
            if (auto pg = session->GetActivePage()) {
                Point2D snapAnchor;
                if (Folio::SmartArrowObject::FindSnapAnchor(shapeCreation.startWorld, pg->objects, snapAnchor, 6.0)) {
                    shapeCreation.startWorld = snapAnchor;
                }
            }
        }

        shapeCreation.currentWorld = shapeCreation.startWorld;
        shapeCreation.isSnapped = false;
        isDirty = true;
    }

    void OnShapeDrawMove(float screenX, float screenY, DocumentSession* session = nullptr) {
        shapeCreation.currentScreenX = screenX;
        shapeCreation.currentScreenY = screenY;
        Point2D rawWorld = transform.ScreenToWorld(screenX, screenY);
        shapeCreation.currentWorld = SnapToGridIfNeeded(rawWorld);

        if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1) {
            // Step 2 of ellipse creation: adjust minor radius (thickness) relative to major radius
            double d = std::hypot(shapeCreation.currentWorld.x - shapeCreation.ellipseCenter.x,
                                  shapeCreation.currentWorld.y - shapeCreation.ellipseCenter.y);
            shapeCreation.ellipseMinorRadius = std::clamp(d, 1.0, std::max(2.0, shapeCreation.ellipseMajorRadius));
            isDirty = true;
            return;
        }

        // Magnetic snap for line/arrow tip
        if ((shapeCreation.shapeType == Folio::ShapeType::Line || shapeCreation.shapeType == Folio::ShapeType::LineArrow) && session) {
            if (auto pg = session->GetActivePage()) {
                Point2D snapAnchor;
                if (Folio::SmartArrowObject::FindSnapAnchor(shapeCreation.currentWorld, pg->objects, snapAnchor, 6.0)) {
                    shapeCreation.snapAnchorPoint = snapAnchor;
                    shapeCreation.currentWorld = snapAnchor;
                    shapeCreation.isSnapped = true;
                } else {
                    shapeCreation.isSnapped = false;
                }
            }
        } else {
            shapeCreation.isSnapped = false;
        }

        if (shapeCreation.isDragging) {
            isDirty = true;
        }
    }

    std::shared_ptr<CanvasObject> OnShapeDrawUp(DocumentSession* session) {
        if (!shapeCreation.isDragging && shapeCreation.ellipseStep == 0) return nullptr;

        if (!session) return nullptr;
        auto activePage = session->GetActivePage();
        if (!activePage) return nullptr;

        // Ellipse step 1 finalization: click places the completed ellipse
        if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1) {
            double rx = shapeCreation.ellipseMajorRadius;
            double ry = shapeCreation.ellipseMinorRadius;
            if (rx < 1.0) rx = 10.0;
            if (ry < 1.0) ry = 10.0;

            double cx = shapeCreation.ellipseCenter.x;
            double cy = shapeCreation.ellipseCenter.y;
            double minX = cx - rx;
            double minY = cy - ry;
            double w = rx * 2.0;
            double h = ry * 2.0;

            auto shp = std::make_shared<Folio::ShapeObject>(Folio::ShapeType::Ellipse, minX, minY, w, h);
            shp->guuid = GUIDGenerator::GenerateV4();
            shp->uid = UIDGenerator::Next();
            shp->fillType = shapeCreation.defaultFillType;
            shp->fillColor = shapeCreation.defaultFillColor;
            shp->outlineType = shapeCreation.defaultOutlineType;
            shp->strokeColor = shapeCreation.defaultOutlineColor;
            shp->strokeWidth = shapeCreation.defaultStrokeWidth;
            shp->UpdateBounds();

            activePage->AddObject(shp, true);
            if (session) {
                session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(shp));
            }
            ClearSelection(session);
            shp->isSelected = 1;
            selectionGizmo.SetSelectedObjects(activePage->objects);

            shapeCreation.ellipseStep = 0;
            shapeCreation.isDragging = false;
            shapeCreation.isSnapped = false;
            if (!shapeCreation.lockDrawingMode) {
                shapeCreation.isActive = false;
            }
            needsFullRebake = true;
            isDirty = true;
            return shp;
        }

        shapeCreation.isDragging = false;

        // Threshold guard: shapes must be dragged out. Accidental clicks/taps (< 2.5mm) do NOT spawn a shape!
        double dragDist = std::hypot(shapeCreation.currentWorld.x - shapeCreation.startWorld.x,
                                     shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
        if (dragDist < 2.5) {
            LOG_INFO(CanvasEngine, "Shape drag cancelled: drag distance below 2.5mm threshold - clearing selection");
            shapeCreation.isDragging = false;
            shapeCreation.ellipseStep = 0;
            shapeCreation.isSnapped = false;
            ClearSelection(session);
            selectionGizmo.ClearSelection();
            needsFullRebake = true;
            isDirty = true;
            return nullptr;
        }

        // Ellipse Step 0: User just dragged out the major radius. Advance to Step 1 for thickness adjustment!
        if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 0) {
            shapeCreation.ellipseStep = 1;
            shapeCreation.ellipseCenter = shapeCreation.startWorld;
            shapeCreation.ellipseMajorPoint = shapeCreation.currentWorld;
            shapeCreation.ellipseMajorRadius = dragDist;
            shapeCreation.ellipseMinorRadius = dragDist; // starts circular
            isDirty = true;
            LOG_INFO(CanvasEngine, "Ellipse Step 1: Major radius set to " + std::to_string(dragDist) + "mm. Move mouse to adjust thickness, click to place.");
            return nullptr;
        }

        // Handle Line and LineArrow promotion to SmartArrowObject with 2-point handles
        if (shapeCreation.shapeType == Folio::ShapeType::Line ||
            shapeCreation.shapeType == Folio::ShapeType::LineArrow) {
            auto arrow = std::make_shared<Folio::SmartArrowObject>(
                shapeCreation.startWorld.x, shapeCreation.startWorld.y,
                shapeCreation.currentWorld.x, shapeCreation.currentWorld.y
            );
            arrow->guuid = GUIDGenerator::GenerateV4();
            arrow->uid = UIDGenerator::Next();
            arrow->strokeColor = shapeCreation.defaultOutlineColor;
            arrow->strokeWidth = shapeCreation.defaultStrokeWidth;
            arrow->outlineType = shapeCreation.defaultOutlineType;
            arrow->connectorStyle = shapeCreation.defaultConnectorStyle;
            arrow->startArrow = Folio::ArrowHeadType::None;
            arrow->endArrow = (shapeCreation.shapeType == Folio::ShapeType::LineArrow)
                              ? shapeCreation.defaultEndArrow
                              : Folio::ArrowHeadType::None;
            arrow->arrowHeadSize = 4.0;
            arrow->UpdateBounds();

            activePage->AddObject(arrow, true);
            if (session) {
                session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(arrow));
            }

            ClearSelection(session);
            arrow->isSelected = 1;
            selectionGizmo.SetSelectedObjects(activePage->objects);

            shapeCreation.isDragging = false;
            shapeCreation.isSnapped = false;
            if (!shapeCreation.lockDrawingMode) {
                shapeCreation.isActive = false;
            }
            needsFullRebake = true;
            isDirty = true;

            LOG_INFO(CanvasEngine, "Created SmartArrowObject connector (uid=" + std::to_string(arrow->uid) +
                     ", style=" + std::to_string(static_cast<int>(arrow->connectorStyle)) +
                     ", from [" + std::to_string(arrow->x1) + "," + std::to_string(arrow->y1) + "] to [" +
                     std::to_string(arrow->x2) + "," + std::to_string(arrow->y2) + "])");
            return arrow;
        }

        double minX = 0.0, minY = 0.0, w = 0.0, h = 0.0;

        if (shapeCreation.shapeType == Folio::ShapeType::Circle) {
            // Circle starts from center as a dot and expands symmetrically
            double r = dragDist;
            w = r * 2.0;
            h = r * 2.0;
            minX = shapeCreation.startWorld.x - r;
            minY = shapeCreation.startWorld.y - r;
        }
        else if (shapeCreation.shapeType == Folio::ShapeType::Hexagon ||
                 shapeCreation.shapeType == Folio::ShapeType::RegularPolygon) {
            // Onshape style: draw radius from center, polygon vertices circumscribed
            double r = dragDist;
            w = r * 2.0;
            h = r * 2.0;
            minX = shapeCreation.startWorld.x - r;
            minY = shapeCreation.startWorld.y - r;
        }
        else if (shapeCreation.shapeType == Folio::ShapeType::SineWave ||
                 shapeCreation.shapeType == Folio::ShapeType::SquareWave ||
                 shapeCreation.shapeType == Folio::ShapeType::TriangleWave ||
                 shapeCreation.shapeType == Folio::ShapeType::RightTriangleWave) {
            // Wave shape creation: horizontal drag sets length (w).
            // If user drags primarily horizontally (vertical delta < 4mm), set default height of 20mm
            // (10mm amplitude) centered on start line so wave does not collapse to zero amplitude.
            minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
            w = std::max(0.5, std::abs(shapeCreation.currentWorld.x - shapeCreation.startWorld.x));
            double rawH = std::abs(shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
            if (rawH < 4.0) {
                h = 20.0;
                minY = shapeCreation.startWorld.y - 10.0;
            } else {
                minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
                h = std::max(0.5, rawH);
            }
        }
        else {
            // Rectangle, RoundedRect, Triangle, RightTriangle, Star, etc. (bounding box drag)
            minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
            minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
            w = std::abs(shapeCreation.currentWorld.x - shapeCreation.startWorld.x);
            h = std::abs(shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
        }

        auto shp = std::make_shared<Folio::ShapeObject>(shapeCreation.shapeType, minX, minY, w, h);
        shp->guuid = GUIDGenerator::GenerateV4();
        shp->uid = UIDGenerator::Next();
        shp->fillType = shapeCreation.defaultFillType;
        shp->fillColor = shapeCreation.defaultFillColor;
        shp->outlineType = shapeCreation.defaultOutlineType;
        shp->strokeColor = shapeCreation.defaultOutlineColor;
        shp->strokeWidth = shapeCreation.defaultStrokeWidth;
        if (shapeCreation.shapeType == Folio::ShapeType::Hexagon ||
            shapeCreation.shapeType == Folio::ShapeType::RegularPolygon) {
            shp->param1 = static_cast<double>(shapeCreation.polygonSides);
        } else if (shapeCreation.shapeType == Folio::ShapeType::SineWave ||
                   shapeCreation.shapeType == Folio::ShapeType::SquareWave ||
                   shapeCreation.shapeType == Folio::ShapeType::TriangleWave ||
                   shapeCreation.shapeType == Folio::ShapeType::RightTriangleWave) {
            shp->param1 = 3.0; // Default 3 cycles across length
            shp->param2 = 0.0; // Default: right angle on right
        }
        shp->UpdateBounds();

        activePage->AddObject(shp, true);
        if (session) {
            session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(shp));
        }

        // Select the newly created shape so the user can immediately transform or operate with it
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

    std::shared_ptr<Folio::SmartArrowObject> GetSelectedConnector(DocumentSession* session) const {
        if (!session) return nullptr;
        auto activePage = session->GetActivePage();
        if (!activePage) return nullptr;
        for (const auto& obj : activePage->objects) {
            if (obj && obj->isSelected && obj->type == ObjectType::Connector) {
                return std::dynamic_pointer_cast<Folio::SmartArrowObject>(obj);
            }
        }
        return nullptr;
    }

    /**
     * @brief Resolves the currently selected or actively edited TextBoxObject.
     */
    std::shared_ptr<Folio::TextBoxObject> GetSelectedTextBox(DocumentSession* session) const {
        if (!session) return nullptr;
        auto activePage = session->GetActivePage();
        if (!activePage) return nullptr;
        for (const auto& obj : activePage->objects) {
            if (obj && obj->type == ObjectType::Text) {
                if (obj->isSelected || (textEditor.IsActive() && obj.get() == textEditor.GetTarget())) {
                    return std::dynamic_pointer_cast<Folio::TextBoxObject>(obj);
                }
            }
        }
        return nullptr;
    }

    /**
     * @brief Inserts a new TextBoxObject onto the active canvas page and activates editor.
     */
    std::shared_ptr<Folio::TextBoxObject> InsertTextBox(DocumentSession* session, double worldX = 0.0, double worldY = 0.0) {
        if (!session) return nullptr;
        auto activePage = session->GetActivePage();
        if (!activePage) return nullptr;

        if (worldX == 0.0 && worldY == 0.0) {
            Point2D center = transform.ScreenToWorld(viewportW * 0.5f, viewportH * 0.5f);
            worldX = center.x - 35.0;
            worldY = center.y - 10.0;
        }

        auto box = std::make_shared<Folio::TextBoxObject>(worldX, worldY, 70.0, 20.0);
        session->AddTextBox(box);
        textEditor.Attach(box.get());
        needsFullRebake = true;
        isDirty = true;
        return box;
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

        activePage->AddObject(shp, true);
        if (session) {
            session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(shp));
        }

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
    // =========================================================================
    // ATTACHMENT — MODAL-DEFERRED FLOW
    // =========================================================================
    // The attach flow is split into two phases so the user can choose Embed vs Link:
    //   Phase 1: OpenAttachmentFileDialog() → OS file picker → stores pending state → sets m_attachModalOpen = true.
    //   Phase 2: app.hpp draws the modal each frame. On user choice, calls CommitAttachment(embed, session).
    // =========================================================================

    /// True while the "Embed vs Link" modal is waiting for a user decision.
    bool m_attachModalOpen = false;

    /// File path chosen in the OS dialog, pending user's embed/link decision.
    std::string m_pendingAttachPath;

    /// Display name (filename leaf) for the pending attachment.
    std::string m_pendingAttachName;

    /// Session that was active when the file picker was opened.
    DocumentSession* m_pendingAttachSession = nullptr;

    // -------------------------------------------------------------------------
    // Internal SDL callback context (cross-platform async path only)
    // -------------------------------------------------------------------------
    struct AttachmentFileDialogContext {
        CanvasEngine* canvas   = nullptr;
        DocumentSession* session = nullptr;
    };

    /**
     * @brief SDL_ShowOpenFileDialog callback (cross-platform fallback only).
     *
     * Does NOT create the chip immediately. Stores the chosen path in canvas->m_pending*
     * and sets m_attachModalOpen = true so the per-frame modal draw can handle it.
     *
     * @param userdata Heap-allocated AttachmentFileDialogContext* (deleted here).
     * @param filelist Null-terminated array of selected paths, or nullptr on cancel.
     * @param filter   Filter index (unused).
     */
    static void SDLCALL OnAttachmentFileSelected(void* userdata, const char* const* filelist, int /*filter*/) {
        auto* ctx = static_cast<AttachmentFileDialogContext*>(userdata);
        if (!ctx) return;

        if (filelist && filelist[0] && filelist[0][0] != '\0') {
            std::string selectedPath = filelist[0];
            std::string filename = std::filesystem::path(selectedPath).filename().string();
            if (filename.empty()) filename = selectedPath;

            if (ctx->canvas) {
                // Store pending state; the ImGui modal in app.hpp will call CommitAttachment().
                ctx->canvas->m_pendingAttachPath    = selectedPath;
                ctx->canvas->m_pendingAttachName    = filename;
                ctx->canvas->m_pendingAttachSession = ctx->session;
                ctx->canvas->m_attachModalOpen      = true;
                LOG_INFO(CanvasEngine, "Attachment file selected (async): '" + filename + "' — awaiting embed/link decision.");
            }
        }

        delete ctx;
    }

    /**
     * @brief Opens a native OS file picker to select a file for attachment.
     *
     * Phase 1 of the attachment flow. After the user picks a file, the path is stored
     * in m_pending* and m_attachModalOpen is set to true. The modal in app.hpp then
     * renders each frame and calls CommitAttachment() once the user decides.
     *
     * No AttachmentObject is created here — that happens in CommitAttachment().
     *
     * @param parentWin SDL3 window (used to obtain the parent HWND on Windows).
     * @param session   Active DocumentSession (stored for CommitAttachment to use).
     */
    void OpenAttachmentFileDialog(SDL_Window* parentWin, DocumentSession* session) {
        if (!session) return;

#if defined(_WIN32)
        // ----------------------------------------------------------------
        // WIN32 PATH: GetOpenFileNameW — synchronous, main-thread, reliable.
        // ----------------------------------------------------------------
        SDL_Window* win = parentWin ? parentWin : sdlWindow;
        HWND hwnd = nullptr;
        if (win) {
            SDL_PropertiesID props = SDL_GetWindowProperties(win);
            hwnd = static_cast<HWND>(
                SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        }

        // Filter string: pairs of "Description\0*.ext\0" terminated with "\0\0".
        const wchar_t kFilter[] =
            L"All Files (*.*)\0*.*\0"
            L"Documents (*.pdf;*.docx;*.xlsx;*.pptx;*.txt;*.md)\0*.pdf;*.docx;*.xlsx;*.pptx;*.txt;*.md\0"
            L"Images (*.png;*.jpg;*.jpeg;*.webp)\0*.png;*.jpg;*.jpeg;*.webp\0"
            L"\0";

        wchar_t fileBuf[MAX_PATH] = {};

        OPENFILENAMEW ofn   = {};
        ofn.lStructSize     = sizeof(ofn);
        ofn.hwndOwner       = hwnd;
        ofn.lpstrFilter     = kFilter;
        ofn.nFilterIndex    = 1;
        ofn.lpstrFile       = fileBuf;
        ofn.nMaxFile        = MAX_PATH;
        ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                              OFN_EXPLORER    | OFN_NOCHANGEDIR;
        ofn.lpstrTitle      = L"Select File to Attach";

        LOG_INFO(CanvasEngine, "Opening Win32 attachment file dialog (GetOpenFileNameW)...");

        if (::GetOpenFileNameW(&ofn)) {
            // Convert the chosen wide path to UTF-8.
            int utf8Len = WideCharToMultiByte(
                CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);

            std::string selectedPath;
            if (utf8Len > 0) {
                selectedPath.resize(static_cast<size_t>(utf8Len) - 1);
                WideCharToMultiByte(
                    CP_UTF8, 0, fileBuf, -1,
                    &selectedPath[0], utf8Len, nullptr, nullptr);
            }

            if (!selectedPath.empty()) {
                std::string filename =
                    std::filesystem::path(selectedPath).filename().string();
                if (filename.empty()) filename = selectedPath;

                // Store pending state; the ImGui modal in app.hpp calls CommitAttachment().
                m_pendingAttachPath    = selectedPath;
                m_pendingAttachName    = filename;
                m_pendingAttachSession = session;
                m_attachModalOpen      = true;
                LOG_INFO(CanvasEngine, "Attachment file selected: '" + filename + "' — awaiting embed/link decision.");
            }
        } else {
            // User cancelled or dialog error — CommDlgExtendedError() gives details.
            DWORD err = CommDlgExtendedError();
            if (err != 0) {
                LOG_INFO(CanvasEngine,
                    "GetOpenFileNameW failed, CommDlgExtendedError=" +
                    std::to_string(static_cast<unsigned long>(err)));
            }
        }

#else
        // ----------------------------------------------------------------
        // CROSS-PLATFORM FALLBACK: SDL_ShowOpenFileDialog (async callback).
        // ----------------------------------------------------------------
        auto* ctx = new AttachmentFileDialogContext{ this, session };
        LOG_INFO(CanvasEngine, "Opening SDL attachment file dialog...");
        SDL_ShowOpenFileDialog(
            OnAttachmentFileSelected, ctx,
            parentWin ? parentWin : sdlWindow,
            nullptr, 0, nullptr, false);
#endif
    }

    /**
     * @brief Phase 2 of the attachment flow: creates and places the AttachmentObject chip.
     *
     * Called by app.hpp after the user clicks "Embed" or "Link" in the modal dialog.
     *
     * Working Process:
     *   1. If embed == true:
     *        a. Resolves the notebook sidecar folder: <notebook_dir>/attachments/.
     *        b. Generates a UUID prefix to avoid collisions: <uuid>_<filename>.
     *        c. Copies the source file using std::filesystem::copy_file.
     *        d. filePath on the chip stores the sidecar-relative path.
     *   2. Creates an AttachmentObject with isEmbedded set accordingly.
     *   3. Places it at canvas centre, assigns UID/GUID, records undo command.
     *   4. Clears all m_pending* state and closes the modal.
     *
     * @param embed   true = copy file into sidecar; false = store absolute path link.
     * @param session Active DocumentSession (may differ from m_pendingAttachSession if needed).
     */
    void CommitAttachment(bool embed, DocumentSession* session) {
        // Require both a chosen file and a valid session
        if (m_pendingAttachPath.empty() || !session) {
            m_attachModalOpen = false;
            return;
        }

        std::string finalPath = m_pendingAttachPath;
        bool embeddedOk = false;

        if (embed) {
            // Resolve sidecar attachments folder: <notebook_directory>/attachments/
            auto activeNb = session->GetActiveNotebook();
            std::string notebookDir = (activeNb) ? activeNb->filePath : "";
            if (!notebookDir.empty()) {
                namespace fs = std::filesystem;
                fs::path attachDir = fs::path(notebookDir) / "attachments";

                std::error_code ec;
                fs::create_directories(attachDir, ec); // No-op if already exists

                if (!ec) {
                    // Prefix with a UUID to avoid filename collisions inside the sidecar.
                    std::string safeFilename = GUIDGenerator::GenerateV4().substr(0, 8)
                                             + "_" + m_pendingAttachName;
                    fs::path destPath = attachDir / safeFilename;

                    fs::copy_file(fs::path(m_pendingAttachPath), destPath,
                                  fs::copy_options::overwrite_existing, ec);

                    if (!ec) {
                        // Store as sidecar-relative path so the notebook stays portable.
                        finalPath  = "attachments/" + safeFilename;
                        embeddedOk = true;
                        LOG_INFO(CanvasEngine, "Embedded attachment: copied '" + m_pendingAttachName +
                                              "' to sidecar as '" + safeFilename + "'");
                    } else {
                        // Copy failed — fall back to link mode with a warning.
                        LOG_ERROR_CODE(CanvasEngine, Folio::FolioErrorCode::SysFileWriteFailed,
                            "Failed to copy attachment to sidecar: " + ec.message() +
                            " — falling back to link mode.");
                        finalPath  = m_pendingAttachPath;
                        embeddedOk = false;
                    }
                } else {
                    LOG_ERROR_CODE(CanvasEngine, Folio::FolioErrorCode::SysDirectoryCreateFailed,
                        "Failed to create attachments sidecar directory: " + ec.message() +
                        " — falling back to link mode.");
                }
            } else {
                // Notebook not yet saved to disk — cannot embed, fall back to link.
                LOG_WARN(CanvasEngine,
                    "Cannot embed attachment: notebook has no directory yet. Using link mode.");
            }
        }

        // Compute canvas-centre world coordinates for chip placement.
        Point2D centerWorld = transform.ScreenToWorld(
            static_cast<float>(viewportW) * 0.5f,
            static_cast<float>(viewportH) * 0.5f);

        // Create the chip and place it at canvas centre.
        auto attachObj = std::make_shared<Folio::AttachmentObject>(
            finalPath, m_pendingAttachName, "", (embed && embeddedOk));
        attachObj->worldX  = centerWorld.x - Folio::AttachmentObject::chipW * 0.5;
        attachObj->worldY  = centerWorld.y - Folio::AttachmentObject::chipH * 0.5;
        attachObj->guuid   = GUIDGenerator::GenerateV4();
        attachObj->uid     = UIDGenerator::Next();
        attachObj->UpdateBounds();

        auto activePage = session->GetActivePage();
        if (activePage) {
            activePage->AddObject(attachObj, true);
            session->RecordHistoryCommand(
                activePage,
                std::make_unique<Folio::AddObjectCommand>(attachObj));
            activePage->isModified = true;
            session->NotifyPageModified(activePage);
        }

        needsFullRebake = true;
        isDirty         = true;

        LOG_INFO(CanvasEngine, "Committed attachment '" + m_pendingAttachName +
                               "' mode=" + std::string(embed && embeddedOk ? "embedded" : "link") +
                               " path='" + finalPath + "'");

        // Clear pending state and close modal
        m_pendingAttachPath.clear();
        m_pendingAttachName.clear();
        m_pendingAttachSession = nullptr;
        m_attachModalOpen      = false;
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
                        pdfObj->resolvedDiskPath = docInfo.diskPath;
                        pdfObj->isExternalLink = docInfo.isExternal;
                        pdfObj->guuid = GUIDGenerator::GenerateV4();
                        pdfObj->uid = UIDGenerator::Next();
                        pdfObj->EnsurePageLoaded();
                        pdfObj->UpdateBounds();

                        activePage->AddObject(pdfObj, true);
                        if (ctx->session) {
                            ctx->session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(pdfObj));
                        }
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

            // Eraser Target Filter:
            // An eraser strictly targets freehand ink strokes and hand-drawn geometric shapes.
            // Under no circumstances should an eraser drag delete PDFs, background images, text boxes,
            // tables, media, or file attachment chips (which are removed via keyboard Delete or the Gizmo).
            // We also guard against modifying locked objects.
            if (obj->isLocked) {
                continue;
            }
            if (obj->type != ObjectType::InkContainer && obj->type != ObjectType::Shape) {
                continue;
            }

            if (isStrokeEraser) {
                bool hit = false;
                if (obj->type == ObjectType::InkContainer) {
                    hit = std::static_pointer_cast<InkContainer>(obj)->HitTestSwept(w0, w1, r);
                } else if (obj->type == ObjectType::Shape) {
                    hit = std::static_pointer_cast<Folio::ShapeObject>(obj)->HitTestSwept(w0, w1, r);
                }
                if (hit) {
                    if (devMode) debugCollision.hitUids.push_back(obj->uid);
                    session.RecordErasedObject(obj);
                    activePage->RemoveObject(obj);
                    modified = true;
                }
            } else {
                // Precision / Point eraser: slices vector ink strokes along continuous swept path
                if (obj->type == ObjectType::InkContainer) {
                    auto ink = std::static_pointer_cast<InkContainer>(obj);
                    double segLen = std::hypot(w1.x - w0.x, w1.y - w0.y);
                    int steps = std::clamp(static_cast<int>(std::ceil(segLen / (r * 0.6))), 1, 30);

                    // Clone pristine original stroke state before any slicing takes place
                    auto originalClone = std::shared_ptr<InkContainer>(static_cast<InkContainer*>(ink->Clone().release()));

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
                        std::vector<std::shared_ptr<InkContainer>> survivingFragments;
                        if (ink->strokes.empty()) {
                            activePage->RemoveObject(ink);
                        } else {
                            activePage->UpdateObject(ink);
                            survivingFragments.push_back(ink);
                        }
                        for (auto& frag : newFragments) {
                            frag->uid = UIDGenerator::Next();
                            activePage->AddObject(frag);
                            survivingFragments.push_back(frag);
                        }
                        session.RecordSlicedStroke(originalClone, survivingFragments);
                        modified = true;
                    }
                } else {
                    // Non-stroke erasable objects (e.g. hand-drawn shapes): delete on hit
                    bool hit = false;
                    if (obj->type == ObjectType::Shape) {
                        hit = std::static_pointer_cast<Folio::ShapeObject>(obj)->HitTestSwept(w0, w1, r);
                    }
                    if (hit) {
                        if (devMode) debugCollision.hitUids.push_back(obj->uid);
                        session.RecordErasedObject(obj);
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

        Viewport currentView = GetViewport();

        // ---------------------------------------------------------------------
        // Dynamic Selection Frustum Inclusion:
        // When an object is created or moved outside the camera frustum, the R-tree
        // spatial index retains its original off-screen bounding box until mouse release
        // (when BakeTransform commits the geometry and updates the spatial index).
        // If the user selects the object and drags it into the viewport, standard
        // spatialIndex.Query(viewport.bounds) would cull it out because the R-tree has
        // not yet been updated.
        //
        // Process & Working Details:
        // 1. Fast-path check: Verify if all active selectedObjects already exist in visibleBakedObjects.
        // 2. Slow-path fallback: If any selected object is missing, construct mergedObjects
        //    combining visibleBakedObjects with the missing selected items and stable-sort by zOrder.
        // 3. Avoids per-frame heap allocations when all selected items are already visible.
        // ---------------------------------------------------------------------
        const std::vector<std::shared_ptr<CanvasObject>>* finalRenderList = &visibleBakedObjects;
        std::vector<std::shared_ptr<CanvasObject>> mergedObjects;

        if (selectionGizmo.HasSelection()) {
            bool hasMissingSelected = false;
            for (const auto& selObj : selectionGizmo.selectedObjects) {
                if (!selObj || !selObj->isVisible) continue;
                bool found = false;
                for (const auto& bakedObj : visibleBakedObjects) {
                    if (bakedObj && bakedObj->uid == selObj->uid) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    hasMissingSelected = true;
                    break;
                }
            }

            if (hasMissingSelected) {
                mergedObjects = visibleBakedObjects;
                for (const auto& selObj : selectionGizmo.selectedObjects) {
                    if (!selObj || !selObj->isVisible) continue;
                    bool found = false;
                    for (const auto& bakedObj : visibleBakedObjects) {
                        if (bakedObj && bakedObj->uid == selObj->uid) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        mergedObjects.push_back(selObj);
                    }
                }
                std::stable_sort(mergedObjects.begin(), mergedObjects.end(), [](const auto& a, const auto& b) {
                    return a->zOrder < b->zOrder;
                });
                finalRenderList = &mergedObjects;
            }
        }

        // Track content bounds for automatic page border
        double maxX = 0.0, maxY = 0.0;
        for (const auto& obj : *finalRenderList) {
            if (!obj) continue;
            const AABB& b = obj->bounds;
            if (b.minX <= b.maxX && b.minY <= b.maxY) {
                maxX = std::max(maxX, b.maxX);
                maxY = std::max(maxY, b.maxY);
            }
        }
        contentMaxXMm = maxX;
        contentMaxYMm = maxY;

        BLMatrix2D renderMatrix = transform.GetBlend2DTransformMatrix();

        // 1. Static Baked Layer (Background grid + all visible objects)
        if (needsFullRebake) {
            BLContext staticCtx(staticCanvasLayer);
            staticCtx.clear_all();

            DrawTiledBackground(staticCtx, currentView);

            staticCtx.save();
            staticCtx.set_transform(renderMatrix);
            for (const auto& obj : *finalRenderList) {
                if (textEditor.IsActive() && obj.get() == textEditor.GetTarget()) {
                    continue; // Rendered live in real-time composite pass with caret and selection
                }
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
            for (const auto& obj : *finalRenderList) {
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
        if (shapeCreation.isDragging || (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1)) {
            double minX = 0.0, minY = 0.0, w = 0.5, h = 0.5;

            if (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1) {
                double rx = shapeCreation.ellipseMajorRadius;
                double ry = shapeCreation.ellipseMinorRadius;
                double cx = shapeCreation.ellipseCenter.x;
                double cy = shapeCreation.ellipseCenter.y;
                minX = cx - rx;
                minY = cy - ry;
                w = std::max(0.5, rx * 2.0);
                h = std::max(0.5, ry * 2.0);

                // Guide markers for center and major axis
                compCtx.fill_circle(cx, cy, 0.8, shapeCreation.defaultOutlineColor);
                compCtx.fill_circle(shapeCreation.ellipseMajorPoint.x, shapeCreation.ellipseMajorPoint.y, 0.8, shapeCreation.defaultOutlineColor);
            }
            else if (shapeCreation.shapeType == Folio::ShapeType::Circle) {
                double r = std::hypot(shapeCreation.currentWorld.x - shapeCreation.startWorld.x,
                                      shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
                w = std::max(0.5, r * 2.0);
                h = std::max(0.5, r * 2.0);
                minX = shapeCreation.startWorld.x - r;
                minY = shapeCreation.startWorld.y - r;

                // Center dot indicator
                compCtx.fill_circle(shapeCreation.startWorld.x, shapeCreation.startWorld.y, 0.8, shapeCreation.defaultOutlineColor);
            }
            else if (shapeCreation.shapeType == Folio::ShapeType::Hexagon ||
                     shapeCreation.shapeType == Folio::ShapeType::RegularPolygon) {
                double r = std::hypot(shapeCreation.currentWorld.x - shapeCreation.startWorld.x,
                                      shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
                w = std::max(0.5, r * 2.0);
                h = std::max(0.5, r * 2.0);
                minX = shapeCreation.startWorld.x - r;
                minY = shapeCreation.startWorld.y - r;

                compCtx.fill_circle(shapeCreation.startWorld.x, shapeCreation.startWorld.y, 0.8, shapeCreation.defaultOutlineColor);
            }
            else if (shapeCreation.shapeType == Folio::ShapeType::Line ||
                     shapeCreation.shapeType == Folio::ShapeType::LineArrow) {
                // Handled in dedicated SmartArrowObject preview branch below
            }
            else if (shapeCreation.shapeType == Folio::ShapeType::SineWave ||
                     shapeCreation.shapeType == Folio::ShapeType::SquareWave ||
                     shapeCreation.shapeType == Folio::ShapeType::TriangleWave ||
                     shapeCreation.shapeType == Folio::ShapeType::RightTriangleWave) {
                minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
                w = std::max(0.5, std::abs(shapeCreation.currentWorld.x - shapeCreation.startWorld.x));
                double rawH = std::abs(shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
                if (rawH < 4.0) {
                    h = 20.0;
                    minY = shapeCreation.startWorld.y - 10.0;
                } else {
                    minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
                    h = std::max(0.5, rawH);
                }
            }
            else {
                minX = std::min(shapeCreation.startWorld.x, shapeCreation.currentWorld.x);
                minY = std::min(shapeCreation.startWorld.y, shapeCreation.currentWorld.y);
                w = std::max(0.5, std::abs(shapeCreation.currentWorld.x - shapeCreation.startWorld.x));
                h = std::max(0.5, std::abs(shapeCreation.currentWorld.y - shapeCreation.startWorld.y));
            }

            if (shapeCreation.shapeType == Folio::ShapeType::Line ||
                shapeCreation.shapeType == Folio::ShapeType::LineArrow) {
                double dragDist = std::hypot(shapeCreation.currentWorld.x - shapeCreation.startWorld.x,
                                             shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
                if (dragDist >= 1.0) {
                    Folio::SmartArrowObject preview(
                        shapeCreation.startWorld.x, shapeCreation.startWorld.y,
                        shapeCreation.currentWorld.x, shapeCreation.currentWorld.y
                    );
                    preview.strokeColor = shapeCreation.defaultOutlineColor;
                    preview.strokeWidth = shapeCreation.defaultStrokeWidth;
                    preview.outlineType = shapeCreation.defaultOutlineType;
                    preview.connectorStyle = shapeCreation.defaultConnectorStyle;
                    preview.startArrow = Folio::ArrowHeadType::None;
                    preview.endArrow = (shapeCreation.shapeType == Folio::ShapeType::LineArrow)
                                      ? shapeCreation.defaultEndArrow
                                      : Folio::ArrowHeadType::None;
                    preview.arrowHeadSize = 4.0;
                    Viewport vp;
                    vp.zoom = transform.zoom;
                    preview.Render(compCtx, vp);
                }
            } else {
                double dragDist = std::hypot(shapeCreation.currentWorld.x - shapeCreation.startWorld.x,
                                             shapeCreation.currentWorld.y - shapeCreation.startWorld.y);
                if (dragDist >= 1.0 || (shapeCreation.shapeType == Folio::ShapeType::Ellipse && shapeCreation.ellipseStep == 1)) {
                    Folio::ShapeObject preview(shapeCreation.shapeType, minX, minY, w, h);
                    preview.fillType = shapeCreation.defaultFillType;
                    preview.fillColor = shapeCreation.defaultFillColor;
                    preview.outlineType = shapeCreation.defaultOutlineType;
                    preview.strokeColor = shapeCreation.defaultOutlineColor;
                    preview.strokeWidth = shapeCreation.defaultStrokeWidth;
                    if (shapeCreation.shapeType == Folio::ShapeType::Hexagon ||
                        shapeCreation.shapeType == Folio::ShapeType::RegularPolygon) {
                        preview.param1 = static_cast<double>(shapeCreation.polygonSides);
                    } else if (shapeCreation.shapeType == Folio::ShapeType::SineWave ||
                               shapeCreation.shapeType == Folio::ShapeType::SquareWave ||
                               shapeCreation.shapeType == Folio::ShapeType::TriangleWave ||
                               shapeCreation.shapeType == Folio::ShapeType::RightTriangleWave) {
                        preview.param1 = 3.0;
                        preview.param2 = 0.0;
                    }

                    Viewport vp;
                    vp.zoom = transform.zoom;
                    preview.Render(compCtx, vp);
                }
            }

            // Magnetic snap glow target indicator
            if (shapeCreation.isSnapped) {
                compCtx.save();
                compCtx.set_stroke_style(BLRgba32(0x00, 0xBD, 0xB0, 0xE0));
                compCtx.set_stroke_width(0.8);
                compCtx.stroke_circle(shapeCreation.snapAnchorPoint.x, shapeCreation.snapAnchorPoint.y, 2.8);
                compCtx.set_fill_style(BLRgba32(0x00, 0xE5, 0xFF, 0xFF));
                compCtx.fill_circle(shapeCreation.snapAnchorPoint.x, shapeCreation.snapAnchorPoint.y, 1.2);
                compCtx.restore();
            }
        }

        // Render ephemeral presentation ink strokes (e.g. Laser Pointer) in World Coordinates
        // Mathematical model: Quadratic decay α(t) = α_0 * (1 - t/T)^2
        if (!ephemeralStrokes.empty()) {
            auto nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());

            // Remove expired strokes and render surviving strokes with continuous alpha attenuation
            ephemeralStrokes.erase(
                std::remove_if(ephemeralStrokes.begin(), ephemeralStrokes.end(),
                    [&](const EphemeralStroke& stroke) {
                        if (nowMs < stroke.startTimeMs) return false;
                        uint64_t elapsed = nowMs - stroke.startTimeMs;
                        if (elapsed >= stroke.durationMs) return true; // Lifetime expired

                        // Normalized progress: progress ∈ [0.0, 1.0]
                        double progress = static_cast<double>(elapsed) / static_cast<double>(stroke.durationMs);
                        // Quadratic decay factor: (1 - t)^2
                        double decayFactor = (1.0 - progress) * (1.0 - progress);

                        uint32_t baseAlpha = stroke.color.a();
                        uint32_t fadedAlpha = static_cast<uint32_t>(baseAlpha * decayFactor);
                        if (fadedAlpha > 0) {
                            BLRgba32 fadedColor(stroke.color.r(), stroke.color.g(), stroke.color.b(), fadedAlpha);
                            compCtx.set_fill_rule(BL_FILL_RULE_NON_ZERO);
                            compCtx.set_fill_style(fadedColor);
                            compCtx.fill_path(stroke.outlinePath);
                        }
                        return false;
                    }),
                ephemeralStrokes.end()
            );

            // As long as ephemeral strokes are alive and decaying, request continuous frame redraws
            if (!ephemeralStrokes.empty()) {
                isDirty = true;
            }
        }

        // Active Headless Text Editor Pass (World Coordinates)
        if (textEditor.IsActive() && textEditor.GetTarget()) {
            auto nowSec = static_cast<double>(SDL_GetTicks()) / 1000.0;
            textEditor.UpdateBlink(nowSec);
            textEditor.GetTarget()->RenderWithEditor(compCtx, currentView, textEditor);
            isDirty = true; // Continuous refresh for caret blink
        }

        compCtx.restore();

        // 3. Selection Gizmo Overlay Pass (Screen Coordinates) - suppressed during active shape drawing
        if (selectionGizmo.HasSelection() && !shapeCreation.isActive && !shapeCreation.isDragging) {
            selectionGizmo.lockToGrid = shapeCreation.lockToGrid;
            selectionGizmo.gridSpacingMm = gridSpacingMm;
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
