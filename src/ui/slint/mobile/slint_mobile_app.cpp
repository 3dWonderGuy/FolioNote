#include "ui/slint/mobile/slint_mobile_app.hpp"
#include "app/window_state_manager.hpp"
#include "core/clipboard/clipboard_manager.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/objects/primitives/shape_container.hpp"
#include "core/history/canvas_command.hpp"
#include "io/paths/app_directories.hpp"
#include "io/paths/path_utils.hpp"
#include "io/storage/file_logger.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>

#if defined(FOLIONOTE_HAS_SLINT)
#include <slint.h>
#include <slint-platform.h>
#include "mobile_shell.h"
#endif

namespace Folio {

#if defined(FOLIONOTE_HAS_SLINT)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((optimize("O3")))
#endif
static void BlitBlend2DSubRectToSlint(const uint8_t* __restrict srcBytes,
                                      size_t srcStride,
                                      slint::Rgba8Pixel* __restrict dst,
                                      uint32_t dstWidth,
                                      uint32_t rx,
                                      uint32_t ry,
                                      uint32_t rw,
                                      uint32_t rh) noexcept {
    for (uint32_t y = ry; y < ry + rh; ++y) {
        const uint32_t* __restrict srcRow = reinterpret_cast<const uint32_t*>(srcBytes + static_cast<size_t>(y) * srcStride);
        uint32_t* __restrict dstRow = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * dstWidth);
        for (uint32_t x = rx; x < rx + rw; ++x) {
            uint32_t c = srcRow[x];
            // Swizzle little-endian PRGB32 (0xAARRGGBB) -> RGBA8 (0xAABBGGRR)
            uint32_t rb = ((c << 16) & 0x00FF0000u) | ((c >> 16) & 0x000000FFu);
            uint32_t ga = c & 0xFF00FF00u;
            dstRow[x] = ga | rb;
        }
    }
}
#endif

struct SlintMobileApplication::Impl {
#if defined(FOLIONOTE_HAS_SLINT)
    std::optional<slint::ComponentHandle<MobileSlint::FolioMobileShell>> shell;
    slint::SharedPixelBuffer<slint::Rgba8Pixel> pixelBuffer;
    slint::Timer renderTimer;
#endif
};

SlintMobileApplication::SlintMobileApplication()
    : m_impl(std::make_unique<Impl>()) {
}

SlintMobileApplication::~SlintMobileApplication() {
    Shutdown();
}

void SlintMobileApplication::EnsureCanvasViewportMatch() {
#if defined(FOLIONOTE_HAS_SLINT)
    if (!m_impl->shell.has_value()) return;
    auto& s = *m_impl->shell;

    float scaleFactor = s->window().scale_factor();
    if (scaleFactor <= 0.0f) scaleFactor = 1.0f;
    float targetDpi = 96.0f * scaleFactor;
    if (std::abs(m_canvas.transform.GetDPI() - targetDpi) > 0.1f) {
        m_canvas.SetDPI(targetDpi);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    }

    int vpW = s->get_canvas_viewport_w();
    int vpH = s->get_canvas_viewport_h();
    if (vpW > 8 && vpH > 8 && (vpW != m_canvas.viewportW || vpH != m_canvas.viewportH)) {
        const bool wasUninitialized = (m_canvas.viewportW <= 0 || m_canvas.viewportH <= 0);
        m_canvas.Resize(vpW, vpH);
        if (wasUninitialized) {
            m_canvas.HomeOrCenterPage();
        }
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    }
#endif
}

void SlintMobileApplication::SyncAndRenderCanvas(bool forceImmediate) {
#if defined(FOLIONOTE_HAS_SLINT)
    if (!m_impl->shell.has_value()) return;
    auto& s = *m_impl->shell;

    const uint64_t nowNs = SDL_GetTicksNS();
    if (!forceImmediate && m_lastRenderTicksNs != 0 && (nowNs - m_lastRenderTicksNs) < 4000000ULL) {
        // Coalesce sub-4ms pointer moves (~250Hz cap) so rapid events batch cleanly
        return;
    }

    EnsureCanvasViewportMatch();

    const double dt = (m_lastRenderTicksNs > 0)
        ? std::clamp(static_cast<double>(nowNs - m_lastRenderTicksNs) * 1e-9, 0.001, 0.05)
        : 0.008;
    m_lastRenderTicksNs = nowNs;

    m_canvas.Update(dt, &m_session);

    if (!m_canvas.isDirty &&
        !m_canvas.needsFullRebake &&
        !m_canvas.layerCompositor.GetBakedCanvasLayer().IsDirty() &&
        !m_canvas.layerCompositor.GetLiveInteractionLayer().HasActiveInteraction()) {
        return;
    }

    std::vector<std::shared_ptr<CanvasObject>> visibleObjects = m_session.QueryVisible(m_canvas.GetViewport());
    m_canvas.Render(visibleObjects, &m_session, dt);

    BLImageData imgData;
    if (m_canvas.compositeSurface.get_data(&imgData) == BL_SUCCESS && imgData.pixel_data) {
        const uint32_t w = static_cast<uint32_t>(m_canvas.viewportW);
        const uint32_t h = static_cast<uint32_t>(m_canvas.viewportH);
        if (w == 0 || h == 0) return;

        bool bufferResized = false;
        if (m_impl->pixelBuffer.width() != w || m_impl->pixelBuffer.height() != h) {
            m_impl->pixelBuffer = slint::SharedPixelBuffer<slint::Rgba8Pixel>(w, h);
            bufferResized = true;
        } else {
            // Drop Slint's secondary reference BEFORE calling .begin() so refcount == 1.
            // This avoids SharedVector::detach() allocating & copying 1-2 million pixels!
            s->set_canvas_image(slint::Image());
        }

        slint::Rgba8Pixel* dst = m_impl->pixelBuffer.begin();
        const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(imgData.pixel_data);
        const size_t stride = static_cast<size_t>(imgData.stride);

        uint32_t rx = 0, ry = 0, rw = w, rh = h;
        if (!bufferResized && m_canvas.lastUploadWasPartial && m_canvas.lastDirtyW > 0 && m_canvas.lastDirtyH > 0) {
            rx = static_cast<uint32_t>(std::clamp(m_canvas.lastDirtyX, 0, static_cast<int>(w) - 1));
            ry = static_cast<uint32_t>(std::clamp(m_canvas.lastDirtyY, 0, static_cast<int>(h) - 1));
            rw = std::min(static_cast<uint32_t>(m_canvas.lastDirtyW), w - rx);
            rh = std::min(static_cast<uint32_t>(m_canvas.lastDirtyH), h - ry);
        }

        BlitBlend2DSubRectToSlint(srcBytes, stride, dst, w, rx, ry, rw, rh);
        s->set_canvas_image(slint::Image(m_impl->pixelBuffer));
    }
#endif
}

bool SlintMobileApplication::Init(const std::string& title, int width, int height, const std::string& initialImportPath) {
    LOG_INFO(General, "Booting native Slint Mobile Shell (" + std::to_string(width) + "x" + std::to_string(height) + ")");

    m_width = width;
    m_height = height;

    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
    }

    // Initialize data storage and session
    const char* docsPath = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    std::filesystem::path folioPath = docsPath ? (std::filesystem::path(docsPath) / "FolioNote") : (std::filesystem::path(".") / "FolioNote");
    std::error_code ec;
    std::filesystem::create_directories(folioPath, ec);

    Folio::AppDirectories::SetAppRootDirectory(folioPath.string());
    Folio::FileLogger::Instance().Initialize(Folio::PathUtils::JoinPath(folioPath.string(), "logs"));
    m_session.Init(folioPath.string());

    // Restore last active session (Notebook, Section, Page)
    try {
        SettingsManager::Instance().Load();
    } catch (...) {}

    const auto& sm = SettingsManager::Instance();
    if (!sm.lastActiveNotebookGuid.empty() || !sm.lastActivePageGuid.empty()) {
        try {
            m_session.RestoreLastSession(sm.lastActiveNotebookGuid, sm.lastActiveSectionGuid, sm.lastActivePageGuid);
        } catch (...) {}
    }

    // Fallback: If session restore didn't establish a valid active page, ensure we activate a valid notebook & page
    if (!m_session.GetActivePage()) {
        if (!m_session.workspace.notebooks.empty()) {
            auto firstNb = m_session.workspace.notebooks.front();
            m_session.OpenNotebook(firstNb->guid);
            auto activeSec = firstNb->GetActiveSection();
            if (!activeSec && !firstNb->sections.empty()) {
                firstNb->SetActiveSection(firstNb->sections.front());
                activeSec = firstNb->GetActiveSection();
            }
            if (activeSec && !activeSec->pages.empty()) {
                m_session.NavigateToPage(activeSec->pages.front()->guid);
            }
        }
    }

    // If still no page, create a default first page
    if (!m_session.GetActivePage()) {
        auto newPg = m_session.CreateNewPage("First Page");
        if (newPg) {
            m_session.NavigateToPage(newPg->guid);
        }
    }

    // Initialize inking canvas engine
    m_canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
    m_canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
    m_canvas.Init(width, height, 96.0f, false /* createGlTexture = false */);
    m_canvas.GetInkEngine().SetConfig(SettingsManager::Instance().inkingConfig);
    m_canvas.HomeOrCenterPage();
    m_canvas.needsFullRebake = true;
    m_canvas.isDirty = true;

    // Default pen configuration
    m_activePen.penType = PenType::Pen;
    m_activePen.color = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
    m_activePen.baseSize = 0.5f;
    m_activePen.widthMm = 0.5f;
    m_activePen.opacity = 1.0f;
    m_currentTool = "pen";
    m_eraserRadiusMm = 3.0;

#if defined(FOLIONOTE_HAS_SLINT)
    try {
        m_impl->shell = MobileSlint::FolioMobileShell::create();
        auto& sh = *m_impl->shell;

        // Setup the initial canvas image pixel buffer
        m_impl->pixelBuffer = slint::SharedPixelBuffer<slint::Rgba8Pixel>(width, height);

        // Tool Selection Handlers
        sh->on_select_tool([this](slint::SharedString toolName) {
            std::string t = toolName.data();
            LOG_INFO(General, "Tool selected in Slint: " + t);
            m_currentTool = t;
            m_canvas.HideEraserCursor();
            m_canvas.CancelShapeCreation();

            if (t == "pen") {
                m_activePen.penType = PenType::Pen;
                m_activePen.blendMode = BlendMode::Normal;
                m_activePen.isPressureEnabled = true;
                m_activePen.isFlowSimEnabled = false;
            } else if (t == "fountain") {
                m_activePen.penType = PenType::Fountain;
                m_activePen.blendMode = BlendMode::Normal;
                m_activePen.isPressureEnabled = true;
                m_activePen.isFlowSimEnabled = true;
            } else if (t == "pencil") {
                m_activePen.penType = PenType::Pencil;
                m_activePen.blendMode = BlendMode::Normal;
                m_activePen.isPressureEnabled = true;
                m_activePen.isFlowSimEnabled = false;
            } else if (t == "brush") {
                m_activePen.penType = PenType::Brush;
                m_activePen.blendMode = BlendMode::Normal;
                m_activePen.isPressureEnabled = true;
                m_activePen.isFlowSimEnabled = true;
            } else if (t == "highlighter") {
                m_activePen.penType = PenType::Highlighter;
                m_activePen.blendMode = BlendMode::Multiply;
                m_activePen.opacity = 0.45f;
                if (m_activePen.widthMm < 2.5f) {
                    m_activePen.widthMm = 3.5f;
                    m_activePen.baseSize = 3.5f;
                    if (m_impl->shell.has_value()) {
                        (*m_impl->shell)->set_stroke_width_mm(3.5f);
                        (*m_impl->shell)->set_stroke_width_label("3.5 mm");
                    }
                }
            } else if (t == "laser") {
                m_activePen.penType = PenType::LaserPointer;
                m_activePen.blendMode = BlendMode::Additive;
            } else if (t == "select") {
                m_canvas.selectionMode = CanvasEngine::SelectionMode::Box;
            } else if (t == "lasso") {
                m_canvas.selectionMode = CanvasEngine::SelectionMode::Lasso;
            }
            SyncAndRenderCanvas(true);
        });

        sh->on_select_color([this](slint::Color col) {
            m_activePen.color = BLRgba32(col.red(), col.green(), col.blue(), col.alpha());
            m_canvas.shapeCreation.defaultOutlineColor = BLRgba32(col.red(), col.green(), col.blue(), 0xFF);
        });

        sh->on_set_stroke_width([this](float w) {
            m_activePen.widthMm = w;
            m_activePen.baseSize = w;
            m_canvas.shapeCreation.defaultStrokeWidth = w;
        });

        sh->on_set_opacity_pct([this](int pct) {
            m_activePen.opacity = std::clamp(static_cast<float>(pct) / 100.0f, 0.05f, 1.0f);
        });

        sh->on_set_stroke_pattern([this](slint::SharedString pat) {
            std::string p = pat.data();
            if (p == "dashed") {
                m_activePen.strokePattern = StrokePattern::Dashed;
                m_canvas.shapeCreation.defaultOutlineType = Folio::ShapeOutlineType::Dashed;
            } else if (p == "dotted") {
                m_activePen.strokePattern = StrokePattern::Dotted;
                m_canvas.shapeCreation.defaultOutlineType = Folio::ShapeOutlineType::Dotted;
            } else {
                m_activePen.strokePattern = StrokePattern::Solid;
                m_canvas.shapeCreation.defaultOutlineType = Folio::ShapeOutlineType::Solid;
            }
        });

        sh->on_ribbon_action([this](slint::SharedString action) {
            HandleRibbonAction(action.data());
        });

        // 1:1 Physical Pixel Pointer & Scroll Events
        sh->on_canvas_pointer_down([this](float x, float y, int buttonId) {
            EnsureCanvasViewportMatch();

            // Middle-mouse button (buttonId == 2) or explicit Pan tool -> Panning
            if (buttonId == 2 || m_currentTool == "pan") {
                m_isPanning = true;
                m_lastPanPoint = Point2D{x, y};
                return;
            }

            // If ruler is enabled and user clicks directly on the ruler body (not near edge), allow dragging the ruler
            if (m_canvas.ruler.IsEnabled()) {
                Point2D worldPt = m_canvas.transform.ScreenToWorld(x, y);
                Point2D rc = m_canvas.ruler.GetCenter();
                double dx = worldPt.x - rc.x;
                double dy = worldPt.y - rc.y;
                double ang = -m_canvas.ruler.GetAngleRad();
                double lx = dx * std::cos(ang) - dy * std::sin(ang);
                double ly = dx * std::sin(ang) + dy * std::cos(ang);
                if (std::abs(lx) <= m_canvas.ruler.GetLengthMm() * 0.5 &&
                    std::abs(ly) <= m_canvas.ruler.GetWidthMm() * 0.35) {
                    m_isDraggingRuler = true;
                    m_rulerDragOffsetWorld = Point2D{rc.x - worldPt.x, rc.y - worldPt.y};
                    return;
                }
            }

            if (m_currentTool == "eraser" || m_currentTool == "point_eraser") {
                const bool strokeMode = (m_currentTool == "eraser");
                m_isErasing = true;
                m_lastEraserPoint = Point2D{x, y};
                m_session.BeginEraseTransaction();
                m_canvas.EraseAt(x, y, m_eraserRadiusMm, m_session, strokeMode);
                m_canvas.SetEraserCursor(x, y, m_eraserRadiusMm, true, strokeMode);
                m_canvas.isDirty = true;
            } else if (m_currentTool == "select" || m_currentTool == "lasso") {
                if (m_canvas.selectionGizmo.OnPointerDown(x, y, m_canvas.transform,
                                                          m_canvas.shapeCreation.lockToGrid, m_canvas.gridSpacingMm)) {
                    m_canvas.isDirty = true;
                } else {
                    Point2D worldMm = m_canvas.transform.ScreenToWorld(x, y);
                    auto activePage = m_session.GetActivePage();
                    std::shared_ptr<CanvasObject> clickedObj = activePage
                        ? activePage->HitTestSingleClick(worldMm.x, worldMm.y, 2.5)
                        : nullptr;
                    if (clickedObj) {
                        m_canvas.ClearSelection(&m_session);
                        clickedObj->isSelected = 1;
                        m_canvas.selectionGizmo.SetSelectedObjects(activePage->objects);
                        m_canvas.selectionGizmo.OnPointerDown(x, y, m_canvas.transform,
                                                              m_canvas.shapeCreation.lockToGrid, m_canvas.gridSpacingMm);
                        m_canvas.needsFullRebake = true;
                        m_canvas.isDirty = true;
                    } else {
                        m_canvas.ClearSelection(&m_session);
                        if (m_currentTool == "lasso") {
                            m_canvas.OnLassoDown(x, y);
                        } else {
                            m_canvas.OnBoxSelectDown(x, y);
                        }
                    }
                }
            } else if (m_currentTool == "shape") {
                m_canvas.OnShapeDrawDown(x, y, &m_session);
                m_canvas.isDirty = true;
            } else if (m_currentTool == "text") {
                Point2D worldMm = m_canvas.transform.ScreenToWorld(x, y);
                auto box = m_canvas.InsertTextBox(&m_session, worldMm.x, worldMm.y);
                m_canvas.needsFullRebake = true;
                m_canvas.isDirty = true;
            } else {
                double timeSec = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
                m_canvas.OnPointerDown(x, y, 0.8f, timeSec, m_activePen);
            }

            SyncAndRenderCanvas(true);
        });

        sh->on_canvas_pointer_move([this](float x, float y, int buttonId) {
            if (m_isPanning) {
                double dx = static_cast<double>(x) - m_lastPanPoint.x;
                double dy = static_cast<double>(y) - m_lastPanPoint.y;
                m_lastPanPoint = Point2D{x, y};
                m_canvas.Pan(dx, dy);
                SyncAndRenderCanvas(false);
                return;
            }

            if (m_isDraggingRuler) {
                Point2D worldPt = m_canvas.transform.ScreenToWorld(x, y);
                m_canvas.ruler.SetCenter(worldPt.x + m_rulerDragOffsetWorld.x,
                                         worldPt.y + m_rulerDragOffsetWorld.y);
                m_canvas.isDirty = true;
                SyncAndRenderCanvas(false);
                return;
            }

            if ((m_currentTool == "eraser" || m_currentTool == "point_eraser") && m_isErasing) {
                const bool strokeMode = (m_currentTool == "eraser");
                m_canvas.EraseSegment(static_cast<float>(m_lastEraserPoint.x),
                                      static_cast<float>(m_lastEraserPoint.y),
                                      x, y, m_eraserRadiusMm, m_session, strokeMode);
                m_lastEraserPoint = Point2D{x, y};
                m_canvas.SetEraserCursor(x, y, m_eraserRadiusMm, true, strokeMode);
                m_canvas.isDirty = true;
            } else if (m_currentTool == "select" || m_currentTool == "lasso") {
                if (m_canvas.selectionGizmo.isDragging) {
                    if (m_canvas.selectionGizmo.OnPointerMove(x, y, m_canvas.transform,
                                                              m_canvas.shapeCreation.lockToGrid, m_canvas.gridSpacingMm)) {
                        m_canvas.InvalidateLayer();
                    }
                } else if (m_currentTool == "lasso") {
                    m_canvas.OnLassoMove(x, y);
                } else if (m_canvas.marqueeBox.isActive) {
                    m_canvas.OnBoxSelectMove(x, y);
                }
            } else if (m_currentTool == "shape") {
                if (m_canvas.shapeCreation.isDragging ||
                    (m_canvas.shapeCreation.shapeType == Folio::ShapeType::Ellipse && m_canvas.shapeCreation.ellipseStep == 1)) {
                    m_canvas.OnShapeDrawMove(x, y, &m_session);
                    m_canvas.isDirty = true;
                }
            } else if (m_currentTool != "text" && m_currentTool != "pan") {
                double timeSec = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
                m_canvas.OnPointerMove(x, y, 0.8f, timeSec);
            }

            SyncAndRenderCanvas(false);
        });

        sh->on_canvas_pointer_up([this](float x, float y, int buttonId) {
            if (m_isPanning) {
                m_isPanning = false;
                SyncAndRenderCanvas(true);
                return;
            }
            if (m_isDraggingRuler) {
                m_isDraggingRuler = false;
                m_canvas.isDirty = true;
                SyncAndRenderCanvas(true);
                return;
            }

            if ((m_currentTool == "eraser" || m_currentTool == "point_eraser") && m_isErasing) {
                m_isErasing = false;
                m_session.EndEraseTransaction(&m_canvas);
                m_canvas.HideEraserCursor();
                m_canvas.isDirty = true;
            } else if (m_currentTool == "select" || m_currentTool == "lasso") {
                if (m_canvas.selectionGizmo.isDragging) {
                    m_canvas.selectionGizmo.OnPointerUp(&m_session);
                    m_canvas.SyncSelectionToSpatialIndex(&m_session);
                    m_canvas.InvalidateLayer();
                } else if (m_currentTool == "lasso") {
                    m_canvas.OnLassoUp(&m_session);
                } else if (m_canvas.marqueeBox.isActive) {
                    m_canvas.OnBoxSelectUp(&m_session);
                }
            } else if (m_currentTool == "shape") {
                if (m_canvas.shapeCreation.isDragging ||
                    (m_canvas.shapeCreation.shapeType == Folio::ShapeType::Ellipse && m_canvas.shapeCreation.ellipseStep == 1)) {
                    m_canvas.OnShapeDrawUp(&m_session);
                    m_canvas.needsFullRebake = true;
                    m_canvas.isDirty = true;
                }
            } else if (m_currentTool != "text" && m_currentTool != "pan") {
                m_canvas.OnPointerUp(m_session, m_activePen);
            }

            SyncAndRenderCanvas(true);
        });

        sh->on_canvas_scroll([this](float x, float y, float dx, float dy) {
            EnsureCanvasViewportMatch();
            if (std::abs(dy) > 0.01f) {
                double factor = (dy > 0.0f) ? 1.12 : (1.0 / 1.12);
                m_canvas.ZoomAt(x, y, factor);
                if (m_impl->shell.has_value()) {
                    char zBuf[32];
                    std::snprintf(zBuf, sizeof(zBuf), "%.0f%%", m_canvas.transform.zoom * 100.0);
                    (*m_impl->shell)->set_zoom_label(zBuf);
                }
                SyncAndRenderCanvas(true);
            }
        });

        sh->on_trigger_undo([this]() {
            m_session.Undo(&m_canvas);
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
            SyncAndRenderCanvas(true);
        });

        sh->on_trigger_redo([this]() {
            m_session.Redo(&m_canvas);
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
            SyncAndRenderCanvas(true);
        });

        sh->on_new_page_requested([this]() {
            auto newPg = m_session.CreateNewPage("New Page");
            if (newPg) {
                m_session.NavigateToPage(newPg->guid);
                m_canvas.needsFullRebake = true;
                m_canvas.isDirty = true;
                UpdateSlintNavigationModel();
                SyncAndRenderCanvas(true);
            }
        });

        sh->on_select_page([this](slint::SharedString guid) {
            m_session.NavigateToPage(guid.data());
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
            UpdateSlintNavigationModel();
            SyncAndRenderCanvas(true);
        });

        sh->on_select_section([this](slint::SharedString guid) {
            if (auto nb = m_session.workspace.GetActiveNotebook()) {
                for (auto& sec : nb->sections) {
                    if (sec && sec->guid == guid.data()) {
                        nb->SetActiveSection(sec);
                        if (!sec->pages.empty()) {
                            m_session.NavigateToPage(sec->pages.front()->guid);
                        }
                        m_canvas.needsFullRebake = true;
                        m_canvas.isDirty = true;
                        UpdateSlintNavigationModel();
                        SyncAndRenderCanvas(true);
                        break;
                    }
                }
            }
        });

        sh->on_switch_to_desktop_mode([this]() {
            auto& sm = SettingsManager::Instance();
            sm.uiMode = UIMode::DesktopImGui;
            sm.SaveSettings();
            LOG_INFO(General, "Switching UI mode to DesktopImGui. Exiting application...");
            slint::quit_event_loop();
        });

        // Initialize document navigation sidebar models
        UpdateSlintNavigationModel();

        // 120Hz (8ms) background timer for animations, async dialogs, and resize detection
        m_impl->renderTimer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(8), [this]() {
            SyncAndRenderCanvas(false);
        });

        sh->show();
        SyncAndRenderCanvas(true);
    } catch (const std::exception& e) {
        LOG_ERROR(General, "Failed to instantiate Slint mobile shell: " + std::string(e.what()));
        return false;
    }
#endif

    m_running = true;
    return true;
}

void SlintMobileApplication::HandleRibbonAction(const std::string& action) {
#if defined(FOLIONOTE_HAS_SLINT)
    if (!m_impl->shell.has_value()) return;
    auto& sh = *m_impl->shell;

    // -------------------------------------------------------------------------
    // 1. ERASER & SELECTION ACTIONS
    // -------------------------------------------------------------------------
    if (action == "cycle_eraser_size") {
        if (m_eraserRadiusMm < 2.0) {
            m_eraserRadiusMm = 3.0;
            sh->set_eraser_size_label("6 mm");
        } else if (m_eraserRadiusMm < 4.5) {
            m_eraserRadiusMm = 6.0;
            sh->set_eraser_size_label("12 mm");
        } else {
            m_eraserRadiusMm = 1.0;
            sh->set_eraser_size_label("2 mm");
        }
    } else if (action == "delete_selected") {
        m_canvas.DeleteSelectedObjects(&m_session);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "select_all") {
        m_canvas.SelectAll(&m_session);
        m_currentTool = "select";
        sh->set_active_tool("select");
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    }
    // -------------------------------------------------------------------------
    // 2. CLIPBOARD ACTIONS
    // -------------------------------------------------------------------------
    else if (action == "clipboard_copy") {
        if (auto pg = m_session.GetActivePage()) {
            std::vector<std::shared_ptr<CanvasObject>> sel;
            for (const auto& o : pg->objects) {
                if (o && o->isSelected) sel.push_back(o);
            }
            Folio::ClipboardManager::Instance().CopyObjects(sel);
        }
    } else if (action == "clipboard_cut") {
        if (auto pg = m_session.GetActivePage()) {
            std::vector<std::shared_ptr<CanvasObject>> sel;
            for (const auto& o : pg->objects) {
                if (o && o->isSelected) sel.push_back(o);
            }
            if (Folio::ClipboardManager::Instance().CutObjects(sel, m_session)) {
                m_canvas.selectionGizmo.ClearSelection();
                m_canvas.needsFullRebake = true;
                m_canvas.isDirty = true;
            }
        }
    } else if (action == "clipboard_paste") {
        Point2D centerWorld = m_canvas.transform.ScreenToWorld(
            static_cast<float>(m_canvas.viewportW) * 0.5f,
            static_cast<float>(m_canvas.viewportH) * 0.5f);
        Folio::ClipboardManager::Instance().Paste(m_session, m_canvas, centerWorld.x, centerWorld.y);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "selection_duplicate") {
        if (auto pg = m_session.GetActivePage()) {
            std::vector<std::shared_ptr<CanvasObject>> sel;
            for (const auto& o : pg->objects) {
                if (o && o->isSelected) sel.push_back(o);
            }
            if (!sel.empty()) {
                Folio::ClipboardManager::Instance().CopyObjects(sel);
                Point2D centerWorld = m_canvas.transform.ScreenToWorld(
                    static_cast<float>(m_canvas.viewportW) * 0.5f + 20.0f,
                    static_cast<float>(m_canvas.viewportH) * 0.5f + 20.0f);
                Folio::ClipboardManager::Instance().Paste(m_session, m_canvas, centerWorld.x, centerWorld.y);
                m_canvas.needsFullRebake = true;
                m_canvas.isDirty = true;
            }
        }
    }
    // -------------------------------------------------------------------------
    // 3. BASIC TEXT FORMATTING ACTIONS
    // -------------------------------------------------------------------------
    else if (action.rfind("text_", 0) == 0) {
        auto selectedTextBox = m_canvas.GetSelectedTextBox(&m_session);
        if (action == "text_toggle_bold") {
            m_canvas.defaultTextBold = !m_canvas.defaultTextBold;
            if (selectedTextBox) selectedTextBox->isBold = m_canvas.defaultTextBold;
        } else if (action == "text_toggle_italic") {
            m_canvas.defaultTextItalic = !m_canvas.defaultTextItalic;
            if (selectedTextBox) selectedTextBox->isItalic = m_canvas.defaultTextItalic;
        } else if (action == "text_toggle_underline") {
            m_canvas.defaultTextUnderline = !m_canvas.defaultTextUnderline;
            if (selectedTextBox) selectedTextBox->isUnderline = m_canvas.defaultTextUnderline;
        } else if (action == "text_toggle_strike") {
            m_canvas.defaultTextStrikethrough = !m_canvas.defaultTextStrikethrough;
            if (selectedTextBox) selectedTextBox->isStrikethrough = m_canvas.defaultTextStrikethrough;
        } else if (action == "text_size_up") {
            m_canvas.defaultTextFontSize = std::min(72.0f, m_canvas.defaultTextFontSize + 2.0f);
            if (selectedTextBox) selectedTextBox->fontSize = m_canvas.defaultTextFontSize;
        } else if (action == "text_size_down") {
            m_canvas.defaultTextFontSize = std::max(8.0f, m_canvas.defaultTextFontSize - 2.0f);
            if (selectedTextBox) selectedTextBox->fontSize = m_canvas.defaultTextFontSize;
        } else if (action == "text_align_left") {
            m_canvas.defaultTextAlignment = 0;
            if (selectedTextBox) selectedTextBox->alignment = 0;
        } else if (action == "text_align_center") {
            m_canvas.defaultTextAlignment = 1;
            if (selectedTextBox) selectedTextBox->alignment = 1;
        } else if (action == "text_align_right") {
            m_canvas.defaultTextAlignment = 2;
            if (selectedTextBox) selectedTextBox->alignment = 2;
        } else if (action == "text_clear_format") {
            m_canvas.defaultTextBold = false;
            m_canvas.defaultTextItalic = false;
            m_canvas.defaultTextUnderline = false;
            m_canvas.defaultTextStrikethrough = false;
            m_canvas.defaultTextFontSize = 14.0f;
            sh->set_text_bold(false);
            sh->set_text_italic(false);
            sh->set_text_underline(false);
            sh->set_text_strike(false);
            if (selectedTextBox) {
                selectedTextBox->isBold = false;
                selectedTextBox->isItalic = false;
                selectedTextBox->isUnderline = false;
                selectedTextBox->isStrikethrough = false;
                selectedTextBox->fontSize = 14.0f;
            }
        }
        if (selectedTextBox) {
            selectedTextBox->SyncTextToRuns();
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
        }
    }
    // -------------------------------------------------------------------------
    // 4. PAGE NAVIGATION & CREATION
    // -------------------------------------------------------------------------
    else if (action == "page_duplicate") {
        auto dup = m_session.DuplicateActivePage();
        if (dup) {
            m_session.NavigateToPage(dup->guid);
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
            UpdateSlintNavigationModel();
        }
    } else if (action == "page_prev") {
        if (m_session.PreviousPage()) {
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
            UpdateSlintNavigationModel();
        }
    } else if (action == "page_next") {
        if (m_session.NextPage()) {
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
            UpdateSlintNavigationModel();
        }
    }
    // -------------------------------------------------------------------------
    // 5. INSERT FILES, MEDIA, STICKY NOTES & SHAPES
    // -------------------------------------------------------------------------
    else if (action == "insert_pdf") {
        m_canvas.OpenPdfFileDialog(nullptr, &m_session);
    } else if (action == "insert_attachment") {
        m_canvas.OpenAttachmentFileDialog(nullptr, &m_session);
    } else if (action == "insert_image") {
        m_canvas.OpenImageFileDialog(nullptr, &m_session);
    } else if (action == "insert_audio") {
        m_canvas.OpenAudioFileDialog(nullptr, &m_session);
    } else if (action == "insert_video") {
        m_canvas.OpenVideoFileDialog(nullptr, &m_session);
    } else if (action == "insert_textbox") {
        m_canvas.InsertTextBox(&m_session);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "insert_sticky_yellow") {
        if (auto box = m_canvas.InsertTextBox(&m_session)) {
            box->ApplyStickyPreset(Folio::StickyPreset::PaleYellow);
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
        }
    } else if (action == "insert_sticky_blue") {
        if (auto box = m_canvas.InsertTextBox(&m_session)) {
            box->ApplyStickyPreset(Folio::StickyPreset::SoftBlue);
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
        }
    } else if (action == "insert_sticky_green") {
        if (auto box = m_canvas.InsertTextBox(&m_session)) {
            box->ApplyStickyPreset(Folio::StickyPreset::SoftGreen);
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
        }
    } else if (action == "insert_sticky_pink") {
        if (auto box = m_canvas.InsertTextBox(&m_session)) {
            box->ApplyStickyPreset(Folio::StickyPreset::SoftPink);
            m_canvas.needsFullRebake = true;
            m_canvas.isDirty = true;
        }
    } else if (action == "insert_shape_rect") {
        m_canvas.InsertShape(Folio::ShapeType::Rectangle, &m_session);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "insert_shape_circle") {
        m_canvas.InsertShape(Folio::ShapeType::Circle, &m_session);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "insert_shape_triangle") {
        m_canvas.InsertShape(Folio::ShapeType::Triangle, &m_session);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "insert_shape_star") {
        m_canvas.InsertShape(Folio::ShapeType::Star, &m_session);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    }
    // -------------------------------------------------------------------------
    // 6. INTERACTIVE SHAPE DRAWING MODES & RULER
    // -------------------------------------------------------------------------
    else if (action == "shape_line") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::Line, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_arrow") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::LineArrow, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_rect") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::Rectangle, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_round_rect") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::RoundedRectangle, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_circle") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::Circle, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_ellipse") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::Ellipse, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_triangle") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::Triangle, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_diamond") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::Diamond, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_polygon") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::RegularPolygon, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "shape_star") {
        m_currentTool = "shape";
        m_canvas.StartShapeCreation(Folio::ShapeType::Star, m_canvas.shapeCreation.lockDrawingMode, &m_session);
    } else if (action == "toggle_lock_shape_mode") {
        m_canvas.shapeCreation.lockDrawingMode = sh->get_lock_shape_mode();
    } else if (action == "toggle_snap_grid") {
        m_canvas.shapeCreation.lockToGrid = sh->get_snap_to_grid();
    } else if (action == "toggle_ruler") {
        m_canvas.ruler.ToggleEnabled();
        sh->set_ruler_enabled(m_canvas.ruler.IsEnabled());
        if (m_canvas.ruler.IsEnabled()) {
            Point2D centerWorld = m_canvas.transform.ScreenToWorld(
                static_cast<float>(m_canvas.viewportW) * 0.5f,
                static_cast<float>(m_canvas.viewportH) * 0.5f);
            m_canvas.ruler.SetCenter(centerWorld.x, centerWorld.y);
        }
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "ruler_rot_ccw") {
        m_canvas.ruler.SetAngleDeg(m_canvas.ruler.GetAngleDeg() - 15.0);
        m_canvas.isDirty = true;
    } else if (action == "ruler_rot_cw") {
        m_canvas.ruler.SetAngleDeg(m_canvas.ruler.GetAngleDeg() + 15.0);
        m_canvas.isDirty = true;
    }
    // -------------------------------------------------------------------------
    // 7. VIEW, ZOOM & PAPER SETUP
    // -------------------------------------------------------------------------
    else if (action == "zoom_in") {
        m_canvas.ZoomAt(m_canvas.viewportW * 0.5, m_canvas.viewportH * 0.5, 1.25);
        char zBuf[32];
        std::snprintf(zBuf, sizeof(zBuf), "%.0f%%", m_canvas.transform.zoom * 100.0);
        sh->set_zoom_label(zBuf);
    } else if (action == "zoom_out") {
        m_canvas.ZoomAt(m_canvas.viewportW * 0.5, m_canvas.viewportH * 0.5, 0.8);
        char zBuf[32];
        std::snprintf(zBuf, sizeof(zBuf), "%.0f%%", m_canvas.transform.zoom * 100.0);
        sh->set_zoom_label(zBuf);
    } else if (action == "zoom_100") {
        m_canvas.transform.zoom = 1.0;
        m_canvas.HomeOrCenterPage();
        char zBuf[32];
        std::snprintf(zBuf, sizeof(zBuf), "%.0f%%", m_canvas.transform.zoom * 100.0);
        sh->set_zoom_label(zBuf);
    } else if (action == "zoom_center") {
        m_canvas.HomeOrCenterPage();
        char zBuf[32];
        std::snprintf(zBuf, sizeof(zBuf), "%.0f%%", m_canvas.transform.zoom * 100.0);
        sh->set_zoom_label(zBuf);
    } else if (action == "paper_blank") {
        m_canvas.currentPaperStyle = PaperStyle::Blank;
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_lined") {
        m_canvas.currentPaperStyle = PaperStyle::Lined;
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_grid") {
        m_canvas.currentPaperStyle = PaperStyle::Grid;
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_dotted") {
        m_canvas.currentPaperStyle = PaperStyle::Dotted;
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "grid_spacing_down") {
        m_canvas.gridSpacingMm = std::max(2.0, m_canvas.gridSpacingMm - 1.0);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "grid_spacing_up") {
        m_canvas.gridSpacingMm = std::min(25.0, m_canvas.gridSpacingMm + 1.0);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "toggle_invert_canvas") {
        m_canvas.SetInkColorInverted(sh->get_canvas_inverted());
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "toggle_page_border") {
        m_canvas.showPageBorder = sh->get_show_page_border();
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_color_white") {
        m_canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
        m_canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_color_ivory") {
        m_canvas.canvasBgColor = BLRgba32(0xFA, 0xF8, 0xF4);
        m_canvas.gridLineColor = BLRgba32(0xDE, 0xD4, 0xC4);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_color_sepia") {
        m_canvas.canvasBgColor = BLRgba32(0xF6, 0xEE, 0xDE);
        m_canvas.gridLineColor = BLRgba32(0xD8, 0xC8, 0xB0);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_color_blue") {
        m_canvas.canvasBgColor = BLRgba32(0xF0, 0xF4, 0xFA);
        m_canvas.gridLineColor = BLRgba32(0xC4, 0xD4, 0xE8);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_color_mint") {
        m_canvas.canvasBgColor = BLRgba32(0xEE, 0xF6, 0xF0);
        m_canvas.gridLineColor = BLRgba32(0xCA, 0xDE, 0xCE);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    } else if (action == "paper_color_dark") {
        m_canvas.canvasBgColor = BLRgba32(0x1E, 0x20, 0x26);
        m_canvas.gridLineColor = BLRgba32(0x34, 0x38, 0x44);
        m_canvas.needsFullRebake = true;
        m_canvas.isDirty = true;
    }

    SyncAndRenderCanvas(true);
#endif
}

void SlintMobileApplication::UpdateSlintNavigationModel() {
#if defined(FOLIONOTE_HAS_SLINT)
    if (!m_impl->shell.has_value()) return;
    auto& sh = *m_impl->shell;

    auto nb = m_session.workspace.GetActiveNotebook();
    if (!nb) return;

    sh->set_notebook_title(slint::SharedString(nb->name.c_str()));

    auto sec = nb->GetActiveSection();
    if (sec) {
        sh->set_section_title(slint::SharedString(sec->name.c_str()));
    }

    auto activePg = m_session.GetActivePage();
    if (activePg) {
        sh->set_page_title(slint::SharedString(activePg->title.c_str()));
    }

    // Populate sections list
    std::vector<MobileSlint::SectionInfo> secList;
    for (const auto& s : nb->sections) {
        if (!s) continue;
        secList.push_back(MobileSlint::SectionInfo{
            slint::SharedString(s->name.c_str()),
            slint::SharedString(s->guid.c_str()),
            slint::SharedString("#3B82F6"),
            (sec && sec->guid == s->guid)
        });
    }
    auto secModel = std::make_shared<slint::VectorModel<MobileSlint::SectionInfo>>(secList);
    sh->set_sections(secModel);

    // Populate pages list
    if (sec) {
        std::vector<MobileSlint::PageInfo> pgList;
        for (const auto& p : sec->pages) {
            if (!p) continue;
            pgList.push_back(MobileSlint::PageInfo{
                slint::SharedString(p->title.c_str()),
                slint::SharedString(p->guid.c_str()),
                (activePg && activePg->guid == p->guid)
            });
        }
        auto pgModel = std::make_shared<slint::VectorModel<MobileSlint::PageInfo>>(pgList);
        sh->set_pages(pgModel);
    }
#endif
}

void SlintMobileApplication::Run() {
    LOG_INFO(General, "Entering Slint native event loop");
#if defined(FOLIONOTE_HAS_SLINT)
    slint::run_event_loop();
#endif
}

void SlintMobileApplication::Shutdown() {
    if (m_running) {
        m_session.SaveActivePageAsync();
        m_session.workspace.FlushActiveNotebookAsync();
        if (auto activeNb = m_session.workspace.GetActiveNotebook()) {
            SettingsManager::Instance().lastActiveNotebookGuid = activeNb->guid;
            auto sec = activeNb->GetActiveSection();
            if (sec) {
                SettingsManager::Instance().lastActiveSectionGuid = sec->guid;
                auto page = sec->GetActivePage();
                if (page) {
                    SettingsManager::Instance().lastActivePageGuid = page->guid;
                }
            }
        }
        SettingsManager::Instance().Save();
        m_running = false;
    }
}

} // namespace Folio
