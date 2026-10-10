#pragma once
/**
 * =========================================================================================
 * @file ui/framework/ui_sidebar.hpp
 * @brief Standalone Animated Dockable Sidebar Component for FolioNote UI
 * =========================================================================================
 *
 * GENERAL ARCHITECTURE:
 * ---------------------
 * A reusable sidebar component supporting:
 * 1. Dual-state animated layout:
 *    - Compact Icon Rail (48px): Minimal screen footprint during full-screen canvas inking.
 *    - Expanded Drawer (260px): Hierarchical tree navigation, search, and metadata tags.
 * 2. Spring-animated width transitions that never stutter or cause heavy canvas rebakes.
 * 3. Modern acrylic surface rendering with subtle drop shadow and borders.
 * 4. Pinned header (search box, title) and pinned footer (settings, sync status).
 */

#include "imgui.h"
#include "ui/framework/ui_tokens.hpp"
#include "ui/framework/ui_animation_manager.hpp"
#include "ui/framework/ui_shape_manager.hpp"
#include "ui/framework/ui_builder.hpp"
#include <string>
#include <vector>
#include <functional>

namespace Folio::UI {

/**
 * @struct SidebarItem
 * @brief Item descriptor for list elements rendered inside the sidebar.
 */
struct SidebarItem {
    std::string id;
    std::string label;
    std::string iconText;
    std::string badgeText;
    bool isSelected = false;
    std::function<void()> onClick;
    std::function<void()> onContextMenu;
};

/**
 * @class UISidebarComponent
 * @brief Reusable animated sidebar drawer for page lists, notebook navigation, and settings.
 */
class UISidebarComponent {
public:
    explicit UISidebarComponent(const std::string& id);

    void SetExpanded(bool expanded);
    bool IsExpanded() const { return isExpanded_; }
    void ToggleExpanded() { SetExpanded(!isExpanded_); }

    float GetCurrentWidth() const { return currentWidth_; }
    bool IsAnimating() const { return std::abs(currentWidth_ - targetWidth_) > 0.5f; }

    /**
     * @brief Begins rendering the sidebar at the specified screen position and height.
     *
     * @param originX Left X coordinate.
     * @param originY Top Y coordinate.
     * @param contentH Total available height.
     * @param title Title displayed in expanded header.
     */
    void Begin(float originX, float originY, float contentH, const std::string& title = "");

    /**
     * @brief Renders a single interactive item within the sidebar list.
     */
    void RenderItem(const SidebarItem& item);

    /**
     * @brief Ends rendering the sidebar and draws the pinned footer.
     *
     * @param footerCallback Optional callback to render custom footer actions (e.g. + New Page).
     */
    void End(std::function<void()> footerCallback = nullptr);

private:
    std::string id_;
    bool isExpanded_ = true;
    float currentWidth_ = 260.0f;
    float targetWidth_ = 260.0f;

    static constexpr float kCompactWidth = 52.0f;
    static constexpr float kExpandedWidth = 260.0f;
};

} // namespace Folio::UI
