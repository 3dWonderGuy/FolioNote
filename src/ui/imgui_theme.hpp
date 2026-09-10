#pragma once
#include "imgui.h"
#include <filesystem>
#include <vector>
#include <string>

namespace FolioTheme {
    inline ImFont* FontRegular          = nullptr; // Standard UI size (20px)
    inline ImFont* FontBold             = nullptr; // Standard UI Bold size (20px)
    inline ImFont* FontRibbonSection    = nullptr; // Ribbon section & toolbar button size (15px = 0.75x)
    inline ImFont* FontRibbonSectionBold= nullptr; // Ribbon section Bold size (15px = 0.75x)
    inline ImFont* FontNavLarge         = nullptr; // Large Nav UI size (26px)
    inline ImFont* FontNavBoldLarge     = nullptr; // Large Nav Bold size (26px)
    inline ImFont* FontRibbonLarge      = nullptr; // 3x Large Regular for all ribbon tabs (32px)
    inline ImFont* FontRibbonBoldLarge  = nullptr; // 3x Large Bold for the active selected tab (32px)
    inline ImFont* FontBoldLarge        = nullptr; // Backward compatibility alias

    inline std::string FindFontPath(const std::vector<std::string>& candidates) {
        std::error_code ec;
        for (const auto& path : candidates) {
            if (std::filesystem::exists(path, ec) && !ec) {
                return path;
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
            // Linux / Fedora (Cantarell, Noto Sans, Liberation Sans, DejaVu Sans, Inter)
            "/usr/share/fonts/cantarell/Cantarell-Regular.otf",
            "/usr/share/fonts/cantarell/Cantarell-VF.otf",
            "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
            "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/inter/Inter-Regular.ttf",
            "/usr/share/fonts/inter/Inter-Regular.otf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
            // Bundled and relative repo assets
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
            // Linux / Fedora
            "/usr/share/fonts/cantarell/Cantarell-Bold.otf",
            "/usr/share/fonts/cantarell/Cantarell-VF.otf",
            "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
            "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
            "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/inter/Inter-Bold.ttf",
            "/usr/share/fonts/inter/Inter-Bold.otf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
            // Bundled and relative repo assets
            "assets/fonts/Roboto-Bold.ttf",
            "assets/fonts/Roboto-Medium.ttf",
            "../assets/fonts/Roboto-Medium.ttf",
            "third_party/imgui/misc/fonts/Roboto-Medium.ttf",
            "../third_party/imgui/misc/fonts/Roboto-Medium.ttf",
            "../../third_party/imgui/misc/fonts/Roboto-Medium.ttf"
        };

        std::string regularPath = FindFontPath(regularCandidates);
        std::string boldPath    = FindFontPath(boldCandidates);

        // 1. Standard regular UI font
        if (!regularPath.empty()) {
            FontRegular       = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 20.0f, &cfg);
            FontRibbonSection = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 15.0f, &cfg); // 0.75x of 20px
            FontNavLarge      = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 23.0f, &cfg);
            FontRibbonLarge   = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 32.0f, &cfg);
        } else {
            FontRegular       = io.Fonts->AddFontDefault(&cfg);
            FontRibbonSection = FontRegular;
            FontNavLarge      = FontRegular;
            FontRibbonLarge   = FontRegular;
        }

        // 2. Bold fonts
        if (!boldPath.empty()) {
            FontBold              = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 20.0f, &cfg);
            FontRibbonSectionBold = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 15.0f, &cfg); // 0.75x of 20px
            FontNavBoldLarge      = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 23.0f, &cfg);
            FontRibbonBoldLarge   = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 32.0f, &cfg);
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