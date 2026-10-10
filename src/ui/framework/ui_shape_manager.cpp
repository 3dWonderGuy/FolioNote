/**
 * =========================================================================================
 * @file ui/framework/ui_shape_manager.cpp
 * @brief Implementation of the UI Shape & Surface Rendering Primitives
 * =========================================================================================
 */

#include "ui/framework/ui_shape_manager.hpp"
#include <algorithm>
#include <cmath>

namespace Folio::UI {

void UIShapeManager::DrawShadow(
    ImDrawList* drawList,
    ImVec2 pMin,
    ImVec2 pMax,
    float elevation,
    float rounding
) {
    if (!drawList || elevation <= 0.0f) return;

    // Multi-pass layered ambient occlusion shadow:
    // Pass 1: Direct contact shadow (tight, slightly darker)
    float off1 = elevation * 0.35f;
    float spread1 = 1.5f;
    ImVec2 min1(pMin.x - spread1, pMin.y + off1 - spread1);
    ImVec2 max1(pMax.x + spread1, pMax.y + off1 + spread1);
    drawList->AddRectFilled(min1, max1, IM_COL32(0, 0, 0, static_cast<int>(std::min(45.0f, elevation * 3.5f))), rounding + 1.0f);

    // Pass 2: Soft ambient field shadow (diffuse, soft falloff)
    float off2 = elevation * 0.75f;
    float spread2 = elevation * 0.85f;
    ImVec2 min2(pMin.x - spread2, pMin.y + off2 - spread2);
    ImVec2 max2(pMax.x + spread2, pMax.y + off2 + spread2);
    drawList->AddRectFilled(min2, max2, IM_COL32(0, 0, 0, static_cast<int>(std::min(30.0f, elevation * 2.2f))), rounding + spread2 * 0.6f);
}

void UIShapeManager::DrawAcrylicPanel(
    ImDrawList* drawList,
    ImVec2 pMin,
    ImVec2 pMax,
    ImU32 baseFill,
    ImU32 borderCol,
    float rounding,
    float elevation
) {
    if (!drawList) return;

    // 1. Cast shadow if elevated
    if (elevation > 0.0f) {
        DrawShadow(drawList, pMin, pMax, elevation, rounding);
    }

    // 2. Base acrylic fill with subtle top-to-bottom luminance gradient
    // Top is slightly highlighted (+10% luminance), bottom is base
    uint32_t r = (baseFill >> IM_COL32_R_SHIFT) & 0xFF;
    uint32_t g = (baseFill >> IM_COL32_G_SHIFT) & 0xFF;
    uint32_t b = (baseFill >> IM_COL32_B_SHIFT) & 0xFF;
    uint32_t a = (baseFill >> IM_COL32_A_SHIFT) & 0xFF;

    uint32_t topR = std::min(255u, r + 15u);
    uint32_t topG = std::min(255u, g + 15u);
    uint32_t topB = std::min(255u, b + 15u);
    ImU32 topCol = IM_COL32(topR, topG, topB, a);

    drawList->AddRectFilledMultiColor(pMin, pMax, topCol, topCol, baseFill, baseFill);

    // 3. Perimeter border
    if ((borderCol & IM_COL32_A_MASK) != 0) {
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.0f);
    }

    // 4. Specular top-edge highlight (1px inset highlight simulating direct light)
    if (rounding > 0.0f) {
        float inset = std::min(rounding, 8.0f);
        ImVec2 specStart(pMin.x + inset, pMin.y + 1.0f);
        ImVec2 specEnd(pMax.x - inset, pMin.y + 1.0f);
        drawList->AddLine(specStart, specEnd, IM_COL32(255, 255, 255, 38), 1.0f);
    }
}

void UIShapeManager::DrawInteractiveCard(
    ImDrawList* drawList,
    ImVec2 pMin,
    ImVec2 pMax,
    ImU32 fill,
    ImU32 hoverFill,
    ImU32 border,
    ImU32 hoverBorder,
    float hoverAlpha,
    float rounding
) {
    if (!drawList) return;

    ImU32 currentFill = ColorUtils::Lerp(fill, hoverFill, hoverAlpha);
    ImU32 currentBorder = ColorUtils::Lerp(border, hoverBorder, hoverAlpha);

    // Subtle elevation increase on hover
    float currentElevation = 1.0f + 3.0f * hoverAlpha;
    DrawShadow(drawList, pMin, pMax, currentElevation, rounding);

    drawList->AddRectFilled(pMin, pMax, currentFill, rounding);
    drawList->AddRect(pMin, pMax, currentBorder, rounding, 0, 1.0f);
}

void UIShapeManager::DrawPillBadge(
    ImDrawList* drawList,
    ImVec2 center,
    const char* text,
    ImU32 bgCol,
    ImU32 textCol,
    ImFont* font
) {
    if (!drawList || !text || text[0] == '\0') return;

    if (font) ImGui::PushFont(font);
    ImVec2 textSize = ImGui::CalcTextSize(text);

    float padX = 7.0f;
    float padY = 2.5f;
    float halfW = (textSize.x + padX * 2.0f) * 0.5f;
    float halfH = (textSize.y + padY * 2.0f) * 0.5f;

    ImVec2 bMin(center.x - halfW, center.y - halfH);
    ImVec2 bMax(center.x + halfW, center.y + halfH);

    drawList->AddRectFilled(bMin, bMax, bgCol, halfH);
    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), textCol, text);

    if (font) ImGui::PopFont();
}

void UIShapeManager::DrawGlow(
    ImDrawList* drawList,
    ImVec2 center,
    float radius,
    ImU32 glowColor
) {
    if (!drawList || radius <= 0.0f) return;

    uint32_t r = (glowColor >> IM_COL32_R_SHIFT) & 0xFF;
    uint32_t g = (glowColor >> IM_COL32_G_SHIFT) & 0xFF;
    uint32_t b = (glowColor >> IM_COL32_B_SHIFT) & 0xFF;
    uint32_t baseA = (glowColor >> IM_COL32_A_SHIFT) & 0xFF;

    // 3 concentric alpha rings to simulate radial falloff
    const int rings = 3;
    for (int i = 0; i < rings; ++i) {
        float factor = static_cast<float>(i + 1) / static_cast<float>(rings);
        float currentRadius = radius * factor;
        float alphaFraction = (1.0f - static_cast<float>(i) / rings);
        int a = static_cast<int>(static_cast<float>(baseA) * 0.35f * alphaFraction);
        drawList->AddCircleFilled(center, currentRadius, IM_COL32(r, g, b, a), 24);
    }
}

} // namespace Folio::UI
