#pragma once
#include "imgui.h"
#include <filesystem>

namespace FolioTheme {
    inline ImFont* FontRegular          = nullptr; // Standard UI size (20px)
    inline ImFont* FontBold             = nullptr; // Standard UI Bold size (20px)
    inline ImFont* FontNavLarge         = nullptr; // Large Nav UI size (26px)
    inline ImFont* FontNavBoldLarge     = nullptr; // Large Nav Bold size (26px)
    inline ImFont* FontRibbonLarge      = nullptr; // 3x Large Regular for all ribbon tabs (32px)
    inline ImFont* FontRibbonBoldLarge  = nullptr; // 3x Large Bold for the active selected tab (32px)
    inline ImFont* FontBoldLarge        = nullptr; // Backward compatibility alias

    inline void LoadModernFonts(ImGuiIO& io) {
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = true;

#if defined(__ANDROID__)
        // ANDROID: Windows system fonts (Segoe UI) do not exist on Android.
        // ImGui's built-in font is used as a fallback. To get a custom font on Android,
        // bundle a .ttf file in the project's assets/ folder and load it like:
        //   FontRegular = io.Fonts->AddFontFromFileTTF("fonts/Inter-Regular.ttf", 20.0f, &cfg);
        // SDL_IOFromFile will resolve the path from the APK's assets/ bundle automatically.
        FontRegular         = io.Fonts->AddFontDefault(&cfg);
        FontNavLarge        = FontRegular;
        FontRibbonLarge     = FontRegular;
        FontBold            = FontRegular;
        FontNavBoldLarge    = FontRegular;
        FontRibbonBoldLarge = FontRegular;
#else
        const char* regularPath = "C:\\Windows\\Fonts\\segoeui.ttf";
        const char* boldPath    = "C:\\Windows\\Fonts\\segoeuib.ttf";

        // Use non-throwing error_code overload to avoid crashes on non-Windows systems.
        std::error_code ec;

        // 1. Standard regular UI font
        if (std::filesystem::exists(regularPath, ec) && !ec) {
            FontRegular     = io.Fonts->AddFontFromFileTTF(regularPath, 20.0f, &cfg);
            FontNavLarge    = io.Fonts->AddFontFromFileTTF(regularPath, 23.0f, &cfg);
            FontRibbonLarge = io.Fonts->AddFontFromFileTTF(regularPath, 32.0f, &cfg);
        } else {
            FontRegular     = io.Fonts->AddFontDefault(&cfg);
            FontNavLarge    = FontRegular;
            FontRibbonLarge = FontRegular;
        }

        // 2. Bold fonts
        if (std::filesystem::exists(boldPath, ec) && !ec) {
            FontBold            = io.Fonts->AddFontFromFileTTF(boldPath, 20.0f, &cfg);
            FontNavBoldLarge    = io.Fonts->AddFontFromFileTTF(boldPath, 23.0f, &cfg);
            FontRibbonBoldLarge = io.Fonts->AddFontFromFileTTF(boldPath, 32.0f, &cfg);
        } else {
            FontBold            = FontRegular;
            FontNavBoldLarge    = FontNavLarge;
            FontRibbonBoldLarge = FontRibbonLarge;
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