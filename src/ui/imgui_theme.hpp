#pragma once
#include "imgui.h"
#include <filesystem>
#include <vector>
#include <string>

namespace FolioTheme {
    inline ImFont* FontRegular          = nullptr; // Standard UI size (15px)
    inline ImFont* FontBold             = nullptr; // Standard UI Bold size (15px)
    inline ImFont* FontRibbonSection    = nullptr; // Ribbon section & toolbar button size (12px)
    inline ImFont* FontRibbonSectionBold= nullptr; // Ribbon section Bold size (12px)
    inline ImFont* FontNavLarge         = nullptr; // Large Nav UI size (16px)
    inline ImFont* FontNavBoldLarge     = nullptr; // Large Nav Bold size (16px)
    inline ImFont* FontRibbonLarge      = nullptr; // Modern Regular for ribbon tabs (17px)
    inline ImFont* FontRibbonBoldLarge  = nullptr; // Modern Bold for the active selected tab (17px)
    inline ImFont* FontBoldLarge        = nullptr; // Backward compatibility alias

    inline std::string FindFontPath(const std::vector<std::string>& candidates) {
        std::error_code ec;
        const char* home = std::getenv("HOME");
        for (const auto& path : candidates) {
            std::string resolved = path;
            if (!resolved.empty() && resolved[0] == '~' && home) {
                resolved = std::string(home) + resolved.substr(1);
            }
            if (std::filesystem::exists(resolved, ec) && !ec) {
                return resolved;
            }
        }
        return "";
    }

    inline void LoadModernFonts(ImGuiIO& io) {
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = true;

#if defined(__ANDROID__)
        // ANDROID: Windows system fonts do not exist on Android.
        // Fall back to scalable default or bundled font.
        FontRegular           = io.Fonts->AddFontDefault(&cfg);
        FontRibbonSection     = FontRegular;
        FontRibbonSectionBold = FontRegular;
        FontNavLarge          = FontRegular;
        FontRibbonLarge       = FontRegular;
        FontBold              = FontRegular;
        FontNavBoldLarge      = FontRegular;
        FontRibbonBoldLarge   = FontRegular;
#else
        const std::vector<std::string> regularCandidates = {
            // Windows
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            // Bundled and relative repo assets (Segoe UI or Inter or Roboto)
            "assets/fonts/segoeui.ttf",
            "../assets/fonts/segoeui.ttf",
            "../../assets/fonts/segoeui.ttf",
            "bin/assets/fonts/segoeui.ttf",
            // User local fonts (~/.local/share/fonts/)
            "~/.local/share/fonts/segoeui.ttf",
            "~/.fonts/segoeui.ttf",
            "/usr/share/fonts/segoeui.ttf",
            // Linux Modern Clean System Fonts (Inter, Adwaita, Open Sans, Liberation Sans, Cantarell)
            "/usr/share/fonts/inter/Inter-Regular.ttf",
            "/usr/share/fonts/inter/Inter-Regular.otf",
            "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
            "/usr/share/fonts/open-sans/OpenSans-Regular.ttf",
            "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
            "/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf",
            "/usr/share/fonts/google-carlito-fonts/Carlito-Regular.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            // Fallback bundled assets
            "assets/fonts/Roboto-Medium.ttf",
            "../assets/fonts/Roboto-Medium.ttf",
            "../../assets/fonts/Roboto-Medium.ttf",
            "third_party/imgui/misc/fonts/Roboto-Medium.ttf",
            "../third_party/imgui/misc/fonts/Roboto-Medium.ttf",
            "../../third_party/imgui/misc/fonts/Roboto-Medium.ttf"
        };

        const std::vector<std::string> boldCandidates = {
            // Windows
            "C:\\Windows\\Fonts\\segoeuib.ttf",
            "C:/Windows/Fonts/segoeuib.ttf",
            // Bundled and relative repo assets
            "assets/fonts/segoeuib.ttf",
            "../assets/fonts/segoeuib.ttf",
            "../../assets/fonts/segoeuib.ttf",
            "bin/assets/fonts/segoeuib.ttf",
            // User local fonts (~/.local/share/fonts/)
            "~/.local/share/fonts/segoeuib.ttf",
            "~/.fonts/segoeuib.ttf",
            "/usr/share/fonts/segoeuib.ttf",
            // Linux Modern Clean System Fonts
            "/usr/share/fonts/inter/Inter-Bold.ttf",
            "/usr/share/fonts/inter/Inter-Bold.otf",
            "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
            "/usr/share/fonts/open-sans/OpenSans-Bold.ttf",
            "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
            "/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Bold.otf",
            "/usr/share/fonts/google-carlito-fonts/Carlito-Bold.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            // Fallback bundled assets
            "assets/fonts/Roboto-Bold.ttf",
            "assets/fonts/Roboto-Medium.ttf",
            "../assets/fonts/Roboto-Medium.ttf",
            "third_party/imgui/misc/fonts/Roboto-Medium.ttf"
        };

        std::string regularPath = FindFontPath(regularCandidates);
        std::string boldPath    = FindFontPath(boldCandidates);

        // 1. Standard regular UI font
        if (!regularPath.empty()) {
            FontRegular       = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 15.0f, &cfg);
            FontRibbonSection = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 12.0f, &cfg);
            FontNavLarge      = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 16.0f, &cfg);
            FontRibbonLarge   = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 17.0f, &cfg);
        } else {
            FontRegular       = io.Fonts->AddFontDefault(&cfg);
            FontRibbonSection = FontRegular;
            FontNavLarge      = FontRegular;
            FontRibbonLarge   = FontRegular;
        }

        // 2. Bold fonts
        if (!boldPath.empty()) {
            FontBold              = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 15.0f, &cfg);
            FontRibbonSectionBold = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 12.0f, &cfg);
            FontNavBoldLarge      = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 16.0f, &cfg);
            FontRibbonBoldLarge   = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 17.0f, &cfg);
        } else {
            FontBold              = FontRegular;
            FontRibbonSectionBold = FontRibbonSection;
            FontNavBoldLarge      = FontNavLarge;
            FontRibbonBoldLarge   = FontRibbonLarge;
        }
#endif

        FontBoldLarge = FontRibbonBoldLarge;
    }

    inline void ApplyModernFluentDark() {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 6.0f;
        style.PopupRounding     = 6.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 6.0f;

        style.WindowBorderSize  = 0.0f;
        style.ChildBorderSize   = 0.0f;
        style.FrameBorderSize   = 0.0f;
        style.PopupBorderSize   = 1.0f;

        style.WindowPadding     = ImVec2(12.0f, 12.0f);
        style.FramePadding      = ImVec2(12.0f, 8.0f);
        style.ItemSpacing       = ImVec2(10.0f, 10.0f);

        ImVec4* c = style.Colors;
        c[ImGuiCol_WindowBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        c[ImGuiCol_ChildBg]          = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
        c[ImGuiCol_PopupBg]          = ImVec4(0.14f, 0.14f, 0.17f, 0.98f);
        c[ImGuiCol_Border]           = ImVec4(0.15f, 0.15f, 0.18f, 0.00f);
        c[ImGuiCol_FrameBg]          = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
        c[ImGuiCol_FrameBgHovered]   = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
        c[ImGuiCol_FrameBgActive]    = ImVec4(0.26f, 0.26f, 0.34f, 1.00f);
        c[ImGuiCol_Button]           = ImVec4(0.19f, 0.19f, 0.23f, 1.00f);
        c[ImGuiCol_ButtonHovered]    = ImVec4(0.25f, 0.25f, 0.31f, 1.00f);
        c[ImGuiCol_ButtonActive]     = ImVec4(0.32f, 0.32f, 0.40f, 1.00f);
        c[ImGuiCol_Header]           = ImVec4(0.20f, 0.20f, 0.25f, 0.70f);
        c[ImGuiCol_HeaderHovered]    = ImVec4(0.26f, 0.26f, 0.32f, 0.85f);
        c[ImGuiCol_HeaderActive]     = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
        c[ImGuiCol_Text]             = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
        c[ImGuiCol_TextDisabled]     = ImVec4(0.55f, 0.55f, 0.60f, 1.00f);
        c[ImGuiCol_Separator]        = ImVec4(0.15f, 0.15f, 0.18f, 0.00f);
    }
}