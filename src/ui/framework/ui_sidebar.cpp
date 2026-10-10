/**
 * =========================================================================================
 * @file ui/framework/ui_sidebar.cpp
 * @brief Implementation of the Standalone Animated Dockable Sidebar Component
 * =========================================================================================
 */

#include "ui/framework/ui_sidebar.hpp"
#include "app/theme_manager.hpp"
#include "imgui_internal.h"

namespace Folio::UI {

UISidebarComponent::UISidebarComponent(const std::string& id)
    : id_(id), isExpanded_(true), currentWidth_(kExpandedWidth), targetWidth_(kExpandedWidth) {}

void UISidebarComponent::SetExpanded(bool expanded) {
    isExpanded_ = expanded;
    targetWidth_ = expanded ? kExpandedWidth : kCompactWidth;
}

void UISidebarComponent::Begin(float originX, float originY, float contentH, const std::string& title) {
    // 1. Advance animated width using spring physics
    std::string animKey = "sidebar_w_" + id_;
    currentWidth_ = UIAnimationManager::Instance().GetSpring(animKey, targetWidth_, 240.0f, 24.0f);

    const auto& theme = ThemeManager::Instance();
    bool isDark = (theme.colorBg.x < 0.5f);

    // 2. Setup ImGui window
    ImGui::SetNextWindowPos(ImVec2(originX, originY));
    ImGui::SetNextWindowSize(ImVec2(currentWidth_, contentH));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar;

    ImU32 bgCol = isDark ? IM_COL32(26, 30, 40, 245) : IM_COL32(248, 250, 253, 245);
    ImU32 borderCol = isDark ? IM_COL32(48, 54, 68, 200) : IM_COL32(215, 222, 232, 220);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgCol);
    ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Spacing::xs, Spacing::sm));

    std::string winName = "##Sidebar_" + id_;
    ImGui::Begin(winName.c_str(), nullptr, flags);

    // 3. Draw right-side boundary line with subtle shadow
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 winMin = ImGui::GetWindowPos();
    ImVec2 winMax(winMin.x + currentWidth_, winMin.y + contentH);
    drawList->AddLine(ImVec2(winMax.x, winMin.y), winMax, borderCol, 1.0f);

    // 4. Header Bar with Toggle Button
    float headerH = 38.0f;
    ImGui::BeginChild("##Header", ImVec2(0, headerH), false, ImGuiWindowFlags_NoScrollbar);

    if (currentWidth_ > 120.0f) {
        ImGui::SetCursorPos(ImVec2(Spacing::sm, (headerH - ImGui::GetFontSize()) * 0.5f));
        if (!title.empty()) {
            ImU32 titleCol = isDark ? IM_COL32(235, 238, 245, 255) : IM_COL32(20, 24, 32, 255);
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.15f, 1.0f), "☰");
            ImGui::SameLine();
            ImGui::TextUnformatted(title.c_str());
        }

        // Toggle collapse button on top right
        float btnW = 28.0f;
        ImGui::SetCursorPos(ImVec2(currentWidth_ - btnW - Spacing::sm, (headerH - btnW) * 0.5f));
        if (UI::Button("◀").Size(UIButtonSize::Small).Variant(UIButtonVariant::Ghost).Width(btnW).Tooltip("Collapse sidebar").Render()) {
            SetExpanded(false);
        }
    } else {
        // Centered expand button in compact mode
        ImGui::SetCursorPos(ImVec2((currentWidth_ - 30.0f) * 0.5f, (headerH - 26.0f) * 0.5f));
        if (UI::Button("▶").Size(UIButtonSize::Small).Variant(UIButtonVariant::Ghost).Width(30.0f).Tooltip("Expand sidebar").Render()) {
            SetExpanded(true);
        }
    }

    ImGui::EndChild();
    ImGui::Separator();

    // 5. Scrollable Items Region (leave 50px for footer)
    float itemsH = contentH - headerH - 52.0f;
    ImGui::BeginChild("##ItemList", ImVec2(0, itemsH), false, ImGuiWindowFlags_None);
}

void UISidebarComponent::RenderItem(const SidebarItem& item) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const auto& theme = ThemeManager::Instance();
    bool isDark = (theme.colorBg.x < 0.5f);

    float itemH = 36.0f;
    float rowW = ImGui::GetContentRegionAvail().x;
    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + rowW, pos.y + itemH));

    ImGuiID itemId = window->GetID(item.id.c_str());
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, itemId)) return;

    bool hovered = false;
    bool held = false;
    bool clicked = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);

    if (clicked && item.onClick) {
        item.onClick();
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && item.onContextMenu) {
        item.onContextMenu();
    }

    // Smooth hover/active transition alpha
    float hoverAlpha = 0.0f;
    float activeAlpha = 0.0f;
    UIAnimationManager::Instance().GetInteractionAlphas(itemId, hovered || item.isSelected, held, hoverAlpha, activeAlpha);

    // Render interactive surface
    ImDrawList* drawList = window->DrawList;
    ImU32 restingFill = item.isSelected ? (isDark ? IM_COL32(45, 52, 68, 240) : IM_COL32(230, 235, 244, 255)) : IM_COL32(0, 0, 0, 0);
    ImU32 hoverFill = isDark ? IM_COL32(52, 60, 78, 255) : IM_COL32(220, 226, 238, 255);
    ImU32 currentFill = ColorUtils::Lerp(restingFill, hoverFill, hoverAlpha);

    if ((currentFill & IM_COL32_A_MASK) != 0) {
        drawList->AddRectFilled(bb.Min, bb.Max, currentFill, Radii::xs);
    }

    // Selected indicator left accent pill
    if (item.isSelected) {
        ImVec2 indMin(bb.Min.x + 2.0f, bb.Min.y + 4.0f);
        ImVec2 indMax(bb.Min.x + 5.0f, bb.Max.y - 4.0f);
        drawList->AddRectFilled(indMin, indMax, IM_COL32(242, 115, 38, 255), 2.0f);
    }

    // Render item contents
    float fontSize = ImGui::GetFontSize();
    float textY = bb.Min.y + (itemH - fontSize) * 0.5f;

    if (currentWidth_ > 120.0f) {
        // Expanded Mode: Icon + Label + Badge
        float iconX = bb.Min.x + Spacing::md;
        if (!item.iconText.empty()) {
            ImU32 iconCol = item.isSelected ? IM_COL32(242, 115, 38, 255) : (isDark ? IM_COL32(190, 198, 212, 230) : IM_COL32(85, 92, 105, 230));
            drawList->AddText(ImVec2(iconX, textY), iconCol, item.iconText.c_str());
            iconX += ImGui::CalcTextSize(item.iconText.c_str()).x + Spacing::sm;
        }

        ImU32 textCol = item.isSelected ? (isDark ? IM_COL32(255, 255, 255, 255) : IM_COL32(15, 18, 25, 255)) : (isDark ? IM_COL32(215, 222, 235, 230) : IM_COL32(35, 40, 50, 230));
        drawList->AddText(ImVec2(iconX, textY), textCol, item.label.c_str());

        if (!item.badgeText.empty()) {
            ImVec2 badgeSize = ImGui::CalcTextSize(item.badgeText.c_str());
            float badgeX = bb.Max.x - badgeSize.x - Spacing::lg;
            ImU32 badgeBg = isDark ? IM_COL32(255, 255, 255, 24) : IM_COL32(0, 0, 0, 18);
            ImU32 badgeTextCol = isDark ? IM_COL32(180, 186, 200, 220) : IM_COL32(100, 105, 116, 220);
            UIShapeManager::DrawPillBadge(drawList, ImVec2(badgeX + badgeSize.x * 0.5f, textY + fontSize * 0.5f),
                                          item.badgeText.c_str(), badgeBg, badgeTextCol);
        }
    } else {
        // Compact Rail Mode: Centered icon with tooltip flyout
        ImVec2 iconSize = ImGui::CalcTextSize(item.iconText.c_str());
        float iconX = bb.Min.x + (rowW - iconSize.x) * 0.5f;
        ImU32 iconCol = item.isSelected ? IM_COL32(242, 115, 38, 255) : (isDark ? IM_COL32(200, 208, 222, 230) : IM_COL32(75, 82, 95, 230));
        drawList->AddText(ImVec2(iconX, textY), iconCol, item.iconText.c_str());

        if (hovered) {
            ImGui::SetTooltip("%s", item.label.c_str());
        }
    }
}

void UISidebarComponent::End(std::function<void()> footerCallback) {
    ImGui::EndChild(); // End items region

    // 6. Pinned Footer Region
    ImGui::Separator();
    float footerH = 46.0f;
    ImGui::BeginChild("##Footer", ImVec2(0, footerH), false, ImGuiWindowFlags_NoScrollbar);

    if (footerCallback) {
        footerCallback();
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace Folio::UI
