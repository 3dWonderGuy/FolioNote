#pragma once
#include "imgui.h"
#include "app/theme_manager.hpp"
#include <string>

class ThemeCustomizerModal {
public:
    bool isVisible = false;
    char configPath[128] = "config/theme_custom.json";
    std::string statusMessage = "";

    void Render(ThemeManager& theme) {
        if (!isVisible) return;

        ImGui::SetNextWindowSize(ImVec2(520, 580), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Appearance & Theme Studio [F4]", &isVisible, ImGuiWindowFlags_NoCollapse)) {
            // 1. Preset Selector
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "PRESET THEMES");
            const char* presets[] = { "Fluent Dark", "Fluent Light", "Nord Dark", "Custom" };
            int selected = static_cast<int>(theme.currentPreset);

            if (ImGui::Combo("Theme Preset", &selected, presets, IM_ARRAYSIZE(presets))) {
                theme.ApplyTheme(static_cast<ThemePreset>(selected));
                statusMessage = std::string("Loaded preset: ") + presets[selected];
            }

            ImGui::Separator();

            // 2. Live Color Palette
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "COLOR PALETTE (LIVE PREVIEW)");
            bool changed = false;

            changed |= ImGui::ColorEdit4("Workspace Background", (float*)&theme.colorBg, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Panels & Toolbars",     (float*)&theme.colorPanel, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Primary Accent",        (float*)&theme.colorPrimary, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Base Text",             (float*)&theme.colorText, ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Borders & Dividers",    (float*)&theme.colorBorder, ImGuiColorEditFlags_AlphaBar);

            ImGui::Separator();

            // 3. UI Geometry & Rounding
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "GEOMETRY & ROUNDING");
            changed |= ImGui::SliderFloat("Window Rounding", &theme.windowRounding, 0.0f, 16.0f, "%.1f px");
            changed |= ImGui::SliderFloat("Button/Frame Rounding", &theme.frameRounding, 0.0f, 12.0f, "%.1f px");
            changed |= ImGui::SliderFloat("Popup Rounding", &theme.popupRounding, 0.0f, 12.0f, "%.1f px");
            changed |= ImGui::SliderFloat("Tab Rounding", &theme.tabRounding, 0.0f, 12.0f, "%.1f px");

            if (changed) {
                theme.currentPreset = ThemePreset::Custom;
                theme.ApplyToImGui();
            }

            ImGui::Separator();

            // 4. JSON Config File Export / Import
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "JSON CONFIGURATION");
            ImGui::InputText("Config Path", configPath, sizeof(configPath));

            if (ImGui::Button("Save to JSON", ImVec2(140, 28))) {
                if (theme.SaveToJson(configPath)) {
                    statusMessage = "Theme successfully exported to " + std::string(configPath);
                } else {
                    statusMessage = "Error saving file to " + std::string(configPath);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load from JSON", ImVec2(140, 28))) {
                if (theme.LoadFromJson(configPath)) {
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