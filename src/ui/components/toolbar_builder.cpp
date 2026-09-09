#include "ui/components/toolbar_builder.hpp"
#include "ui/imgui_theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>

namespace FolioUI {

// ============================================================================
// 1. FLYOUT SUBMENU BUILDER IMPLEMENTATION
// ============================================================================

FlyoutMenuBuilder::FlyoutMenuBuilder(const ThemeManager& t) : theme(t) {}

void FlyoutMenuBuilder::AddHeader(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    hasItems = true;
}

void FlyoutMenuBuilder::AddSeparator() {
    ImGui::Separator();
}

bool FlyoutMenuBuilder::AddItem(
    const char* label,
    GLuint iconTex,
    const char* shortcut,
    bool isSelected,
    std::function<void()> onSelect
) {
    hasItems = true;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float availW = ImGui::GetContentRegionAvail().x;
    float itemH = 30.0f;
    ImVec2 size(std::max(availW, 180.0f), itemH);

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##item", size);
    bool hovered = ImGui::IsItemHovered();

    // Background
    if (hovered) {
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 4.0f);
    } else if (isSelected) {
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected), 4.0f);
    }

    // Active selection check indicator (left margin)
    float textStartX = pos.x + 10.0f;
    if (isSelected) {
        drawList->AddCircleFilled(ImVec2(pos.x + 14.0f, pos.y + itemH * 0.5f), 3.5f,
            ImGui::ColorConvertFloat4ToU32(theme.colorPrimary));
        textStartX += 18.0f;
    }

    // Icon
    if (iconTex != 0) {
        float iconSize = 18.0f;
        float iconY = pos.y + (itemH - iconSize) * 0.5f;
        drawList->AddImage((ImTextureID)(intptr_t)iconTex,
            ImVec2(textStartX, iconY),
            ImVec2(textStartX + iconSize, iconY + iconSize));
        textStartX += iconSize + 8.0f;
    }

    // Label
    ImVec4 textColor = isSelected ? theme.colorItemSelectedText : (hovered ? theme.colorText : theme.colorItemText);
    ImVec2 labelSize = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(textStartX, pos.y + (itemH - labelSize.y) * 0.5f),
        ImGui::ColorConvertFloat4ToU32(textColor), label);

    // Shortcut (right aligned)
    if (shortcut && shortcut[0] != '\0') {
        ImVec2 scSize = ImGui::CalcTextSize(shortcut);
        float scX = pos.x + size.x - scSize.x - 12.0f;
        drawList->AddText(ImVec2(scX, pos.y + (itemH - scSize.y) * 0.5f),
            ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), shortcut);
    }

    ImGui::PopID();

    if (clicked) {
        if (onSelect) onSelect();
        ImGui::CloseCurrentPopup();
    }
    return clicked;
}

bool FlyoutMenuBuilder::AddItem(
    const char* label,
    GLuint iconTex,
    const char* shortcut,
    std::function<void()> onSelect,
    bool isSelected
) {
    return AddItem(label, iconTex, shortcut, isSelected, onSelect);
}

bool FlyoutMenuBuilder::AddItem(
    const char* label,
    std::function<void()> onSelect,
    bool isSelected
) {
    return AddItem(label, 0, nullptr, isSelected, onSelect);
}

bool FlyoutMenuBuilder::AddCheckItem(
    const char* label,
    bool isChecked,
    std::function<void(bool)> onToggle
) {
    return AddItem(label, 0, nullptr, isChecked, [&, isChecked, onToggle]() {
        if (onToggle) onToggle(!isChecked);
    });
}

void FlyoutMenuBuilder::AddCustom(const std::function<void()>& drawFunc) {
    if (drawFunc) {
        hasItems = true;
        drawFunc();
    }
}

// ============================================================================
// 2. SETTINGS POPOVER BUILDER IMPLEMENTATION
// ============================================================================

SettingsPopoverBuilder::SettingsPopoverBuilder(const ThemeManager& t) : theme(t) {}

void SettingsPopoverBuilder::AddTitle(const char* title) {
    ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();
}

bool SettingsPopoverBuilder::AddSlider(const char* label, float* value, float minVal, float maxVal, const char* format) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(100.0f);
    ImGui::SetNextItemWidth(140.0f);
    return ImGui::SliderFloat((std::string("##slider_") + label).c_str(), value, minVal, maxVal, format);
}

bool SettingsPopoverBuilder::AddCheckbox(const char* label, bool* value) {
    return ImGui::Checkbox(label, value);
}

bool SettingsPopoverBuilder::AddColorPicker(const char* label, ImVec4* color) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(100.0f);
    return ImGui::ColorEdit4((std::string("##cp_") + label).c_str(), (float*)color,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueWheel);
}

bool SettingsPopoverBuilder::AddButton(const char* label, ImVec2 size) {
    return ImGui::Button(label, size);
}

void SettingsPopoverBuilder::AddCustom(const std::function<void()>& drawFunc) {
    if (drawFunc) drawFunc();
}

// ============================================================================
// 3. LOW-LEVEL TOOLBAR CONTROLS IMPLEMENTATION
// ============================================================================

// Helper: Get a distinct dark outline color for high-contrast click feedback
static inline ImU32 GetPressedOutlineColor(const ThemeManager& theme) {
    bool isDark = (theme.colorBg.x < 0.5f);
    if (isDark) {
        return IM_COL32(10, 11, 14, 255); // Deep dark charcoal/black outline in dark mode
    } else {
        return IM_COL32(38, 42, 50, 240); // Crisp dark slate outline in light mode
    }
}

bool ToolbarControls::RenderLargeButton(
    const char* strId,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    const ThemeManager& theme,
    bool isActive,
    bool flipH,
    ImVec2 size
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(strId);
    bool clicked = ImGui::InvisibleButton("##large_btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemActive();

    ImVec2 pMin = pos;
    ImVec2 pMax = ImVec2(pos.x + size.x, pos.y + size.y);
    float rounding = theme.frameRounding;

    // Background & Outline:
    // Press: dark outline shows for tactile click feedback
    // Release / Active: slightly greyer (colorItemSelected), never orange
    if (pressed) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = GetPressedOutlineColor(theme);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.8f);
    } else if (isActive) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorItemHover : theme.colorItemSelected);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorBorder);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.0f);
    } else if (hovered) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
    }

    // Icon (centered horizontally in top portion, or centered vertically if size.y < 40)
    float iconW = size.y < 40.0f ? 20.0f : 26.0f;
    float iconH = size.y < 40.0f ? 20.0f : 26.0f;
    float iconX = pos.x + (size.x - iconW) * 0.5f;
    float iconY = size.y < 40.0f ? pos.y + (size.y - iconH) * 0.5f : pos.y + 6.0f;

    if (iconTex != 0) {
        ImVec2 uv0 = flipH ? ImVec2(1.0f, 0.0f) : ImVec2(0.0f, 0.0f);
        ImVec2 uv1 = flipH ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 1.0f);
        ImU32 iconTint = isActive ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                  : ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorText : theme.colorItemText);
        drawList->AddImage((ImTextureID)(intptr_t)iconTex,
            ImVec2(iconX, iconY), ImVec2(iconX + iconW, iconY + iconH), uv0, uv1, iconTint);
    } else {
        // Text fallback icon
        const char* fallbackStr = label ? label : "?";
        ImVec2 fSize = ImGui::CalcTextSize(fallbackStr);
        drawList->AddText(ImVec2(pos.x + (size.x - fSize.x) * 0.5f, iconY + (size.y < 40.0f ? 0.0f : 4.0f)),
            ImGui::ColorConvertFloat4ToU32(theme.colorText), fallbackStr);
    }

    // Label (centered horizontally in bottom portion, only if height >= 40)
    if (size.y >= 40.0f && label && label[0] != '\0') {
        ImVec2 lblSize = ImGui::CalcTextSize(label);
        float lblX = pos.x + (size.x - lblSize.x) * 0.5f;
        float lblY = pos.y + size.y - lblSize.y - 4.0f;
        ImU32 textCol = isActive ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                 : ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorText : theme.colorTextMuted);
        drawList->AddText(ImVec2(lblX, lblY), textCol, label);
    }

    if (tooltip && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }

    ImGui::PopID();
    return clicked;
}

bool ToolbarControls::RenderSmallButton(
    const char* strId,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    const ThemeManager& theme,
    bool isActive,
    bool flipH,
    ImVec2 size
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(strId);
    bool clicked = ImGui::InvisibleButton("##sml_btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemActive();

    ImVec2 pMin = pos;
    ImVec2 pMax = ImVec2(pos.x + size.x, pos.y + size.y);
    float rounding = 4.0f;

    if (pressed) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = GetPressedOutlineColor(theme);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.8f);
    } else if (isActive) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorItemHover : theme.colorItemSelected);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorBorder);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.0f);
    } else if (hovered) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
    }

    float iconSize = 18.0f;
    float iconX = pos.x + (size.x - iconSize) * 0.5f;
    float iconY = pos.y + (size.y - iconSize) * 0.5f;

    if (iconTex != 0) {
        ImVec2 uv0 = flipH ? ImVec2(1.0f, 0.0f) : ImVec2(0.0f, 0.0f);
        ImVec2 uv1 = flipH ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 1.0f);
        ImU32 iconTint = isActive ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                  : ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorText : theme.colorItemText);
        drawList->AddImage((ImTextureID)(intptr_t)iconTex,
            ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize), uv0, uv1, iconTint);
    } else if (label) {
        ImVec2 lblSize = ImGui::CalcTextSize(label);
        drawList->AddText(ImVec2(pos.x + (size.x - lblSize.x) * 0.5f, pos.y + (size.y - lblSize.y) * 0.5f),
            ImGui::ColorConvertFloat4ToU32(theme.colorText), label);
    }

    if (tooltip && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }

    ImGui::PopID();
    return clicked;
}

bool ToolbarControls::RenderSplitButton(
    const char* strId,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    const ThemeManager& theme,
    bool isActive,
    std::function<void()> onAction,
    std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
    bool flipH,
    ImVec2 totalSize
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float chevronW = 16.0f;
    float actionW = totalSize.x - chevronW;

    std::string popupId = std::string("##split_popup_") + strId;
    ImGui::PushID(strId);

    // 1. Action Zone (Left)
    bool actionClicked = ImGui::InvisibleButton("##act_zone", ImVec2(actionW, totalSize.y));
    bool actionHovered = ImGui::IsItemHovered();
    bool actionActive = ImGui::IsItemActive();

    // 2. Chevron Zone (Right)
    ImGui::SameLine(0, 0);
    bool chevronClicked = ImGui::InvisibleButton("##chev_zone", ImVec2(chevronW, totalSize.y));
    bool chevronHovered = ImGui::IsItemHovered();
    bool chevronActive = ImGui::IsItemActive();

    bool isHeld = actionActive || chevronActive;
    ImVec2 pMin = pos;
    ImVec2 pMax = ImVec2(pos.x + totalSize.x, pos.y + totalSize.y);
    float rounding = theme.frameRounding;

    if (isHeld) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = GetPressedOutlineColor(theme);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.8f);
    } else if (isActive) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32((actionHovered || chevronHovered) ? theme.colorItemHover : theme.colorItemSelected);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorBorder);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.0f);
    } else if (actionHovered || chevronHovered) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
    }

    // Slight highlight on the chevron zone if hovered separately and not pressed/active
    if (chevronHovered && !isActive && !isHeld) {
        drawList->AddRectFilled(ImVec2(pos.x + actionW, pos.y),
            pMax,
            ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected), rounding, ImDrawFlags_RoundCornersRight);
    }

    // Divider between action and chevron
    drawList->AddLine(
        ImVec2(pos.x + actionW, pos.y + 6.0f),
        ImVec2(pos.x + actionW, pos.y + totalSize.y - 6.0f),
        ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 1.0f
    );

    // Action Icon & Label
    float iconW = totalSize.y < 40.0f ? 18.0f : 24.0f;
    float iconH = totalSize.y < 40.0f ? 18.0f : 24.0f;
    float iconX = pos.x + (actionW - iconW) * 0.5f;
    float iconY = totalSize.y < 40.0f ? pos.y + (totalSize.y - iconH) * 0.5f : pos.y + 6.0f;

    if (iconTex != 0) {
        ImVec2 uv0 = flipH ? ImVec2(1.0f, 0.0f) : ImVec2(0.0f, 0.0f);
        ImVec2 uv1 = flipH ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 1.0f);
        ImU32 iconTint = isActive ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                  : ImGui::ColorConvertFloat4ToU32(actionHovered ? theme.colorText : theme.colorItemText);
        drawList->AddImage((ImTextureID)(intptr_t)iconTex,
            ImVec2(iconX, iconY), ImVec2(iconX + iconW, iconY + iconH), uv0, uv1, iconTint);
    }

    if (totalSize.y >= 40.0f && label) {
        ImVec2 lblSize = ImGui::CalcTextSize(label);
        float lblX = pos.x + (actionW - lblSize.x) * 0.5f;
        float lblY = pos.y + totalSize.y - lblSize.y - 4.0f;
        ImU32 textCol = isActive ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                 : ImGui::ColorConvertFloat4ToU32(actionHovered ? theme.colorText : theme.colorTextMuted);
        drawList->AddText(ImVec2(lblX, lblY), textCol, label);
    }

    // Chevron Arrow (downward triangle)
    float chevCenterX = pos.x + actionW + chevronW * 0.5f;
    float chevCenterY = pos.y + totalSize.y * 0.5f;
    ImVec2 p1(chevCenterX - 3.5f, chevCenterY - 2.0f);
    ImVec2 p2(chevCenterX + 3.5f, chevCenterY - 2.0f);
    ImVec2 p3(chevCenterX, chevCenterY + 2.5f);
    drawList->AddTriangleFilled(p1, p2, p3,
        ImGui::ColorConvertFloat4ToU32(chevronHovered ? theme.colorText : theme.colorTextMuted));

    // Tooltip
    if (tooltip && (actionHovered || chevronHovered) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }

    // Handlers
    if (actionClicked && onAction) {
        onAction();
    }
    if (chevronClicked) {
        ImGui::OpenPopup(popupId.c_str());
    }

    // Render Submenu Popup if open
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.colorPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

    if (ImGui::BeginPopup(popupId.c_str())) {
        if (onBuildMenu) {
            FlyoutMenuBuilder menuBuilder(theme);
            onBuildMenu(menuBuilder);
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();

    return actionClicked;
}

bool ToolbarControls::RenderPillButton(
    const char* strId,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    const ThemeManager& theme,
    bool isActive,
    ImVec4 accentColor,
    std::function<void()> onClick,
    std::function<void(SettingsPopoverBuilder&)> onSettings,
    ImVec2 size
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float rounding = 14.0f;

    std::string settingsPopupId = std::string("##pill_settings_") + strId;
    ImGui::PushID(strId);

    bool clicked = ImGui::InvisibleButton("##pill_btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemActive();
    ImVec2 pMin = pos;
    ImVec2 pMax = ImVec2(pos.x + size.x, pos.y + size.y);

    if (pressed) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = GetPressedOutlineColor(theme);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.8f);
    } else if (isActive) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorItemHover : theme.colorItemSelected);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorBorder);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.0f);
    } else if (hovered) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
    } else {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorPanel);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
    }

    // Icon
    float iconSize = size.y < 40.0f ? 18.0f : 22.0f;
    float iconX = pos.x + (size.x - iconSize) * 0.5f;
    float iconY = size.y < 40.0f ? pos.y + (size.y - iconSize - 3.0f) * 0.5f : pos.y + 6.0f;
    if (iconTex != 0) {
        drawList->AddImage((ImTextureID)(intptr_t)iconTex,
            ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize));
    }

    // Label (only when height >= 40)
    if (size.y >= 40.0f && label) {
        ImVec2 lblSize = ImGui::CalcTextSize(label);
        float lblX = pos.x + (size.x - lblSize.x) * 0.5f;
        float lblY = pos.y + 30.0f;
        drawList->AddText(ImVec2(lblX, lblY),
            ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorText : theme.colorTextMuted), label);
    }

    // Colored accent bar / dot at bottom
    float barW = size.x - (size.y < 40.0f ? 16.0f : 24.0f);
    float barH = size.y < 40.0f ? 2.5f : 3.5f;
    float barX = pos.x + (size.x - barW) * 0.5f;
    float barY = pos.y + size.y - (size.y < 40.0f ? 4.0f : 7.0f);
    drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
        ImGui::ColorConvertFloat4ToU32(accentColor), 2.0f);

    // Tooltip
    if (tooltip && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }

    // Right-click opens settings popover if provided
    if (onSettings && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(settingsPopupId.c_str());
    }

    if (clicked && onClick) {
        onClick();
    }

    // Settings Popover
    if (onSettings) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 14));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.colorPanel);
        ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

        if (ImGui::BeginPopup(settingsPopupId.c_str())) {
            SettingsPopoverBuilder popover(theme);
            onSettings(popover);
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ImGui::PopID();
    return clicked;
}

bool ToolbarControls::RenderCircleButton(
    const char* strId,
    ImVec4 fillColor,
    const char* tooltip,
    const ThemeManager& theme,
    bool isActive,
    float diameter
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(diameter, diameter);

    ImGui::PushID(strId);
    bool clicked = ImGui::InvisibleButton("##circ_btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemActive();

    ImVec2 center(pos.x + diameter * 0.5f, pos.y + diameter * 0.5f);
    float radius = (diameter * 0.5f) - 3.0f;

    // Fill circle
    drawList->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(fillColor));

    // Outline ring
    if (pressed) {
        drawList->AddCircle(center, radius + 2.0f,
            GetPressedOutlineColor(theme), 0, 2.0f);
    } else if (isActive) {
        drawList->AddCircle(center, radius + 2.0f,
            ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 0, 2.0f);
    } else if (hovered) {
        drawList->AddCircle(center, radius + 1.5f,
            ImGui::ColorConvertFloat4ToU32(theme.colorText), 0, 1.5f);
    } else {
        drawList->AddCircle(center, radius,
            ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 0, 1.0f);
    }

    if (tooltip && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }

    ImGui::PopID();
    return clicked;
}

bool ToolbarControls::RenderPenNibControl(
    const char* strId,
    PenPreset& preset,
    bool isActive,
    const ThemeManager& theme,
    std::function<void()> onSelect,
    std::function<void(PenPreset&)> onCustomChange,
    std::function<void(const std::string&)> onDelete,
    ImVec2 size,
    int presetIndex,
    int totalPresets,
    std::function<void(int fromIdx, int toIdx)> onReorder
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    std::string popupId = std::string("##nib_popup_") + strId;

    ImGui::PushID(strId);
    bool clicked = ImGui::InvisibleButton("##nib_btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemActive();

    // Drag & Drop reordering support: click-hold and drag pen to reorder
    if (presetIndex >= 0 && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("PEN_REORDER_DND", &presetIndex, sizeof(int));
        ImGui::Text("Moving %s", preset.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (presetIndex >= 0 && ImGui::BeginDragDropTarget()) {
        // AcceptBeforeDelivery allows peeking mouse position to show between-marker indicator while dragging;
        // AcceptNoDrawDefaultRect suppresses the distracting default yellow rectangle around the marker.
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PEN_REORDER_DND",
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload != nullptr && payload->Data != nullptr) {
            int srcIndex = *(const int*)payload->Data;
            ImVec2 mousePos = ImGui::GetMousePos();
            bool isRightHalf = (mousePos.x > (pos.x + size.x * 0.5f));

            // Determine target slot
            int targetSlot = presetIndex;
            if (isRightHalf) {
                targetSlot = (srcIndex < presetIndex) ? presetIndex : (presetIndex + 1);
            } else {
                targetSlot = (srcIndex < presetIndex) ? (presetIndex - 1) : presetIndex;
            }

            // Draw crisp insertion beam between the markers rather than highlighting the marker itself
            float lineX = isRightHalf ? (pos.x + size.x + 2.0f) : (pos.x - 2.0f);
            float lineY1 = pos.y + 3.0f;
            float lineY2 = pos.y + size.y - 3.0f;

            // Vivid electric blue indicator (high contrast in both dark and light modes)
            ImU32 beamColor = IM_COL32(59, 130, 246, 255);
            ImU32 glowColor = IM_COL32(59, 130, 246, 80);

            // Subtle glow + crisp core insertion line
            drawList->AddLine(ImVec2(lineX, lineY1), ImVec2(lineX, lineY2), glowColor, 6.0f);
            drawList->AddLine(ImVec2(lineX, lineY1), ImVec2(lineX, lineY2), beamColor, 2.5f);

            // Top and bottom dumbbell endpoint caps
            drawList->AddCircleFilled(ImVec2(lineX, lineY1 + 1.0f), 3.5f, beamColor);
            drawList->AddCircleFilled(ImVec2(lineX, lineY2 - 1.0f), 3.5f, beamColor);

            if (payload->IsDelivery() && onReorder && targetSlot != srcIndex) {
                onReorder(srcIndex, targetSlot);
            }
        }
        ImGui::EndDragDropTarget();
    }
    bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    bool rightClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // Dynamic vertical elevation when selected or hovered
    float elevY = isActive ? -3.0f : (hovered ? -1.5f : 0.0f);

    ImVec2 pMin(pos.x + 2.0f, pos.y + 2.0f + elevY);
    ImVec2 pMax(pos.x + size.x - 2.0f, pos.y + size.y - 2.0f + elevY);
    float rounding = 8.0f;

    // 1. Background slot pill
    if (pressed) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = GetPressedOutlineColor(theme);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.8f);
    } else if (isActive) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(hovered ? theme.colorItemHover : theme.colorItemSelected);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorBorder);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.2f);
    } else if (hovered) {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
    }

    // 2. Geometry calculations for 45° angled technical pen art
    ImVec2 elev = isActive ? ImVec2(-2.0f, -2.0f) : ImVec2(0.0f, 0.0f);
    float scale = (size.y < 45.0f) ? 0.65f : 1.0f;

    // 45° Unit vectors:
    // u points backwards from writing tip up along the barrel axis (top-right)
    const ImVec2 u(0.707107f, -0.707107f);
    // n points perpendicular to barrel axis (up-left)
    const ImVec2 n(-0.707107f, -0.707107f);

    ImVec2 tipOrigin = (size.y < 45.0f)
        ? ImVec2(pos.x + 6.0f + elev.x, pos.y + size.y - 7.0f + elev.y)
        : ImVec2(pos.x + 9.0f + elev.x, pos.y + size.y - 11.0f + elev.y);

    auto P = [&](float s, float offsetN = 0.0f) -> ImVec2 {
        float scaledS = s * scale;
        float scaledN = offsetN * scale;
        return ImVec2(tipOrigin.x + scaledS * u.x + scaledN * n.x,
                      tipOrigin.y + scaledS * u.y + scaledN * n.y);
    };

    ImU32 inkCol = ImGui::ColorConvertFloat4ToU32(preset.color);

    float w_barrel = 4.2f;
    float w_collar = 4.5f;

    // 3. Clean Dark Technical Barrel Body (s = 17.0f to 38.0f)
    ImU32 barrelCol = IM_COL32(32, 35, 42, 255);
    drawList->AddQuadFilled(
        P(17.0f, -w_barrel),
        P(17.0f,  w_barrel),
        P(38.0f,  w_barrel),
        P(38.0f, -w_barrel),
        barrelCol
    );

    // Top metallic end cap / chamfer (s = 38.0f to 40.5f)
    drawList->AddQuadFilled(
        P(38.0f, -w_barrel),
        P(38.0f,  w_barrel),
        P(40.5f,  w_barrel - 1.2f),
        P(40.5f, -(w_barrel - 1.2f)),
        IM_COL32(185, 190, 200, 255)
    );

    // Longitudinal cylindrical specular highlight along upper edge
    drawList->AddLine(
        P(18.0f, w_barrel - 1.0f),
        P(37.0f, w_barrel - 1.0f),
        IM_COL32(255, 255, 255, 50),
        1.0f
    );

    // 4. Colored Collar Ring Displaying Preset Width and Color (s = 11.5f to 17.0f)
    drawList->AddQuadFilled(
        P(11.5f, -w_collar),
        P(11.5f,  w_collar),
        P(17.0f,  w_collar),
        P(17.0f, -w_collar),
        inkCol
    );
    // Chrome accent rings bordering collar ring
    drawList->AddLine(P(11.5f, -w_collar), P(11.5f, w_collar), IM_COL32(225, 230, 240, 230), 1.0f);
    drawList->AddLine(P(17.0f, -w_collar), P(17.0f, w_collar), IM_COL32(225, 230, 240, 230), 1.0f);

    // Inner width indicator band on collar ring reflecting stroke width
    float ringWidth = std::clamp(preset.thicknessMm * 1.2f, 1.0f, 3.5f);
    drawList->AddLine(P(14.25f, -w_collar + 0.6f), P(14.25f, w_collar - 0.6f), IM_COL32(255, 255, 255, 150), ringWidth * scale);

    // 5. Procedural Nib Tip Art & Tip Lead/Ball
    switch (preset.type) {
    case PenType::Pencil: {
        // Natural cedar wood cone (s = 3.5f to 11.5f)
        drawList->AddQuadFilled(
            P(3.5f, -1.4f),
            P(3.5f,  1.4f),
            P(11.5f,  3.5f),
            P(11.5f, -3.5f),
            IM_COL32(215, 185, 140, 255)
        );
        // Graphite lead tip (s = 0.0f to 3.5f)
        drawList->AddTriangleFilled(
            P(3.5f, -1.2f),
            P(3.5f,  1.2f),
            P(0.0f,  0.0f),
            IM_COL32(45, 47, 52, 255)
        );
        break;
    }
    case PenType::Pen: {
        // Machined metallic drafting cone (s = 3.5f to 11.5f)
        drawList->AddQuadFilled(
            P(3.5f, -1.3f),
            P(3.5f,  1.3f),
            P(11.5f,  3.5f),
            P(11.5f, -3.5f),
            IM_COL32(195, 200, 210, 255)
        );
        // Specular sheen on cone
        drawList->AddLine(P(3.8f, 0.8f), P(11.2f, 2.8f), IM_COL32(255, 255, 255, 80), 1.0f);

        // Technical drafting guide pipe (s = 2.0f to 3.5f)
        drawList->AddQuadFilled(
            P(2.0f, -0.9f),
            P(2.0f,  0.9f),
            P(3.5f,  0.9f),
            P(3.5f, -0.9f),
            IM_COL32(170, 175, 185, 255)
        );

        // Tungsten carbide ballpoint tip with active ink color (s = 1.0f)
        drawList->AddCircleFilled(P(1.0f, 0.0f), 1.8f * scale, inkCol);
        drawList->AddCircleFilled(P(0.8f, 0.4f), 0.6f * scale, IM_COL32(255, 255, 255, 240)); // Hotspot
        break;
    }
    case PenType::Fountain: {
        // Metallic fountain nib cone (s = 3.0f to 11.5f)
        drawList->AddQuadFilled(
            P(3.0f, -2.2f),
            P(3.0f,  2.2f),
            P(11.5f,  3.5f),
            P(11.5f, -3.5f),
            IM_COL32(195, 200, 210, 255)
        );
        // Center slit & breather hole
        drawList->AddLine(P(3.0f, 0.0f), P(9.5f, 0.0f), IM_COL32(40, 43, 50, 200), 1.0f);
        drawList->AddCircleFilled(P(9.5f, 0.0f), 1.0f * scale, IM_COL32(40, 43, 50, 200));

        // Inked tip point
        drawList->AddLine(P(1.5f, -1.6f), P(1.5f, 1.6f), inkCol, 2.0f * scale);
        break;
    }
    case PenType::Brush: {
        // Chrome ferrule band (s = 8.5f to 11.5f)
        drawList->AddQuadFilled(
            P(8.5f, -3.2f),
            P(8.5f,  3.2f),
            P(11.5f,  3.5f),
            P(11.5f, -3.5f),
            IM_COL32(200, 205, 215, 255)
        );
        // Pointed brush hair teardrop (s = 0.5f to 8.5f)
        drawList->AddTriangleFilled(
            P(8.5f, -2.8f),
            P(8.5f,  2.8f),
            P(0.5f,  0.0f),
            inkCol
        );
        break;
    }
    case PenType::Highlighter: {
        // Translucent wide angled chisel wedge (s = 1.0f to 11.5f)
        drawList->AddQuadFilled(
            P(1.0f, -3.2f),
            P(3.5f,  3.2f),
            P(11.5f,  3.5f),
            P(11.5f, -3.5f),
            inkCol
        );
        break;
    }
    case PenType::LaserPointer: {
        // Aperture bezel cone (s = 4.0f to 11.5f)
        drawList->AddQuadFilled(
            P(4.0f, -2.2f),
            P(4.0f,  2.2f),
            P(11.5f,  3.5f),
            P(11.5f, -3.5f),
            IM_COL32(200, 205, 218, 255)
        );
        // Laser emitter lens & glowing hotspot
        drawList->AddCircleFilled(P(4.0f, 0.0f), 3.5f * scale, (inkCol & 0x00FFFFFF) | 0x60000000); // Bloom
        drawList->AddCircleFilled(P(4.0f, 0.0f), 2.2f * scale, inkCol);                              // Core
        drawList->AddCircleFilled(P(4.0f, 0.0f), 1.0f * scale, IM_COL32(255, 255, 255, 240));       // Hotspot

        // Laser beam to target dot
        drawList->AddLine(P(4.0f, 0.0f), P(0.5f, 0.0f), inkCol, 1.8f * scale);
        drawList->AddCircleFilled(P(0.5f, 0.0f), 2.0f * scale, inkCol);
        drawList->AddCircleFilled(P(0.5f, 0.0f), 0.9f * scale, IM_COL32(255, 255, 255, 255));
        break;
    }
    }

    // 6. Trace Line Preview (visualize continuous, dashed, or dotted stroke under/from tip)
    {
        float traceY = (size.y < 45.0f)
            ? (pos.y + size.y - 4.5f + elevY)
            : (pos.y + size.y - 7.0f + elevY);
        float traceX1 = pos.x + (size.y < 45.0f ? 4.0f : 6.0f);
        float traceX2 = pos.x + size.x - (size.y < 45.0f ? 4.0f : 6.0f);
        float traceThick = std::clamp(preset.thicknessMm * (size.y < 45.0f ? 1.2f : 1.6f), 1.6f, 4.2f);

        // Visual ink lead-in connecting the nib tip to the trace line in full mode
        if (size.y >= 45.0f && preset.type != PenType::LaserPointer) {
            drawList->AddLine(
                ImVec2(tipOrigin.x, tipOrigin.y),
                ImVec2(traceX1 + 2.0f, traceY),
                inkCol,
                std::min(traceThick, 2.0f)
            );
        }

        if (preset.strokePattern == StrokePattern::Solid) {
            // Continuous solid stroke
            drawList->AddLine(ImVec2(traceX1, traceY), ImVec2(traceX2, traceY), inkCol, traceThick);
        } else if (preset.strokePattern == StrokePattern::Dashed) {
            // Discrete dashes with visible gaps
            float totalW = traceX2 - traceX1;
            int numDashes = (size.y < 45.0f) ? 2 : 3;
            float gapW = (size.y < 45.0f) ? 4.0f : 5.0f;
            float dashW = (totalW - (numDashes - 1) * gapW) / numDashes;
            for (int d = 0; d < numDashes; d++) {
                float dx1 = traceX1 + d * (dashW + gapW);
                float dx2 = dx1 + dashW;
                drawList->AddLine(ImVec2(dx1, traceY), ImVec2(dx2, traceY), inkCol, traceThick);
            }
        } else if (preset.strokePattern == StrokePattern::Dotted) {
            // Crisp circular dots
            int numDots = (size.y < 45.0f) ? 4 : 5;
            float dotR = std::clamp(traceThick * 0.55f, 1.4f, 2.8f);
            float span = traceX2 - traceX1;
            for (int d = 0; d < numDots; d++) {
                float dotX = traceX1 + d * (span / (numDots - 1));
                drawList->AddCircleFilled(ImVec2(dotX, traceY), dotR, inkCol);
            }
        } else {
            // Textured / pencil stippled trace
            int numDots = (size.y < 45.0f) ? 6 : 8;
            float dotR = std::clamp(traceThick * 0.45f, 1.2f, 2.2f);
            float span = traceX2 - traceX1;
            for (int d = 0; d < numDots; d++) {
                float dotX = traceX1 + d * (span / (numDots - 1));
                float jitterY = ((d % 2 == 0) ? -0.7f : 0.7f);
                drawList->AddCircleFilled(ImVec2(dotX, traceY + jitterY), dotR, inkCol);
            }
        }
    }

    // 7. Stroke Thickness Label (e.g. "0.5") in top-left area
    if (size.y >= 45.0f) {
        char szBuf[16];
        snprintf(szBuf, sizeof(szBuf), "%.1f", preset.thicknessMm);
        bool isDark = (theme.colorBg.x < 0.5f);
        ImU32 thickCol = isDark ? IM_COL32(230, 235, 245, 240) : IM_COL32(20, 22, 28, 255);
        drawList->AddText(ImVec2(pos.x + 6.0f, pos.y + 5.0f), thickCol, szBuf);
    }

    // 8. Rich Tooltip on Hover
    if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(preset.name.c_str());
        ImGui::Separator();
        ImGui::Text("Thickness: %.1f mm", preset.thicknessMm);
        ImGui::Text("Opacity: %.0f%%", preset.opacity * 100.0f);
        const char* patternName = (preset.strokePattern == StrokePattern::Dotted) ? "Dotted" :
                                  (preset.strokePattern == StrokePattern::Dashed) ? "Dashed" :
                                  (preset.strokePattern == StrokePattern::TexturedPencil) ? "Textured" : "Continuous";
        ImGui::Text("Line Style: %s", patternName);
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
        ImGui::TextUnformatted("Left-click: Select tool\nRight-click or Double-click: Customize");
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
    }

    // 9. Event Handling
    if (clicked && onSelect) {
        onSelect();
    }
    if (doubleClicked || rightClicked) {
        ImGui::OpenPopup(popupId.c_str());
    }

    // 10. Customization Popover Dialog
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.colorPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

    if (ImGui::BeginPopup(popupId.c_str())) {
        SettingsPopoverBuilder popover(theme);
        popover.AddTitle((std::string("Customize ") + preset.name).c_str());

        bool changed = false;

        // Thickness Presets: 0.3, 0.5, 0.7, 1.2, 2.0, 3.0 mm (with size-proportional dots on each button)
        ImGui::TextUnformatted("Thickness Presets:");
        ImGui::Spacing();
        static const float s_ThicknessPresets[] = { 0.3f, 0.5f, 0.7f, 1.2f, 2.0f, 3.0f };
        for (int t = 0; t < 6; t++) {
            if (t > 0) ImGui::SameLine(0, 5.0f);
            char szT[16];
            snprintf(szT, sizeof(szT), "%.1f", s_ThicknessPresets[t]);
            bool isCurrent = std::abs(preset.thicknessMm - s_ThicknessPresets[t]) < 0.05f;

            ImVec2 btnPos = ImGui::GetCursorScreenPos();
            float btnW = 34.0f;
            float btnH = 40.0f;

            if (isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorItemSelected);
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorItemSelectedText);
                ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPanel);
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
                ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);
            }

            std::string btnId = std::string("##thick_btn_") + std::to_string(t);
            if (ImGui::Button(btnId.c_str(), ImVec2(btnW, btnH))) {
                preset.thicknessMm = s_ThicknessPresets[t];
                changed = true;
            }

            // Draw size indicator dot on the preset button (top portion)
            ImDrawList* popDrawList = ImGui::GetWindowDrawList();
            bool isDark = (theme.colorBg.x < 0.5f);
            ImU32 dotCol = isDark ? IM_COL32(245, 248, 255, 230) : IM_COL32(22, 24, 28, 230);
            float dotRadius = std::clamp(s_ThicknessPresets[t] * 1.6f + 0.8f, 1.6f, 6.0f);
            float dotX = btnPos.x + btnW * 0.5f;
            float dotY = btnPos.y + 11.0f;
            popDrawList->AddCircleFilled(ImVec2(dotX, dotY), dotRadius, dotCol);

            // Draw thickness label text on the button (bottom portion)
            ImVec2 txtSz = ImGui::CalcTextSize(szT);
            float txtX = btnPos.x + (btnW - txtSz.x) * 0.5f;
            float txtY = btnPos.y + btnH - txtSz.y - 3.0f;
            ImU32 txtCol = isCurrent ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                     : ImGui::ColorConvertFloat4ToU32(theme.colorText);
            popDrawList->AddText(ImVec2(txtX, txtY), txtCol, szT);

            ImGui::PopStyleColor(3);
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Color Palette:");
        ImGui::Spacing();

        // 16-Color Quick Selection Palette Grid
        static const ImVec4 s_QuickColors[] = {
            ImVec4(0.09f, 0.10f, 0.13f, 1.0f), // Black
            ImVec4(0.35f, 0.38f, 0.45f, 1.0f), // Charcoal
            ImVec4(0.10f, 0.35f, 0.86f, 1.0f), // Royal Blue
            ImVec4(0.06f, 0.58f, 0.88f, 1.0f), // Sky Blue
            ImVec4(0.06f, 0.65f, 0.45f, 1.0f), // Emerald Green
            ImVec4(0.12f, 0.50f, 0.22f, 1.0f), // Forest Green
            ImVec4(1.00f, 0.88f, 0.00f, 1.0f), // Yellow
            ImVec4(0.96f, 0.55f, 0.08f, 1.0f), // Orange
            ImVec4(0.88f, 0.15f, 0.15f, 1.0f), // Red
            ImVec4(0.92f, 0.22f, 0.55f, 1.0f), // Pink
            ImVec4(0.55f, 0.25f, 0.88f, 1.0f), // Purple
            ImVec4(0.38f, 0.18f, 0.65f, 1.0f), // Indigo
            ImVec4(0.60f, 0.40f, 0.25f, 1.0f), // Brown
            ImVec4(0.00f, 0.75f, 0.75f, 1.0f), // Cyan
            ImVec4(1.00f, 0.45f, 0.75f, 1.0f), // Neon Pink
            ImVec4(0.85f, 0.87f, 0.92f, 1.0f)  // Off White
        };

        for (int i = 0; i < 16; i++) {
            if (i > 0 && (i % 8) != 0) ImGui::SameLine(0, 6.0f);
            ImGui::PushID(i);
            if (ToolbarControls::RenderCircleButton("##swatch", s_QuickColors[i], nullptr, theme, false, 24.0f)) {
                preset.color = s_QuickColors[i];
                changed = true;
            }
            ImGui::PopID();
        }

        // Line Style Selection in Popover
        ImGui::Spacing();
        ImGui::TextUnformatted("Line Style:");
        ImGui::Spacing();

        struct PatternOption {
            const char* name;
            StrokePattern pattern;
        };
        const PatternOption s_Patterns[] = {
            { "Continuous", StrokePattern::Solid },
            { "Dashed",     StrokePattern::Dashed },
            { "Dotted",     StrokePattern::Dotted }
        };

        for (int p = 0; p < 3; p++) {
            if (p > 0) ImGui::SameLine(0, 6.0f);
            bool isCurrent = (preset.strokePattern == s_Patterns[p].pattern);
            if (isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorItemSelected);
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorItemSelectedText);
                ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPanel);
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
                ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);
            }
            ImVec2 styleBtnPos = ImGui::GetCursorScreenPos();
            float styleBtnW = 72.0f;
            float styleBtnH = 34.0f;
            std::string btnId = std::string("##style_btn_") + std::to_string(p);
            if (ImGui::Button(btnId.c_str(), ImVec2(styleBtnW, styleBtnH))) {
                preset.strokePattern = s_Patterns[p].pattern;
                changed = true;
            }
            // Draw line style graphic preview on top and label text at bottom
            ImDrawList* popDrawList = ImGui::GetWindowDrawList();
            ImU32 prevCol = isCurrent ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                      : ImGui::ColorConvertFloat4ToU32(preset.color);
            float lineY = styleBtnPos.y + 10.0f;
            float lineX1 = styleBtnPos.x + 8.0f;
            float lineX2 = styleBtnPos.x + styleBtnW - 8.0f;
            if (s_Patterns[p].pattern == StrokePattern::Solid) {
                popDrawList->AddLine(ImVec2(lineX1, lineY), ImVec2(lineX2, lineY), prevCol, 2.5f);
            } else if (s_Patterns[p].pattern == StrokePattern::Dashed) {
                float segW = 12.0f, gW = 6.0f;
                popDrawList->AddLine(ImVec2(lineX1, lineY), ImVec2(lineX1 + segW, lineY), prevCol, 2.5f);
                popDrawList->AddLine(ImVec2(lineX1 + segW + gW, lineY), ImVec2(lineX2, lineY), prevCol, 2.5f);
            } else {
                float dotR = 2.0f;
                float span = lineX2 - lineX1;
                for (int d = 0; d <= 4; d++) {
                    popDrawList->AddCircleFilled(ImVec2(lineX1 + d * (span / 4.0f), lineY), dotR, prevCol);
                }
            }
            ImVec2 txtSz = ImGui::CalcTextSize(s_Patterns[p].name);
            float txtX = styleBtnPos.x + (styleBtnW - txtSz.x) * 0.5f;
            float txtY = styleBtnPos.y + styleBtnH - txtSz.y - 3.0f;
            ImU32 txtCol = isCurrent ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                     : ImGui::ColorConvertFloat4ToU32(theme.colorText);
            popDrawList->AddText(ImVec2(txtX, txtY), txtCol, s_Patterns[p].name);
            ImGui::PopStyleColor(3);
        }

        if (changed && onCustomChange) {
            onCustomChange(preset);
        }

        // Reorder buttons (Move Left / Move Right)
        if (totalPresets > 1 && onReorder && presetIndex >= 0) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextUnformatted("Reorder Position:");
            ImGui::Spacing();

            if (presetIndex > 0) {
                if (ImGui::Button("< Move Left", ImVec2(106.0f, 26.0f))) {
                    onReorder(presetIndex, presetIndex - 1);
                    ImGui::CloseCurrentPopup();
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("< Move Left", ImVec2(106.0f, 26.0f));
                ImGui::EndDisabled();
            }

            ImGui::SameLine(0, 8.0f);

            if (presetIndex < totalPresets - 1) {
                if (ImGui::Button("Move Right >", ImVec2(106.0f, 26.0f))) {
                    onReorder(presetIndex, presetIndex + 1);
                    ImGui::CloseCurrentPopup();
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("Move Right >", ImVec2(106.0f, 26.0f));
                ImGui::EndDisabled();
            }
        }

        // Every pen is deletable by user (disabled only if 1 pen remains)
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (totalPresets <= 1) {
            ImGui::BeginDisabled();
            ImGui::Button("Delete Tool", ImVec2(220.0f, 28.0f));
            ImGui::EndDisabled();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.16f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.22f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button("Delete Tool", ImVec2(220.0f, 28.0f))) {
                if (onDelete) onDelete(preset.id);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(4);
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();

    return clicked;
}

void ToolbarControls::RenderZoomCompound(
    const char* strId,
    const ThemeManager& theme,
    std::function<void()> onZoomIn,
    std::function<void()> onZoomOut,
    std::function<void()> onZoomReset,
    std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
    const char* zoomPercentLabel,
    ImVec2 totalSize
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float rounding = theme.frameRounding;

    float zoomColW = 32.0f;
    float chevronW = 20.0f;
    float actionW = totalSize.x - zoomColW - chevronW;
    float halfH = std::floor(totalSize.y * 0.5f);

    std::string popupId = std::string("##zoom_popup_") + strId;
    ImGui::PushID(strId);

    // Interactive button regions
    ImGui::BeginGroup();
    // Left group: Zoom In (+) and Zoom Out (-)
    ImGui::BeginGroup();
    bool inClicked = ImGui::InvisibleButton("##z_in", ImVec2(zoomColW, halfH));
    bool inHovered = ImGui::IsItemHovered();
    bool inActive = ImGui::IsItemActive();

    bool outClicked = ImGui::InvisibleButton("##z_out", ImVec2(zoomColW, totalSize.y - halfH));
    bool outHovered = ImGui::IsItemHovered();
    bool outActive = ImGui::IsItemActive();
    ImGui::EndGroup();

    // Right group: 100% Home and Chevron
    ImGui::SameLine(0, 0);
    bool actClicked = ImGui::InvisibleButton("##z_act", ImVec2(actionW, totalSize.y));
    bool actHovered = ImGui::IsItemHovered();
    bool actActive = ImGui::IsItemActive();

    ImGui::SameLine(0, 0);
    bool chevClicked = ImGui::InvisibleButton("##z_chev", ImVec2(chevronW, totalSize.y));
    bool chevHovered = ImGui::IsItemHovered();
    bool chevActive = ImGui::IsItemActive();

    ImGui::EndGroup();

    // Background & Tactile Outline Rendering
    // 1. Zoom In:
    ImVec2 inMin = pos;
    ImVec2 inMax = ImVec2(pos.x + zoomColW, pos.y + halfH);
    if (inActive) {
        drawList->AddRectFilled(inMin, inMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), rounding, ImDrawFlags_RoundCornersTopLeft);
        drawList->AddRect(inMin, inMax, GetPressedOutlineColor(theme), rounding, ImDrawFlags_RoundCornersTopLeft, 1.8f);
    } else if (inHovered) {
        drawList->AddRectFilled(inMin, inMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), rounding, ImDrawFlags_RoundCornersTopLeft);
    }

    // 2. Zoom Out:
    ImVec2 outMin = ImVec2(pos.x, pos.y + halfH);
    ImVec2 outMax = ImVec2(pos.x + zoomColW, pos.y + totalSize.y);
    if (outActive) {
        drawList->AddRectFilled(outMin, outMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), rounding, ImDrawFlags_RoundCornersBottomLeft);
        drawList->AddRect(outMin, outMax, GetPressedOutlineColor(theme), rounding, ImDrawFlags_RoundCornersBottomLeft, 1.8f);
    } else if (outHovered) {
        drawList->AddRectFilled(outMin, outMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), rounding, ImDrawFlags_RoundCornersBottomLeft);
    }

    // 3. Action (100% Home):
    ImVec2 actMin = ImVec2(pos.x + zoomColW, pos.y);
    ImVec2 actMax = ImVec2(pos.x + zoomColW + actionW, pos.y + totalSize.y);
    if (actActive) {
        drawList->AddRectFilled(actMin, actMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover));
        drawList->AddRect(actMin, actMax, GetPressedOutlineColor(theme), 0.0f, 0, 1.8f);
    } else if (actHovered) {
        drawList->AddRectFilled(actMin, actMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover));
    }

    // 4. Chevron:
    ImVec2 chevMin = ImVec2(pos.x + zoomColW + actionW, pos.y);
    ImVec2 chevMax = ImVec2(pos.x + totalSize.x, pos.y + totalSize.y);
    if (chevActive) {
        drawList->AddRectFilled(chevMin, chevMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), rounding, ImDrawFlags_RoundCornersRight);
        drawList->AddRect(chevMin, chevMax, GetPressedOutlineColor(theme), rounding, ImDrawFlags_RoundCornersRight, 1.8f);
    } else if (chevHovered) {
        drawList->AddRectFilled(chevMin, chevMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), rounding, ImDrawFlags_RoundCornersRight);
    }

    // Dividers:
    ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorBorder);
    drawList->AddLine(ImVec2(pos.x, pos.y + halfH), ImVec2(pos.x + zoomColW, pos.y + halfH), borderCol, 1.0f);
    drawList->AddLine(ImVec2(pos.x + zoomColW, pos.y), ImVec2(pos.x + zoomColW, pos.y + totalSize.y), borderCol, 1.0f);
    drawList->AddLine(ImVec2(pos.x + zoomColW + actionW, pos.y + 6.0f), ImVec2(pos.x + zoomColW + actionW, pos.y + totalSize.y - 6.0f), borderCol, 1.0f);

    // Outer frame border around the unified compound button
    drawList->AddRect(pos, ImVec2(pos.x + totalSize.x, pos.y + totalSize.y), borderCol, rounding, 0, 1.0f);

    // Text & Glyphs:
    // Zoom In (+)
    const char* plusStr = "+";
    ImVec2 plusSz = ImGui::CalcTextSize(plusStr);
    drawList->AddText(ImVec2(pos.x + (zoomColW - plusSz.x) * 0.5f, pos.y + (halfH - plusSz.y) * 0.5f),
        ImGui::ColorConvertFloat4ToU32(inHovered ? theme.colorText : theme.colorItemText), plusStr);

    // Zoom Out (-)
    const char* minusStr = "-";
    ImVec2 minusSz = ImGui::CalcTextSize(minusStr);
    drawList->AddText(ImVec2(pos.x + (zoomColW - minusSz.x) * 0.5f, pos.y + halfH + (totalSize.y - halfH - minusSz.y) * 0.5f),
        ImGui::ColorConvertFloat4ToU32(outHovered ? theme.colorText : theme.colorItemText), minusStr);

    // 100% Action Zone
    const char* zLabel = (zoomPercentLabel && zoomPercentLabel[0] != '\0') ? zoomPercentLabel : "100%";
    ImVec2 zSz = ImGui::CalcTextSize(zLabel);
    const char* subLabel = "Home";
    ImVec2 subSz = ImGui::CalcTextSize(subLabel);

    float zY = pos.y + 10.0f;
    float subY = pos.y + 31.0f;
    drawList->AddText(ImVec2(pos.x + zoomColW + (actionW - zSz.x) * 0.5f, zY),
        ImGui::ColorConvertFloat4ToU32(actHovered ? theme.colorText : theme.colorItemSelectedText), zLabel);
    drawList->AddText(ImVec2(pos.x + zoomColW + (actionW - subSz.x) * 0.5f, subY),
        ImGui::ColorConvertFloat4ToU32(actHovered ? theme.colorText : theme.colorTextMuted), subLabel);

    // Chevron Zone (downward triangle)
    float chevCenterX = pos.x + zoomColW + actionW + chevronW * 0.5f;
    float chevCenterY = pos.y + totalSize.y * 0.5f;
    ImVec2 p1(chevCenterX - 3.5f, chevCenterY - 2.0f);
    ImVec2 p2(chevCenterX + 3.5f, chevCenterY - 2.0f);
    ImVec2 p3(chevCenterX, chevCenterY + 2.5f);
    drawList->AddTriangleFilled(p1, p2, p3,
        ImGui::ColorConvertFloat4ToU32(chevHovered ? theme.colorText : theme.colorTextMuted));

    // Tooltips
    if (inHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Zoom In (Ctrl + Plus): Increase canvas zoom by 25%");
        ImGui::EndTooltip();
    }
    if (outHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Zoom Out (Ctrl + Minus): Decrease canvas zoom by 20%");
        ImGui::EndTooltip();
    }
    if (actHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Reset Zoom to 100% / Home (Click chevron for presets)");
        ImGui::EndTooltip();
    }
    if (chevHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Zoom Presets (Click for preset zoom levels)");
        ImGui::EndTooltip();
    }

    // Callbacks
    if (inClicked && onZoomIn) onZoomIn();
    if (outClicked && onZoomOut) onZoomOut();
    if (actClicked && onZoomReset) onZoomReset();
    if (chevClicked) {
        ImGui::OpenPopup(popupId.c_str());
    }

    // Submenu Flyout
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.colorPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

    if (ImGui::BeginPopup(popupId.c_str())) {
        if (onBuildMenu) {
            FlyoutMenuBuilder menuBuilder(theme);
            onBuildMenu(menuBuilder);
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

// ============================================================================
// 4. TOOLBAR SUBSECTION BUILDER IMPLEMENTATION
// ============================================================================

ToolbarSectionBuilder::ToolbarSectionBuilder(const char* id, const char* title, const ThemeManager& t, bool mini)
    : sectionId(id), sectionTitle(title), theme(t), isMiniMode(mini)
{
    ImGui::BeginGroup();
    ImGui::PushID(sectionId);
}

void ToolbarSectionBuilder::FlowNextItem(float spacing) {
    if (inStack) {
        stackItemCount++;
    } else if (hasItems) {
        ImGui::SameLine(0, spacing);
    }
    hasItems = true;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::BeginStack() {
    FlowNextItem();
    if (!isMiniMode) {
        ImGui::BeginGroup();
        inStack = true;
        stackItemCount = 0;
    }
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::EndStack() {
    if (inStack) {
        inStack = false;
        ImGui::EndGroup();
    }
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddLargeButton(
    const char* id,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    bool isActive,
    std::function<void()> onClick,
    bool flipH,
    ImVec2 size
) {
    FlowNextItem();
    ImVec2 actualSize = isMiniMode ? ImVec2(size.x > 42.0f ? 42.0f : size.x, 32.0f) : size;
    if (ToolbarControls::RenderLargeButton(id, iconTex, label, tooltip, theme, isActive, flipH, actualSize)) {
        if (onClick) onClick();
    }
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddSmallButton(
    const char* id,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    bool isActive,
    std::function<void()> onClick,
    bool flipH,
    ImVec2 size
) {
    FlowNextItem(2.0f);
    ImVec2 actualSize = isMiniMode ? ImVec2(size.x, 32.0f) : size;
    if (inStack) {
        stackMaxW = std::max(stackMaxW, actualSize.x);
    }
    if (ToolbarControls::RenderSmallButton(id, iconTex, label, tooltip, theme, isActive, flipH, actualSize)) {
        if (onClick) onClick();
    }
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddSplitButton(
    const char* id,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    bool isActive,
    std::function<void()> onAction,
    std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
    bool flipH,
    ImVec2 size
) {
    FlowNextItem();
    ImVec2 actualSize = isMiniMode ? ImVec2(size.x > 46.0f ? 46.0f : size.x, 32.0f) : size;
    ToolbarControls::RenderSplitButton(id, iconTex, label, tooltip, theme, isActive, onAction, onBuildMenu, flipH, actualSize);
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddPillButton(
    const char* id,
    GLuint iconTex,
    const char* label,
    const char* tooltip,
    bool isActive,
    ImVec4 accentColor,
    std::function<void()> onClick,
    std::function<void(SettingsPopoverBuilder&)> onSettings,
    ImVec2 size
) {
    FlowNextItem();
    ImVec2 actualSize = isMiniMode ? ImVec2(size.x > 44.0f ? 44.0f : size.x, 32.0f) : size;
    ToolbarControls::RenderPillButton(id, iconTex, label, tooltip, theme, isActive, accentColor, onClick, onSettings, actualSize);
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddCircleButton(
    const char* id,
    ImVec4 fillColor,
    const char* tooltip,
    bool isActive,
    std::function<void()> onClick,
    float diameter
) {
    FlowNextItem(isMiniMode ? 3.0f : 4.0f);
    float actualDiameter = isMiniMode ? 26.0f : diameter;
    if (ToolbarControls::RenderCircleButton(id, fillColor, tooltip, theme, isActive, actualDiameter)) {
        if (onClick) onClick();
    }
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddPenNibControl(
    const char* id,
    PenPreset& preset,
    bool isActive,
    std::function<void()> onSelect,
    std::function<void(PenPreset&)> onCustomChange,
    std::function<void(const std::string&)> onDelete,
    ImVec2 size,
    int presetIndex,
    int totalPresets,
    std::function<void(int fromIdx, int toIdx)> onReorder
) {
    FlowNextItem(isMiniMode ? 2.0f : 4.0f);
    ImVec2 actualSize = isMiniMode ? ImVec2(size.x > 32.0f ? 32.0f : size.x, 32.0f) : size;
    ToolbarControls::RenderPenNibControl(id, preset, isActive, theme, onSelect, onCustomChange, onDelete, actualSize,
        presetIndex, totalPresets, onReorder);
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddZoomCompound(
    const char* id,
    std::function<void()> onZoomIn,
    std::function<void()> onZoomOut,
    std::function<void()> onZoomReset,
    std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
    const char* zoomPercentLabel,
    ImVec2 size
) {
    FlowNextItem();
    ImVec2 actualSize = isMiniMode ? ImVec2(size.x, 32.0f) : size;
    ToolbarControls::RenderZoomCompound(id, theme, onZoomIn, onZoomOut, onZoomReset, onBuildMenu, zoomPercentLabel, actualSize);
    return *this;
}

ToolbarSectionBuilder& ToolbarSectionBuilder::AddWidget(const std::function<void()>& widgetFunc, float spacing) {
    FlowNextItem(spacing);
    if (widgetFunc) widgetFunc();
    return *this;
}

void ToolbarSectionBuilder::Render(float sectionSpacing) {
    if (inStack) EndStack();

    ImGui::PopID();
    ImGui::EndGroup(); // Close the group FIRST so GetItemRect encompasses ALL items in the section!

    ImVec2 groupMin = ImGui::GetItemRectMin();
    ImVec2 groupMax = ImGui::GetItemRectMax();
    float contentW = groupMax.x - groupMin.x;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 1. Centered Category Title at Bottom (Hidden in Mini Mode)
    // Measures title and centers it across the ENTIRE button stretch section [groupMin.x, groupMax.x]
    float titleOverhang = 0.0f;
    if (!isMiniMode && sectionTitle && sectionTitle[0] != '\0') {
        ImGui::PushFont(FolioTheme::FontRibbonSection ? FolioTheme::FontRibbonSection : FolioTheme::FontRegular);
        ImVec2 titleSize = ImGui::CalcTextSize(sectionTitle);
        if (titleSize.x + 8.0f > contentW) {
            titleOverhang = ((titleSize.x + 8.0f) - contentW) * 0.5f;
        }
        float titleX = groupMin.x + (contentW - titleSize.x) * 0.5f;
        float titleY = groupMin.y + 64.0f;
        drawList->AddText(ImVec2(titleX, titleY),
            ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), sectionTitle);
        ImGui::PopFont();
    }

    // 2. Vertical Section Separator Line
    // Placed midway between this section's buttons and the next section
    float gap = sectionSpacing > 0.0f ? sectionSpacing : 18.0f;
    float dividerX = groupMax.x + titleOverhang + (gap * 0.5f);
    float divTop = groupMin.y + (isMiniMode ? 2.0f : 4.0f);
    float divBottom = groupMin.y + (isMiniMode ? 30.0f : 78.0f);
    drawList->AddLine(
        ImVec2(dividerX, divTop),
        ImVec2(dividerX, divBottom),
        ImGui::ColorConvertFloat4ToU32(theme.colorBorder),
        1.0f
    );

    // Advance cursor to the next section with balanced spacing
    ImGui::SameLine(0, gap + titleOverhang * 2.0f);
}

} // namespace FolioUI
