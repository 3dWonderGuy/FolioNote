#pragma once
#include "imgui.h"
#include "app/theme_manager.hpp"
#include "core/engine/canvas_engine.hpp"
#include "ui/icon_manager.hpp"
#include <string>

class ThemeCustomizerModal {
public:
    bool isVisible = false;
    char configPath[128] = "config/theme_custom.json";
    std::string statusMessage = "";

    void Render(ThemeManager& theme, CanvasEngine* canvas = nullptr, SDL_Window* window = nullptr) {
        if (!isVisible) return;

        ImGui::SetNextWindowSize(ImVec2(540, 600), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

        OverlayThemeScope overlayScope(theme);
        if (ImGui::Begin("Appearance & Theme Studio [F4]", &isVisible, ImGuiWindowFlags_NoCollapse)) {
            // Brand Logo Header
            GLuint modalLogoTex = g_IconManager.LoadOrGetSVG("app_logo", "assets/icons/logo.svg", 128);
            if (modalLogoTex != 0) {
                ImGui::Image((ImTextureID)(intptr_t)modalLogoTex, ImVec2(36.0f, 36.0f));
                ImGui::SameLine(0, 12.0f);
                ImGui::BeginGroup();
                ImGui::TextUnformatted("FolioNote Studio");
                ImGui::TextColored(theme.colorTextMuted, "Visual Customizer & Design Tokens");
                ImGui::EndGroup();
                ImGui::Separator();
            }

            // 1. Preset Selector
            ImGui::TextColored(theme.colorPrimary, "PRESET THEMES");
            const char* presets[] = { "Folio Dark", "Folio Light", "Folio Color (Default)", "Custom" };
            int selected = static_cast<int>(theme.currentPreset);

            if (ImGui::Combo("Theme Preset", &selected, presets, IM_ARRAYSIZE(presets))) {
                theme.ApplyTheme(static_cast<ThemePreset>(selected));
                theme.UpdateOSWindowFrame(window);
                statusMessage = std::string("Loaded preset: ") + presets[selected];
                if (canvas) {
                    if (theme.currentPreset == ThemePreset::FolioDark) {
                        canvas->canvasBgColor = BLRgba32(0x10, 0x10, 0x12);
                        canvas->gridLineColor = BLRgba32(0x1E, 0x22, 0x2A);
                    } else {
                        canvas->canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
                        canvas->gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
                    }
                    canvas->isDirty = true;
                    canvas->needsFullRebake = true;
                }
            }

            ImGui::Separator();

            // 2. Live Color Palette
            ImGui::TextColored(theme.colorPrimary, "COLOR PALETTE (LIVE PREVIEW)");
            bool changed = false;

            changed |= ImGui::ColorEdit4("Workspace Background", (float*)&theme.colorBg, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Panels & Toolbars",     (float*)&theme.colorPanel, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Primary Accent",        (float*)&theme.colorPrimary, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Base Text",             (float*)&theme.colorText, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Borders & Dividers",    (float*)&theme.colorBorder, ImGuiColorEditFlags_AlphaBar);

            ImGui::Separator();

            // 3. UI Geometry & Rounding
            ImGui::TextColored(theme.colorPrimary, "GEOMETRY & ROUNDING");
            changed |= ImGui::SliderFloat("Window Rounding", &theme.windowRounding, 0.0f, 16.0f, "%.1f px");
            changed |= ImGui::SliderFloat("Button/Frame Rounding", &theme.frameRounding, 0.0f, 12.0f, "%.1f px");
            changed |= ImGui::SliderFloat("Popup Rounding", &theme.popupRounding, 0.0f, 12.0f, "%.1f px");
            changed |= ImGui::SliderFloat("Tab Rounding", &theme.tabRounding, 0.0f, 12.0f, "%.1f px");

            if (changed) {
                theme.currentPreset = ThemePreset::Custom;
                theme.ApplyToImGui();
                theme.UpdateOSWindowFrame(window);
            }

            ImGui::Separator();

            // 4. JSON Config File Export / Import
            ImGui::TextColored(theme.colorPrimary, "JSON CONFIGURATION");
            float availW = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(availW);
            ImGui::InputText("##ConfigPath", configPath, sizeof(configPath));
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            if (ImGui::Button("Save to JSON", ImVec2(150, 32))) {
                if (theme.SaveToJson(configPath)) {
                    statusMessage = "Theme successfully exported to " + std::string(configPath);
                } else {
                    statusMessage = "Error saving file to " + std::string(configPath);
                }
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Load from JSON", ImVec2(150, 32))) {
                if (theme.LoadFromJson(configPath)) {
                    theme.UpdateOSWindowFrame(window);
                    statusMessage = "Theme successfully loaded from " + std::string(configPath);
                } else {
                    statusMessage = "Error reading file: " + std::string(configPath);
                }
            }

            if (!statusMessage.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", statusMessage.c_str());
            }

            ImGui::End();
        }
    }
};