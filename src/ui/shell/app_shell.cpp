/**
 * =========================================================================================
 * @file ui/shell/app_shell.cpp
 * @brief Implementation of AppShell Layout Metrics
 * =========================================================================================
 */

#include "ui/shell/app_shell.hpp"
#include <algorithm>

namespace Folio {

ShellLayoutMetrics AppShell::ComputeLayout(
    float screenW,
    float screenH,
    bool isFullscreen,
    bool isTitleBarVisible,
    float titleBarBaseHeight,
    float ribbonAnimatedHeight,
    float navPanelWidth
) {
    ShellLayoutMetrics m;
    m.screenW = screenW;
    m.screenH = screenH;

    // 0. TitleBar Slot
    m.titleBarY = 0.0f;
    m.titleBarH = (isTitleBarVisible && !isFullscreen) ? titleBarBaseHeight : 0.0f;

    // 1. Ribbon Bar Slot
    m.ribbonY = m.titleBarH;
    m.ribbonH = ribbonAnimatedHeight;

    // 2. Navigation Sidebar Slot
    m.navX = 0.0f;
    m.navY = m.titleBarH + m.ribbonH;
    m.navW = navPanelWidth;
    m.navH = std::max(0.0f, screenH - m.navY);

    // 3. Central Viewport Slot (Fills exact remainder)
    m.viewportX = m.navW;
    m.viewportY = m.navY;
    m.viewportW = std::max(0.0f, screenW - m.navW);
    m.viewportH = std::max(0.0f, screenH - m.navY);

    // 4. Floating Ribbon Pull Tab
    m.showRibbonPullTab = (m.ribbonH <= 8.0f);
    if (m.showRibbonPullTab) {
        m.pullTabW = 130.0f;
        m.pullTabH = 32.0f;
        m.pullTabX = (screenW - m.pullTabW) * 0.5f;
        m.pullTabY = m.titleBarH + 4.0f;
    }

    return m;
}

} // namespace Folio
