/**
 * =========================================================================================
 * @file ui/framework/ui_builder.cpp
 * @brief Implementation of the Fluent Widget Factory & Button Generator
 * =========================================================================================
 */

#include "ui/framework/ui_builder.hpp"
#include "app/theme_manager.hpp"
#include "imgui_internal.h"
#include <cmath>

namespace Folio::UI {

// =========================================================================================
// ButtonBuilder Implementation
// =========================================================================================

ButtonBuilder::ButtonBuilder(const std::string& label) : label_(label) {}

ButtonBuilder& ButtonBuilder::Variant(UIButtonVariant variant) {
    variant_ = variant;
    return *this;
}

ButtonBuilder& ButtonBuilder::Size(UIButtonSize size) {
    size_ = size;
    return *this;
}

ButtonBuilder& ButtonBuilder::Icon(const std::string& iconText) {
    iconText_ = iconText;
    return *this;
}

ButtonBuilder& ButtonBuilder::Badge(const std::string& badgeText) {
    badgeText_ = badgeText;
    return *this;
}

ButtonBuilder& ButtonBuilder::Tooltip(const std::string& tooltip) {
    tooltip_ = tooltip;
    return *this;
}

ButtonBuilder& ButtonBuilder::Width(float width) {
    customWidth_ = width;
    return *this;
}

ButtonBuilder& ButtonBuilder::Disabled(bool disabled) {
    isDisabled_ = disabled;
    return *this;
}

ButtonBuilder& ButtonBuilder::Selected(bool selected) {
    isSelected_ = selected;
    return *this;
}

bool ButtonBuilder::Render() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label_.c_str());

    // 1. Height and padding based on Size tier
    float height = 34.0f;
    float padX = Spacing::md;
    float fontSize = ImGui::GetFontSize();
    float rounding = Radii::sm;

    switch (size_) {
        case UIButtonSize::Small:
            height = 26.0f;
            padX = Spacing::sm;
            rounding = Radii::xs;
            break;
        case UIButtonSize::Large:
            height = 44.0f;
            padX = Spacing::lg;
            rounding = Radii::md;
            break;
        default:
            height = 34.0f;
            padX = Spacing::md;
            rounding = Radii::sm;
            break;
    }

    // 2. Measure content width
    ImVec2 labelSize = ImGui::CalcTextSize(label_.c_str(), nullptr, true);
    float contentW = labelSize.x;
    if (!iconText_.empty()) {
        contentW += ImGui::CalcTextSize(iconText_.c_str()).x + Spacing::sm;
    }
    if (!badgeText_.empty()) {
        contentW += ImGui::CalcTextSize(badgeText_.c_str()).x + Spacing::md;
    }

    float totalW = (customWidth_ > 0.0f) ? customWidth_ : (contentW + padX * 2.0f);
    ImVec2 size(totalW, height);

    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }

    // 3. Interaction evaluation
    bool hovered = false;
    bool held = false;
    bool pressed = false;
    if (!isDisabled_) {
        pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    }

    // 4. Smooth animation transition alphas
    float hoverAlpha = 0.0f;
    float activeAlpha = 0.0f;
    UIAnimationManager::Instance().GetInteractionAlphas(id, hovered || isSelected_, held, hoverAlpha, activeAlpha);

    // 5. Theme color resolution
    const auto& theme = ThemeManager::Instance();
    bool isDark = (theme.colorBg.x < 0.5f);

    ImU32 baseFill = 0;
    ImU32 hoverFill = 0;
    ImU32 baseBorder = 0;
    ImU32 hoverBorder = 0;
    ImU32 textCol = isDark ? IM_COL32(235, 238, 245, 255) : IM_COL32(25, 28, 35, 255);

    // Signature warm accent
    ImU32 accentCol = IM_COL32(242, 115, 38, 255);
    ImU32 accentHover = IM_COL32(250, 135, 60, 255);

    switch (variant_) {
        case UIButtonVariant::Primary:
            baseFill = accentCol;
            hoverFill = accentHover;
            baseBorder = IM_COL32(0, 0, 0, 0);
            hoverBorder = IM_COL32(255, 255, 255, 60);
            textCol = IM_COL32(255, 255, 255, 255);
            break;
        case UIButtonVariant::Ghost:
            baseFill = IM_COL32(0, 0, 0, 0);
            hoverFill = isDark ? IM_COL32(255, 255, 255, 22) : IM_COL32(0, 0, 0, 16);
            baseBorder = IM_COL32(0, 0, 0, 0);
            hoverBorder = isDark ? IM_COL32(255, 255, 255, 35) : IM_COL32(0, 0, 0, 25);
            break;
        case UIButtonVariant::Danger:
            baseFill = isDark ? IM_COL32(180, 45, 45, 180) : IM_COL32(220, 60, 60, 200);
            hoverFill = isDark ? IM_COL32(210, 55, 55, 240) : IM_COL32(240, 70, 70, 255);
            baseBorder = IM_COL32(255, 80, 80, 80);
            hoverBorder = IM_COL32(255, 100, 100, 160);
            textCol = IM_COL32(255, 255, 255, 255);
            break;
        case UIButtonVariant::Glassmorphic:
            baseFill = isDark ? IM_COL32(32, 36, 46, 175) : IM_COL32(255, 255, 255, 190);
            hoverFill = isDark ? IM_COL32(48, 54, 68, 215) : IM_COL32(255, 255, 255, 240);
            baseBorder = isDark ? IM_COL32(80, 88, 105, 140) : IM_COL32(200, 205, 218, 180);
            hoverBorder = isDark ? IM_COL32(120, 132, 155, 200) : IM_COL32(160, 170, 190, 220);
            break;
        case UIButtonVariant::Secondary:
        default:
            baseFill = isDark ? IM_COL32(38, 42, 53, 230) : IM_COL32(245, 247, 250, 240);
            hoverFill = isDark ? IM_COL32(50, 56, 70, 255) : IM_COL32(236, 240, 246, 255);
            baseBorder = isDark ? IM_COL32(65, 72, 88, 180) : IM_COL32(215, 220, 230, 200);
            hoverBorder = isDark ? IM_COL32(95, 105, 128, 220) : IM_COL32(175, 185, 200, 240);
            break;
    }

    if (isSelected_ && variant_ != UIButtonVariant::Primary) {
        baseBorder = accentCol;
        hoverBorder = accentHover;
    }

    if (isDisabled_) {
        textCol = ColorUtils::WithAlpha(textCol, 0.45f);
        baseFill = ColorUtils::WithAlpha(baseFill, 0.5f);
        hoverFill = baseFill;
    }

    // 6. Draw surface with subtle scale press effect
    ImDrawList* drawList = window->DrawList;
    ImVec2 bMin = bb.Min;
    ImVec2 bMax = bb.Max;

    if (activeAlpha > 0.0f) {
        float scaleInset = 1.0f * activeAlpha;
        bMin.x += scaleInset;
        bMin.y += scaleInset;
        bMax.x -= scaleInset;
        bMax.y -= scaleInset;
    }

    if (variant_ == UIButtonVariant::Glassmorphic) {
        UIShapeManager::DrawAcrylicPanel(drawList, bMin, bMax, ColorUtils::Lerp(baseFill, hoverFill, hoverAlpha),
                                         ColorUtils::Lerp(baseBorder, hoverBorder, hoverAlpha), rounding, Elevation::low);
    } else {
        UIShapeManager::DrawInteractiveCard(drawList, bMin, bMax, baseFill, hoverFill, baseBorder, hoverBorder, hoverAlpha, rounding);
    }

    // 7. Render centered text and icons
    float startX = bMin.x + (bMax.x - bMin.x - contentW) * 0.5f;
    float startY = bMin.y + (bMax.y - bMin.y - fontSize) * 0.5f;

    if (!iconText_.empty()) {
        drawList->AddText(ImVec2(startX, startY), textCol, iconText_.c_str());
        startX += ImGui::CalcTextSize(iconText_.c_str()).x + Spacing::sm;
    }

    drawList->AddText(ImVec2(startX, startY), textCol, label_.c_str());
    startX += labelSize.x;

    if (!badgeText_.empty()) {
        startX += Spacing::sm;
        ImU32 badgeBg = isDark ? IM_COL32(255, 255, 255, 28) : IM_COL32(0, 0, 0, 20);
        ImU32 badgeTextCol = isDark ? IM_COL32(200, 205, 215, 220) : IM_COL32(100, 105, 118, 220);
        UIShapeManager::DrawPillBadge(drawList, ImVec2(startX + 18.0f, bMin.y + height * 0.5f), badgeText_.c_str(), badgeBg, badgeTextCol);
    }

    // 8. Tooltip
    if (!tooltip_.empty() && hovered) {
        ImGui::SetTooltip("%s", tooltip_.c_str());
    }

    return pressed;
}

// =========================================================================================
// SwitchBuilder Implementation
// =========================================================================================

SwitchBuilder::SwitchBuilder(const std::string& label, bool* value)
    : label_(label), value_(value) {}

SwitchBuilder& SwitchBuilder::Description(const std::string& desc) {
    description_ = desc;
    return *this;
}

SwitchBuilder& SwitchBuilder::Disabled(bool disabled) {
    isDisabled_ = disabled;
    return *this;
}

bool SwitchBuilder::Render() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || !value_) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label_.c_str());

    float trackW = 42.0f;
    float trackH = 22.0f;
    float knobRadius = 8.5f;

    ImVec2 labelSize = ImGui::CalcTextSize(label_.c_str());
    float rowW = ImGui::GetContentRegionAvail().x;
    float rowH = std::max(trackH, description_.empty() ? labelSize.y : (labelSize.y + 18.0f));

    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + rowW, pos.y + rowH));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false;
    bool held = false;
    bool clicked = false;
    if (!isDisabled_) {
        clicked = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (clicked) {
            *value_ = !(*value_);
        }
    }

    // Animated knob sliding parameter [0.0 = OFF, 1.0 = ON]
    std::string animKey = "switch_" + std::to_string(id);
    float targetT = (*value_) ? 1.0f : 0.0f;
    float t = UIAnimationManager::Instance().GetFloat(animKey, targetT, 16.0f);

    const auto& theme = ThemeManager::Instance();
    bool isDark = (theme.colorBg.x < 0.5f);

    // Track colors
    ImU32 offTrack = isDark ? IM_COL32(55, 60, 75, 220) : IM_COL32(205, 212, 225, 230);
    ImU32 onTrack = IM_COL32(242, 115, 38, 255); // Folio accent orange
    ImU32 currentTrack = ColorUtils::Lerp(offTrack, onTrack, t);

    // Track bounds (anchored to the right)
    ImVec2 trackMin(bb.Max.x - trackW - Spacing::sm, bb.Min.y + (rowH - trackH) * 0.5f);
    ImVec2 trackMax(trackMin.x + trackW, trackMin.y + trackH);

    ImDrawList* drawList = window->DrawList;
    drawList->AddRectFilled(trackMin, trackMax, currentTrack, trackH * 0.5f);

    // Sliding knob position
    float knobMinX = trackMin.x + knobRadius + 2.5f;
    float knobMaxX = trackMax.x - knobRadius - 2.5f;
    float knobX = knobMinX + (knobMaxX - knobMinX) * t;
    float knobY = trackMin.y + trackH * 0.5f;

    // Knob drop shadow & circle
    drawList->AddCircleFilled(ImVec2(knobX, knobY + 1.0f), knobRadius, IM_COL32(0, 0, 0, 45), 18);
    drawList->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, IM_COL32(255, 255, 255, 255), 18);

    // Text labels (on the left)
    ImU32 titleCol = isDark ? IM_COL32(235, 238, 245, 255) : IM_COL32(25, 28, 35, 255);
    drawList->AddText(ImVec2(bb.Min.x, bb.Min.y + (description_.empty() ? (rowH - labelSize.y) * 0.5f : 0.0f)), titleCol, label_.c_str());

    if (!description_.empty()) {
        ImU32 descCol = isDark ? IM_COL32(150, 158, 175, 220) : IM_COL32(115, 122, 138, 220);
        drawList->AddText(ImVec2(bb.Min.x, bb.Min.y + labelSize.y + 2.0f), descCol, description_.c_str());
    }

    return clicked;
}

// =========================================================================================
// SegmentedSwitchBuilder Implementation
// =========================================================================================

SegmentedSwitchBuilder::SegmentedSwitchBuilder(
    const std::string& id,
    const std::vector<std::string>& options,
    int* selectedIndex
) : id_(id), options_(options), selectedIndex_(selectedIndex) {}

SegmentedSwitchBuilder& SegmentedSwitchBuilder::Width(float totalWidth) {
    customWidth_ = totalWidth;
    return *this;
}

bool SegmentedSwitchBuilder::Render() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || options_.empty() || !selectedIndex_) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID widgetId = window->GetID(id_.c_str());

    float height = 32.0f;
    float totalW = (customWidth_ > 0.0f) ? customWidth_ : ImGui::GetContentRegionAvail().x;
    float segW = totalW / static_cast<float>(options_.size());

    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + totalW, pos.y + height));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, widgetId)) return false;

    const auto& theme = ThemeManager::Instance();
    bool isDark = (theme.colorBg.x < 0.5f);

    // Background container
    ImDrawList* drawList = window->DrawList;
    ImU32 bgFill = isDark ? IM_COL32(28, 32, 42, 230) : IM_COL32(235, 238, 245, 240);
    drawList->AddRectFilled(bb.Min, bb.Max, bgFill, Radii::sm);

    // Smooth sliding highlight pill
    std::string pillKey = "seg_pill_" + id_;
    float targetPillX = bb.Min.x + static_cast<float>(*selectedIndex_) * segW;
    float pillX = UIAnimationManager::Instance().GetFloat(pillKey, targetPillX, 18.0f);

    float pad = 2.0f;
    ImVec2 pillMin(pillX + pad, bb.Min.y + pad);
    ImVec2 pillMax(pillX + segW - pad, bb.Max.y - pad);

    ImU32 pillFill = isDark ? IM_COL32(50, 56, 72, 255) : IM_COL32(255, 255, 255, 255);
    drawList->AddRectFilled(pillMin, pillMax, pillFill, Radii::xs);
    drawList->AddRect(pillMin, pillMax, IM_COL32(255, 255, 255, 20), Radii::xs);

    // Item evaluation and rendering
    bool changed = false;
    for (size_t i = 0; i < options_.size(); ++i) {
        ImVec2 segMin(bb.Min.x + static_cast<float>(i) * segW, bb.Min.y);
        ImVec2 segMax(segMin.x + segW, bb.Max.y);
        ImGuiID segId = window->GetID((id_ + "_" + std::to_string(i)).c_str());

        bool hovered = false;
        bool held = false;
        if (ImGui::ButtonBehavior(ImRect(segMin, segMax), segId, &hovered, &held)) {
            if (*selectedIndex_ != static_cast<int>(i)) {
                *selectedIndex_ = static_cast<int>(i);
                changed = true;
            }
        }

        // Text rendering
        ImVec2 optSize = ImGui::CalcTextSize(options_[i].c_str());
        ImVec2 textPos(segMin.x + (segW - optSize.x) * 0.5f, segMin.y + (height - optSize.y) * 0.5f);

        bool isSelected = (*selectedIndex_ == static_cast<int>(i));
        ImU32 textCol = isSelected
            ? (isDark ? IM_COL32(255, 255, 255, 255) : IM_COL32(15, 18, 24, 255))
            : (isDark ? IM_COL32(160, 168, 185, 210) : IM_COL32(110, 118, 132, 210));

        drawList->AddText(textPos, textCol, options_[i].c_str());
    }

    return changed;
}

// =========================================================================================
// CardBuilder Implementation
// =========================================================================================

CardBuilder::CardBuilder(const std::string& title) : title_(title) {}

CardBuilder& CardBuilder::Subtitle(const std::string& subtitle) {
    subtitle_ = subtitle;
    return *this;
}

CardBuilder& CardBuilder::HeaderIcon(const std::string& iconText) {
    headerIcon_ = iconText;
    return *this;
}

CardBuilder& CardBuilder::Elevation(float elevation) {
    elevation_ = elevation;
    return *this;
}

CardBuilder& CardBuilder::WithBody(std::function<void()> bodyFunc) {
    bodyFunc_ = std::move(bodyFunc);
    return *this;
}

void CardBuilder::Render() {
    const auto& theme = ThemeManager::Instance();
    bool isDark = (theme.colorBg.x < 0.5f);

    ImU32 bgFill = isDark ? IM_COL32(28, 32, 42, 200) : IM_COL32(255, 255, 255, 230);
    ImU32 borderCol = isDark ? IM_COL32(55, 62, 78, 160) : IM_COL32(220, 225, 235, 180);

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Radii::md);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Spacing::lg, Spacing::lg));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bgFill);
    ImGui::PushStyleColor(ImGuiCol_Border, borderCol);

    std::string childId = "##card_" + title_;
    ImGui::BeginChild(childId.c_str(), ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);

    // Header
    if (!headerIcon_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.15f, 1.0f), "%s", headerIcon_.c_str());
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(title_.c_str());

    if (!subtitle_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, isDark ? ImVec4(0.6f, 0.65f, 0.72f, 1.0f) : ImVec4(0.45f, 0.5f, 0.58f, 1.0f));
        ImGui::TextUnformatted(subtitle_.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, Spacing::xs));

    // Body content
    if (bodyFunc_) {
        bodyFunc_();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

} // namespace Folio::UI
