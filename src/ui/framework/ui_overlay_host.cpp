/**
 * =========================================================================================
 * @file ui/framework/ui_overlay_host.cpp
 * @brief Implementation of the Unified Floating Overlay, Toast, and Modal Host
 * =========================================================================================
 */

#include "ui/framework/ui_overlay_host.hpp"
#include "app/theme_manager.hpp"
#include <algorithm>

namespace Folio::UI {

UIOverlayHost& UIOverlayHost::Instance() {
    static UIOverlayHost instance;
    return instance;
}

void UIOverlayHost::ShowToast(
    UIToastType type,
    const std::string& title,
    const std::string& message,
    float durationSeconds
) {
    ToastMessage toast;
    toast.id = "toast_" + std::to_string(toasts_.size() + 1);
    toast.type = type;
    toast.title = title;
    toast.message = message;
    toast.remainingSeconds = durationSeconds;
    toast.initialSeconds = durationSeconds;
    toasts_.push_back(toast);
}

void UIOverlayHost::OpenModal(
    const std::string& id,
    const std::string& title,
    std::function<void()> renderFunc,
    float width
) {
    currentModal_.id = id;
    currentModal_.title = title;
    currentModal_.renderFunc = std::move(renderFunc);
    currentModal_.width = width;
    currentModal_.isOpen = true;

    // Reset entrance animation state
    UIAnimationManager::Instance().Snap("modal_scale_" + id, 0.92f);
    UIAnimationManager::Instance().Snap("modal_alpha_" + id, 0.0f);
}

void UIOverlayHost::CloseModal() {
    currentModal_.isOpen = false;
    currentModal_.renderFunc = nullptr;
}

void UIOverlayHost::Render(float dt) {
    const auto& theme = ThemeManager::Instance();
    bool isDark = (theme.colorBg.x < 0.5f);
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // =========================================================================
    // 1. MODAL DIALOG PRESENTATION
    // =========================================================================
    if (currentModal_.isOpen && currentModal_.renderFunc) {
        std::string scaleKey = "modal_scale_" + currentModal_.id;
        std::string alphaKey = "modal_alpha_" + currentModal_.id;

        float scale = UIAnimationManager::Instance().GetSpring(scaleKey, 1.0f, 260.0f, 22.0f);
        float alpha = UIAnimationManager::Instance().GetFloat(alphaKey, 1.0f, 14.0f);

        // Fullscreen dimmed backdrop
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        ImU32 backdropCol = IM_COL32(0, 0, 0, static_cast<int>(140.0f * alpha));
        fgDrawList->AddRectFilled(ImVec2(0, 0), displaySize, backdropCol);

        // Centered modal window calculation
        float modalW = currentModal_.width * scale;
        float modalH = 340.0f * scale; // Approximate initial height
        ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
        ImVec2 modalPos(center.x - modalW * 0.5f, center.y - modalH * 0.5f);

        ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(modalW, 0.0f), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings;

        ImU32 modalBg = isDark ? IM_COL32(28, 32, 44, 250) : IM_COL32(252, 254, 255, 250);
        ImU32 modalBorder = isDark ? IM_COL32(65, 74, 95, 220) : IM_COL32(210, 218, 230, 240);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, modalBg);
        ImGui::PushStyleColor(ImGuiCol_Border, modalBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Radii::lg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Spacing::xl, Spacing::xl));

        std::string modalWinName = "##Modal_" + currentModal_.id;
        if (ImGui::Begin(modalWinName.c_str(), nullptr, flags)) {
            // Header: Title and Close button
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.15f, 1.0f), "%s", currentModal_.title.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 36.0f);
            if (UI::Button("✕").Size(UIButtonSize::Small).Variant(UIButtonVariant::Ghost).Width(28.0f).Render()) {
                CloseModal();
            }

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, Spacing::sm));

            // Body
            currentModal_.renderFunc();

            ImGui::End();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    // =========================================================================
    // 2. TOAST NOTIFICATION STACK (Top-Right Screen Corner)
    // =========================================================================
    if (!toasts_.empty()) {
        float toastW = 320.0f;
        float toastH = 68.0f;
        float padY = 10.0f;
        float startX = displaySize.x - toastW - Spacing::lg;
        float startY = Spacing::xl + 32.0f; // Below titlebar

        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

        for (auto it = toasts_.begin(); it != toasts_.end(); ) {
            it->remainingSeconds -= dt;
            if (it->remainingSeconds <= 0.0f) {
                it = toasts_.erase(it);
                continue;
            }

            ImVec2 tMin(startX, startY);
            ImVec2 tMax(startX + toastW, startY + toastH);

            // Acrylic surface & shadow
            ImU32 bgCol = isDark ? IM_COL32(32, 36, 48, 245) : IM_COL32(250, 252, 255, 245);
            ImU32 borderCol = isDark ? IM_COL32(65, 72, 90, 200) : IM_COL32(215, 222, 232, 220);
            UIShapeManager::DrawAcrylicPanel(fgDrawList, tMin, tMax, bgCol, borderCol, Radii::md, Elevation::high);

            // Accent status indicator bar on left edge
            ImU32 accentCol = IM_COL32(50, 150, 255, 255); // Info blue
            const char* iconSymbol = "ℹ";
            if (it->type == UIToastType::Success) {
                accentCol = IM_COL32(40, 195, 120, 255); // Emerald green
                iconSymbol = "✔";
            } else if (it->type == UIToastType::Warning) {
                accentCol = IM_COL32(245, 175, 40, 255); // Amber yellow
                iconSymbol = "⚠";
            } else if (it->type == UIToastType::Error) {
                accentCol = IM_COL32(235, 60, 60, 255);  // Rose red
                iconSymbol = "✖";
            }

            fgDrawList->AddRectFilled(tMin, ImVec2(tMin.x + 4.0f, tMax.y), accentCol, Radii::md);

            // Icon symbol
            fgDrawList->AddText(ImVec2(tMin.x + 14.0f, tMin.y + 14.0f), accentCol, iconSymbol);

            // Title & message text
            ImU32 titleCol = isDark ? IM_COL32(240, 243, 250, 255) : IM_COL32(20, 25, 35, 255);
            ImU32 msgCol = isDark ? IM_COL32(160, 168, 185, 220) : IM_COL32(105, 112, 128, 220);

            fgDrawList->AddText(ImVec2(tMin.x + 36.0f, tMin.y + 12.0f), titleCol, it->title.c_str());
            fgDrawList->AddText(ImVec2(tMin.x + 36.0f, tMin.y + 32.0f), msgCol, it->message.c_str());

            // Bottom countdown progress bar
            float progressFraction = std::clamp(it->remainingSeconds / it->initialSeconds, 0.0f, 1.0f);
            float progressW = (toastW - 8.0f) * progressFraction;
            fgDrawList->AddRectFilled(
                ImVec2(tMin.x + 4.0f, tMax.y - 2.5f),
                ImVec2(tMin.x + 4.0f + progressW, tMax.y),
                ColorUtils::WithAlpha(accentCol, 0.65f)
            );

            startY += toastH + padY;
            ++it;
        }
    }
}

} // namespace Folio::UI
