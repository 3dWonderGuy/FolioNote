#pragma once
/**
 * =========================================================================================
 * @file ui/slint/mobile/slint_mobile_app.hpp
 * @brief Native Slint Mobile & Touch Application Shell Runner
 * =========================================================================================
 */

#include <SDL3/SDL.h>
#include <string>
#include <memory>
#include <vector>

#include "app/settings_manager.hpp"
#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "input/input_manager.hpp"
#include "core/gpu/gpu_ink_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

class SlintMobileApplication {
public:
    SlintMobileApplication();
    ~SlintMobileApplication();

    bool Init(const std::string& title, int width, int height, const std::string& initialImportPath = "");
    void Run();
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    SDL_Window* m_window = nullptr;
    int m_width = 1080;
    int m_height = 1920;
    bool m_running = false;

    void UpdateSlintNavigationModel();
    void EnsureCanvasViewportMatch();
    void SyncAndRenderCanvas(bool forceImmediate = false);
    void HandleRibbonAction(const std::string& action);

    CanvasEngine m_canvas;
    DocumentSession m_session;
    InputManager m_inputManager;

    PenTool m_activePen;
    std::string m_currentTool = "pen";
    bool m_isErasing = false;
    double m_eraserRadiusMm = 3.0;
    Point2D m_lastEraserPoint{0.0, 0.0};

    bool m_isPanning = false;
    Point2D m_lastPanPoint{0.0, 0.0};

    bool m_isDraggingRuler = false;
    Point2D m_rulerDragOffsetWorld{0.0, 0.0};

    uint64_t m_lastRenderTicksNs = 0;
};

} // namespace Folio
