#pragma once
/**
 * =========================================================================================
 * @file ui/shell/app_shell.hpp
 * @brief Application Window Shell Layout Orchestration & Geometry Calculator
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * --------------------------------
 * `AppShell` isolates window layout math and slot coordinates from domain rendering.
 * It manages:
 *   - Slot 0: Custom TitleBar (0.0f height in fullscreen)
 *   - Slot 1: Ribbon Bar (dynamic animated height based on 4 ribbon display modes)
 *   - Slot 2: Navigation Drawer (dynamic width based on column collapse/expand state)
 *   - Slot 3: Central Content Viewport (fills exact remainder of the window space)
 *   - Pull Tab: Floating ribbon restore tab when ribbon is collapsed/hidden
 */

#include <imgui.h>

namespace Folio {

/**
 * @struct ShellLayoutMetrics
 * @brief Computed 2D layout bounding rectangles (in logical screen pixels) for all primary UI slots.
 */
struct ShellLayoutMetrics {
    float screenW = 0.0f;
    float screenH = 0.0f;

    // Slot 0: TitleBar
    float titleBarY = 0.0f;
    float titleBarH = 0.0f;

    // Slot 1: Ribbon Bar
    float ribbonY = 0.0f;
    float ribbonH = 0.0f;

    // Slot 2: Navigation Drawer (Left Sidebar)
    float navX = 0.0f;
    float navY = 0.0f;
    float navW = 0.0f;
    float navH = 0.0f;

    // Slot 3: Primary Viewport (Canvas / PDF / Markdown)
    float viewportX = 0.0f;
    float viewportY = 0.0f;
    float viewportW = 0.0f;
    float viewportH = 0.0f;

    // Floating Ribbon Pull Tab
    bool showRibbonPullTab = false;
    float pullTabX = 0.0f;
    float pullTabY = 0.0f;
    float pullTabW = 130.0f;
    float pullTabH = 32.0f;
};

/**
 * @class AppShell
 * @brief Main window layout calculator and geometry coordinator.
 */
class AppShell {
public:
    AppShell() = default;

    /**
     * @brief Computes exact pixel-perfect layout geometry for the current frame.
     *
     * @param screenW Logical window width in pixels.
     * @param screenH Logical window height in pixels.
     * @param isFullscreen True if OS window is currently in fullscreen mode.
     * @param isTitleBarVisible True if custom titlebar is enabled in settings.
     * @param titleBarBaseHeight Configured titlebar height constant (e.g. 40.0f).
     * @param ribbonAnimatedHeight Current animated height of the top ribbon (0..160px).
     * @param navPanelWidth Current total width of the navigation sidebar.
     * @return ShellLayoutMetrics Computed slot boundaries.
     */
    static ShellLayoutMetrics ComputeLayout(
        float screenW,
        float screenH,
        bool isFullscreen,
        bool isTitleBarVisible,
        float titleBarBaseHeight,
        float ribbonAnimatedHeight,
        float navPanelWidth
    );
};

} // namespace Folio
