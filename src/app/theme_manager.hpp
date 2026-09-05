#pragma once
#include "imgui.h"
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

// Theme Presets
enum class ThemePreset {
    FolioDark = 0,
    FolioLight = 1,
    FolioColor = 2,
    Custom = 3
};

class ThemeManager {
public:
    ThemePreset currentPreset = ThemePreset::FolioDark;

    // Core Color Palette
    ImVec4 colorBg               = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    ImVec4 colorPanel            = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    ImVec4 colorShelf            = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    ImVec4 colorPrimary          = ImVec4(0.20f, 0.48f, 0.92f, 1.00f);
    ImVec4 colorPrimaryHover     = ImVec4(0.26f, 0.55f, 1.00f, 1.00f);
    ImVec4 colorText             = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
    ImVec4 colorTextMuted        = ImVec4(0.60f, 0.60f, 0.65f, 1.00f);
    ImVec4 colorBorder           = ImVec4(0.22f, 0.22f, 0.26f, 0.60f);

    // Sidebar & Navigation Theming
    ImVec4 colorNavBg            = ImVec4(0.14f, 0.14f, 0.17f, 1.00f); // Top header / container background
    ImVec4 colorSectionBg        = ImVec4(0.11f, 0.11f, 0.13f, 1.00f); // Section column background (distinct)
    ImVec4 colorPageBg           = ImVec4(0.14f, 0.14f, 0.17f, 1.00f); // Page column background (distinct)
    ImVec4 colorItemHover        = ImVec4(0.20f, 0.20f, 0.24f, 1.00f); // Subtle hover
    ImVec4 colorItemSelected     = ImVec4(0.26f, 0.26f, 0.31f, 1.00f); // Selected neutral gray tone (NOT blue)
    ImVec4 colorItemText         = ImVec4(0.88f, 0.88f, 0.92f, 1.00f); // Item text
    ImVec4 colorItemSelectedText = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // Selected item text
    ImVec4 colorActionBtn        = ImVec4(0.18f, 0.18f, 0.22f, 1.00f); // Action buttons (<, ==, nb, + Sec, + Page)
    ImVec4 colorActionBtnHover   = ImVec4(0.24f, 0.24f, 0.29f, 1.00f); // Action button hover
    ImVec4 colorActionBtnActive  = ImVec4(0.28f, 0.28f, 0.34f, 1.00f); // Action button active/click

    // Geometry Rounding Metrics
    float windowRounding     = 0.0f;
    float childRounding      = 6.0f;
    float frameRounding      = 6.0f;
    float popupRounding      = 6.0f;
    float tabRounding        = 6.0f;
    float grabRounding       = 4.0f;
    float scrollbarRounding  = 4.0f;

    void ApplyTheme(ThemePreset preset) {
        currentPreset = preset;
        switch (preset) {
            case ThemePreset::FolioDark:
                colorBg               = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
                colorPanel            = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
                colorShelf            = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
                colorPrimary          = ImVec4(0.20f, 0.48f, 0.92f, 1.00f);
                colorPrimaryHover     = ImVec4(0.26f, 0.55f, 1.00f, 1.00f);
                colorText             = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
                colorTextMuted        = ImVec4(0.60f, 0.60f, 0.65f, 1.00f);
                colorBorder           = ImVec4(0.22f, 0.22f, 0.26f, 0.60f);

                // Sidebar & Navigation Dark Profile (Neutral grey selection)
                colorNavBg            = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
                colorSectionBg        = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
                colorPageBg           = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
                colorItemHover        = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
                colorItemSelected     = ImVec4(0.26f, 0.26f, 0.31f, 1.00f);
                colorItemText         = ImVec4(0.88f, 0.88f, 0.92f, 1.00f);
                colorItemSelectedText = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
                colorActionBtn        = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
                colorActionBtnHover   = ImVec4(0.24f, 0.24f, 0.29f, 1.00f);
                colorActionBtnActive  = ImVec4(0.28f, 0.28f, 0.34f, 1.00f);
                break;

            case ThemePreset::FolioLight:
                colorBg               = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
                colorPanel            = ImVec4(0.95f, 0.95f, 0.96f, 1.00f);
                colorShelf            = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
                colorPrimary          = ImVec4(0.00f, 0.45f, 0.88f, 1.00f);
                colorPrimaryHover     = ImVec4(0.10f, 0.55f, 0.95f, 1.00f);
                colorText             = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
                colorTextMuted        = ImVec4(0.48f, 0.48f, 0.53f, 1.00f);
                colorBorder           = ImVec4(0.86f, 0.86f, 0.89f, 1.00f);

                // Sidebar & Navigation Light Profile (White base, slightly darker grey on hover/select, distinct section bg)
                colorNavBg            = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
                colorSectionBg        = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
                colorPageBg           = ImVec4(0.97f, 0.97f, 0.98f, 1.00f);
                colorItemHover        = ImVec4(0.86f, 0.86f, 0.89f, 1.00f);
                colorItemSelected     = ImVec4(0.79f, 0.79f, 0.83f, 1.00f); // Neutral darker slate-grey when selected (NOT BLUE)
                colorItemText         = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
                colorItemSelectedText = ImVec4(0.05f, 0.05f, 0.08f, 1.00f);
                colorActionBtn        = ImVec4(0.89f, 0.89f, 0.92f, 1.00f);
                colorActionBtnHover   = ImVec4(0.83f, 0.83f, 0.86f, 1.00f);
                colorActionBtnActive  = ImVec4(0.78f, 0.78f, 0.82f, 1.00f);
                break;

            case ThemePreset::FolioColor:
                colorBg               = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
                colorPanel            = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
                colorShelf            = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
                colorPrimary          = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
                colorPrimaryHover     = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
                colorText             = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
                colorTextMuted        = ImVec4(0.85f, 0.87f, 0.91f, 1.00f);
                colorBorder           = ImVec4(0.30f, 0.34f, 0.42f, 0.80f);

                colorNavBg            = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
                colorSectionBg        = ImVec4(0.19f, 0.22f, 0.27f, 1.00f);
                colorPageBg           = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
                colorItemHover        = ImVec4(0.28f, 0.32f, 0.39f, 1.00f);
                colorItemSelected     = ImVec4(0.34f, 0.38f, 0.47f, 1.00f);
                colorItemText         = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
                colorItemSelectedText = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
                colorActionBtn        = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
                colorActionBtnHover   = ImVec4(0.31f, 0.36f, 0.44f, 1.00f);
                colorActionBtnActive  = ImVec4(0.36f, 0.42f, 0.51f, 1.00f);
                break;

            case ThemePreset::Custom:
                break;
        }
        ApplyToImGui();
    }

    void ApplyToImGui() {
        ImGuiStyle& s = ImGui::GetStyle();

        s.WindowRounding    = windowRounding;
        s.ChildRounding     = childRounding;
        s.FrameRounding     = frameRounding;
        s.PopupRounding     = popupRounding;
        s.TabRounding       = tabRounding;
        s.GrabRounding      = grabRounding;
        s.ScrollbarRounding = scrollbarRounding;

        s.WindowBorderSize  = 0.0f;
        s.ChildBorderSize   = 0.0f;
        s.FrameBorderSize   = 0.0f;
        s.PopupBorderSize   = 1.0f;
        s.TabBorderSize     = 0.0f;

        s.WindowPadding     = ImVec2(12.0f, 12.0f);
        s.FramePadding      = ImVec2(12.0f, 8.0f);
        s.ItemSpacing       = ImVec2(10.0f, 10.0f);

        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]         = colorBg;
        c[ImGuiCol_ChildBg]          = colorPanel;
        c[ImGuiCol_PopupBg]          = ImVec4(colorPanel.x, colorPanel.y, colorPanel.z, 0.98f);
        c[ImGuiCol_Border]           = colorBorder;
        c[ImGuiCol_FrameBg]          = colorShelf;
        c[ImGuiCol_FrameBgHovered]   = ImVec4(colorShelf.x * 1.15f, colorShelf.y * 1.15f, colorShelf.z * 1.15f, 1.0f);
        c[ImGuiCol_FrameBgActive]    = colorPrimary;
        c[ImGuiCol_Button]           = colorActionBtn;
        c[ImGuiCol_ButtonHovered]    = colorActionBtnHover;
        c[ImGuiCol_ButtonActive]     = colorActionBtnActive;
        c[ImGuiCol_Header]           = colorShelf;
        c[ImGuiCol_HeaderHovered]    = colorItemHover;
        c[ImGuiCol_HeaderActive]     = colorItemSelected;
        c[ImGuiCol_Text]             = colorText;
        c[ImGuiCol_TextDisabled]     = colorTextMuted;
        c[ImGuiCol_CheckMark]        = colorPrimary;
        c[ImGuiCol_SliderGrab]       = colorPrimary;
        c[ImGuiCol_SliderGrabActive] = colorPrimaryHover;
        c[ImGuiCol_Tab]              = colorPanel;
        c[ImGuiCol_TabHovered]       = colorShelf;
        c[ImGuiCol_TabSelected]      = colorShelf;
        c[ImGuiCol_Separator]        = colorBorder;
    }

    bool SaveToJson(const std::string& filepath) {
        std::filesystem::path dir = std::filesystem::path(filepath).parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }

        std::ofstream out(filepath);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"preset\": " << static_cast<int>(currentPreset) << ",\n";
        out << "  \"colorBg\": [" << colorBg.x << ", " << colorBg.y << ", " << colorBg.z << ", " << colorBg.w << "],\n";
        out << "  \"colorPanel\": [" << colorPanel.x << ", " << colorPanel.y << ", " << colorPanel.z << ", " << colorPanel.w << "],\n";
        out << "  \"colorShelf\": [" << colorShelf.x << ", " << colorShelf.y << ", " << colorShelf.z << ", " << colorShelf.w << "],\n";
        out << "  \"colorPrimary\": [" << colorPrimary.x << ", " << colorPrimary.y << ", " << colorPrimary.z << ", " << colorPrimary.w << "],\n";
        out << "  \"colorText\": [" << colorText.x << ", " << colorText.y << ", " << colorText.z << ", " << colorText.w << "],\n";
        out << "  \"colorBorder\": [" << colorBorder.x << ", " << colorBorder.y << ", " << colorBorder.z << ", " << colorBorder.w << "],\n";
        out << "  \"colorNavBg\": [" << colorNavBg.x << ", " << colorNavBg.y << ", " << colorNavBg.z << ", " << colorNavBg.w << "],\n";
        out << "  \"colorSectionBg\": [" << colorSectionBg.x << ", " << colorSectionBg.y << ", " << colorSectionBg.z << ", " << colorSectionBg.w << "],\n";
        out << "  \"colorPageBg\": [" << colorPageBg.x << ", " << colorPageBg.y << ", " << colorPageBg.z << ", " << colorPageBg.w << "],\n";
        out << "  \"colorItemHover\": [" << colorItemHover.x << ", " << colorItemHover.y << ", " << colorItemHover.z << ", " << colorItemHover.w << "],\n";
        out << "  \"colorItemSelected\": [" << colorItemSelected.x << ", " << colorItemSelected.y << ", " << colorItemSelected.z << ", " << colorItemSelected.w << "],\n";
        out << "  \"colorActionBtn\": [" << colorActionBtn.x << ", " << colorActionBtn.y << ", " << colorActionBtn.z << ", " << colorActionBtn.w << "],\n";
        out << "  \"windowRounding\": " << windowRounding << ",\n";
        out << "  \"frameRounding\": " << frameRounding << ",\n";
        out << "  \"popupRounding\": " << popupRounding << ",\n";
        out << "  \"tabRounding\": " << tabRounding << "\n";
        out << "}\n";
        return true;
    }

    bool LoadFromJson(const std::string& filepath) {
        // ANDROID (and general robustness): The throwing overload of filesystem::exists()
        // raises std::filesystem::filesystem_error on permission denied errors.
        // On Android, trying to stat relative paths (e.g. "config/theme_custom.json")
        // relative to the read-only root "/" triggers EACCES from the kernel, which
        // the throwing overload rethrows as an uncaught exception, crashing the app.
        // Using the error_code overload ensures we always get a safe true/false result.
        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec) || ec) return false;
        std::ifstream in(filepath);
        if (!in.is_open()) return false;

        auto parseVec4 = [](const std::string& line) -> ImVec4 {
            size_t openBracket = line.find('[');
            size_t closeBracket = line.find(']');
            if (openBracket == std::string::npos || closeBracket == std::string::npos) return ImVec4(1, 1, 1, 1);
            std::string values = line.substr(openBracket + 1, closeBracket - openBracket - 1);
            std::stringstream ss(values);
            float x = 0, y = 0, z = 0, w = 1;
            char comma;
            ss >> x >> comma >> y >> comma >> z >> comma >> w;
            return ImVec4(x, y, z, w);
        };

        auto parseFloat = [](const std::string& line) -> float {
            size_t colon = line.find(':');
            if (colon == std::string::npos) return 0.0f;
            return std::stof(line.substr(colon + 1));
        };

        std::string line;
        while (std::getline(in, line)) {
            if (line.find("\"colorBg\"") != std::string::npos)                      colorBg = parseVec4(line);
            else if (line.find("\"colorPanel\"") != std::string::npos)               colorPanel = parseVec4(line);
            else if (line.find("\"colorShelf\"") != std::string::npos)               colorShelf = parseVec4(line);
            else if (line.find("\"colorPrimary\"") != std::string::npos)             colorPrimary = parseVec4(line);
            else if (line.find("\"colorText\"") != std::string::npos)                colorText = parseVec4(line);
            else if (line.find("\"colorBorder\"") != std::string::npos)              colorBorder = parseVec4(line);
            else if (line.find("\"colorNavBg\"") != std::string::npos)               colorNavBg = parseVec4(line);
            else if (line.find("\"colorSectionBg\"") != std::string::npos)           colorSectionBg = parseVec4(line);
            else if (line.find("\"colorPageBg\"") != std::string::npos)              colorPageBg = parseVec4(line);
            else if (line.find("\"colorItemHover\"") != std::string::npos)          colorItemHover = parseVec4(line);
            else if (line.find("\"colorItemSelected\"") != std::string::npos)       colorItemSelected = parseVec4(line);
            else if (line.find("\"colorActionBtn\"") != std::string::npos)          colorActionBtn = parseVec4(line);
            else if (line.find("\"windowRounding\"") != std::string::npos)           windowRounding = parseFloat(line);
            else if (line.find("\"frameRounding\"") != std::string::npos)            frameRounding = parseFloat(line);
            else if (line.find("\"popupRounding\"") != std::string::npos)            popupRounding = parseFloat(line);
            else if (line.find("\"tabRounding\"") != std::string::npos)              tabRounding = parseFloat(line);
        }

        currentPreset = ThemePreset::Custom;
        ApplyToImGui();
        return true;
    }
};