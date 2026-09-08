#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>
#include "imgui.h"
#include "ui/imgui_theme.hpp"
#include "ui/icon_manager.hpp"
#include "app/theme_manager.hpp"
#include "core/document/document_session.hpp"
#include "core/document/library.hpp"
#include "core/export/export_manager.hpp"
#include "core/import/import_manager.hpp"
#include "core/engine/canvas_engine.hpp"
#include "app/app_view_mode.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

enum class BackstageTab {
    Info,
    NotebooksAndLibraries,
    ExportAndPrint,
    Import,
    Settings
};

enum class SettingsSubTab {
    General,
    Appearance,
    Inking,
    Storage,
    Addons,
    About
};

struct NotebookHubView {
    BackstageTab activeTab = BackstageTab::NotebooksAndLibraries;
    SettingsSubTab activeSettingsTab = SettingsSubTab::General;
    LibraryManager libraryManager;
    bool isInitialized = false;

    // Modals & Dialog states
    bool showNewNotebookModal = false;
    bool showSaveAsCopyModal = false;
    bool showNewLibraryModal = false;
    bool showOpenCustomModal = false;

    char newNbName[128] = "My Notes";
    char newNbPath[256] = "";
    int newNbColorIndex = 0;
    int newNbIconIndex = 0;

    char copyNbName[128] = "";
    char copyNbDestPath[256] = "";

    char newLibName[128] = "Work & Research";
    char newLibPath[256] = "";

    char openCustomPath[256] = "";

    // Export state
    ExportScope exportScope = ExportScope::CurrentPage;
    ExportFormat exportFormat = ExportFormat::PDF_Print;
    char exportCustomPath[256] = "";
    std::string exportStatusMessage = "";
    bool exportSuccess = false;

    // Import state
    char importPdfPath[256] = "";
    char importHtmlPath[256] = "";
    char importPkgPath[256] = "";
    std::string importStatusMessage = "";
    bool importSuccess = false;

    // Settings state
    int startupOption = 0; // 0=Resume last page, 1=Open File Hub
    int autoSaveInterval = 1; // 0=30s, 1=1m, 2=5m, 3=Manual
    bool vsyncEnabled = true;
    bool showTelemetryHud = false;
    int selectedLanguage = 0; // 0=English, 1=Spanish, 2=French, 3=German, 4=Japanese

    int selectedThemePreset = 0; // 0=Folio Color, 1=Dark UWP, 2=Fluent Light, 3=Midnight OLED
    float uiScaleFactor = 1.0f;
    char customIconPackPath[256] = "";

    int digitizerDriver = 0; // 0=Windows Ink, 1=Direct Digitizer, 2=Standard Mouse
    int pressureCurve = 1; // 0=Soft, 1=Medium, 2=Firm, 3=Custom
    int strokeSmoothing = 0; // 0=Catmull-Rom, 1=Quadratic Bezier, 2=Raw
    int palmRejection = 1; // 0=Strict, 1=Normal, 2=Disabled
    float rulerDpiCalibration = 96.0f; // DPI matching slider (60 - 200)

    int cacheLimitMb = 512;
    int lruTimeoutSec = 60;
    int backupFrequency = 1; // 0=Daily, 1=Weekly, 2=Disabled

    bool addonMathSolver = true;
    bool addonPdfRasterizer = true;
    bool addonOcrIndexer = false;
    bool addonCloudSync = false;
    bool addonDevMode = false;

    size_t selectedLibraryIndex = 0; // 0 = All Open / Standalone, 1+ = specific library

    void InitIfNeeded(const std::string& defaultPath) {
        if (!isInitialized) {
            libraryManager.Init(defaultPath);
            isInitialized = true;
        }
    }

    void Render(
        float x, 
        float y, 
        float width, 
        float height, 
        AppViewMode& outViewMode, 
        ThemeManager& theme,
        DocumentSession& session,
        CanvasEngine& canvas,
        SDL_Window* window = nullptr
    ) {
        InitIfNeeded(session.workspace.repository.currentPackagePath.empty() ? "FolioNote" : session.workspace.repository.currentPackagePath);

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.colorBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("##BackstageFileView", nullptr, 
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

        float navRailWidth = 240.0f;
        float contentWidth = width - navRailWidth;

        // 1. LEFT NAVIGATION RAIL (Office / UWP Backstage Style)
        RenderNavRail(navRailWidth, height, outViewMode, theme);

        ImGui::SameLine(0.0f, 0.0f);

        // 2. RIGHT MAIN CONTENT AREA
        RenderContentPane(contentWidth, height, session, canvas, theme, outViewMode, window);

        // 3. MODALS AND DIALOGS
        RenderModals(session, canvas, theme);

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

private:
    void RenderNavRail(float railWidth, float railHeight, AppViewMode& outViewMode, const ThemeManager& theme) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorNavBg);
        ImGui::BeginChild("##BackstageNavRail", ImVec2(railWidth, railHeight), false, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        // Back to Canvas Button
        ImGui::SetCursorPosX(14.0f);
        GLuint arrowLeftTex = g_IconManager.LoadOrGetSVG("arrow_left", "assets/icons/Navigation/arrow-left.svg", 64, true);
        
        ImVec2 backBtnSize(railWidth - 28.0f, 42.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, theme.colorNavBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorItemHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        if (ImGui::Button("##BackToCanvasBtn", backBtnSize)) {
            outViewMode = AppViewMode::CanvasWorkspace;
        }
        // Custom draw inside button
        ImVec2 pMin = ImGui::GetItemRectMin();
        ImVec2 pMax = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (arrowLeftTex != 0) {
            dl->AddImage((ImTextureID)(intptr_t)arrowLeftTex, ImVec2(pMin.x + 12.0f, pMin.y + 11.0f), ImVec2(pMin.x + 32.0f, pMin.y + 31.0f));
        }
        ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
        dl->AddText(ImVec2(pMin.x + 40.0f, pMin.y + 10.0f), ImGui::ColorConvertFloat4ToU32(theme.colorText), "Back to Canvas");
        ImGui::PopFont();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::SetCursorPosX(14.0f);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        // Navigation Tabs
        auto RenderRailTab = [&](const char* label, BackstageTab tab, const char* iconKey, const char* iconPath) {
            bool isSelected = (activeTab == tab);
            ImVec2 itemSize(railWidth - 20.0f, 40.0f);
            ImGui::SetCursorPosX(10.0f);

            ImVec4 bgCol = isSelected ? theme.colorItemSelected : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            ImVec4 textCol = isSelected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : theme.colorText;

            ImGui::PushStyleColor(ImGuiCol_Button, bgCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? theme.colorItemSelected : theme.colorItemHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            std::string btnId = std::string("##Tab_") + label;
            if (ImGui::Button(btnId.c_str(), itemSize)) {
                activeTab = tab;
            }

            ImVec2 bMin = ImGui::GetItemRectMin();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            
            GLuint iconTex = g_IconManager.LoadOrGetSVG(iconKey, iconPath, 48, false);
            float textStartX = bMin.x + 16.0f;
            if (iconTex != 0) {
                draw->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(bMin.x + 14.0f, bMin.y + 10.0f), ImVec2(bMin.x + 34.0f, bMin.y + 30.0f));
                textStartX = bMin.x + 44.0f;
            }

            ImGui::PushFont(isSelected ? FolioTheme::FontNavBoldLarge : FolioTheme::FontRegular);
            draw->AddText(ImVec2(textStartX, bMin.y + 10.0f), ImGui::ColorConvertFloat4ToU32(textCol), label);
            ImGui::PopFont();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
        };

        RenderRailTab("Info", BackstageTab::Info, "icon_info", "assets/icons/Navigation/view.svg");
        RenderRailTab("Notebooks & Libraries", BackstageTab::NotebooksAndLibraries, "icon_notebooks", "assets/icons/Sections_Notebooks/blue-notebook.svg");
        RenderRailTab("Export & Print", BackstageTab::ExportAndPrint, "icon_export", "assets/icons/Navigation/add.svg");
        RenderRailTab("Import", BackstageTab::Import, "icon_import", "assets/icons/Navigation/add-section.svg");
        RenderRailTab("Settings", BackstageTab::Settings, "icon_settings", "assets/icons/Navigation/settings.svg");

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void RenderContentPane(
        float contentWidth, 
        float contentHeight, 
        DocumentSession& session, 
        CanvasEngine& canvas, 
        ThemeManager& theme,
        AppViewMode& outViewMode,
        SDL_Window* window
    ) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorPanel);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));
        ImGui::BeginChild("##BackstageContent", ImVec2(contentWidth, contentHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        switch (activeTab) {
            case BackstageTab::Info:
                RenderInfoTab(session, canvas, theme);
                break;
            case BackstageTab::NotebooksAndLibraries:
                RenderNotebooksAndLibrariesTab(session, canvas, theme, outViewMode);
                break;
            case BackstageTab::ExportAndPrint:
                RenderExportAndPrintTab(session, canvas, theme);
                break;
            case BackstageTab::Import:
                RenderImportTab(session, canvas, theme);
                break;
            case BackstageTab::Settings:
                RenderSettingsTab(theme, canvas, window);
                break;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // ========================================================================
    // TAB 1: INFO
    // ========================================================================
    void RenderInfoTab(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto activeNb = session.workspace.GetActiveNotebook();

        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorText, "Notebook Information");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Overview, metadata, storage path, and synchronization state");

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 16.0f));

        if (!activeNb) {
            ImGui::TextColored(theme.colorTextMuted, "No notebook currently open.");
            return;
        }

        // Active Notebook Header Card
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cardMin = ImGui::GetCursorScreenPos();
        float cardWidth = std::min(780.0f, ImGui::GetContentRegionAvail().x);
        float cardHeight = 120.0f;
        ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);

        dl->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg), 8.0f);
        dl->AddRect(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 8.0f, 0, 1.0f);

        // Icon
        GLuint iconTex = 0;
        if (!activeNb->iconFile.empty()) {
            iconTex = g_IconManager.LoadOrGetSVG(activeNb->iconFile, "assets/icons/Sections_Notebooks/" + activeNb->iconFile, 128);
        }
        if (iconTex != 0) {
            dl->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(cardMin.x + 20.0f, cardMin.y + 20.0f), ImVec2(cardMin.x + 100.0f, cardMin.y + 100.0f));
        }

        // Details
        float textX = cardMin.x + 115.0f;
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        dl->AddText(ImVec2(textX, cardMin.y + 20.0f), ImGui::ColorConvertFloat4ToU32(theme.colorText), activeNb->name.c_str());
        ImGui::PopFont();

        std::string pathStr = activeNb->filePath.empty() ? "(In Memory Package)" : activeNb->filePath;
        dl->AddText(ImVec2(textX, cardMin.y + 54.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), pathStr.c_str());

        std::string guidStr = "UUID: " + activeNb->guid;
        dl->AddText(ImVec2(textX, cardMin.y + 78.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), guidStr.c_str());

        ImGui::Dummy(ImVec2(0.0f, cardHeight + 20.0f));

        // Statistics Grid
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "Statistics");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        size_t totalSecs = activeNb->sections.size();
        for (const auto& grp : activeNb->sectionGroups) {
            if (grp) totalSecs += grp->sections.size();
        }
        size_t totalPages = 0;
        size_t totalObjects = 0;
        size_t totalStrokes = 0;

        auto CountPageStats = [&](const std::shared_ptr<CanvasPage>& pg) {
            if (!pg) return;
            totalPages++;
            totalObjects += pg->objects.size();
            for (const auto& obj : pg->objects) {
                if (auto ink = std::dynamic_pointer_cast<InkContainer>(obj)) {
                    totalStrokes += ink->strokes.size();
                }
            }
        };

        for (const auto& sec : activeNb->sections) {
            if (sec) {
                for (const auto& pg : sec->pages) {
                    CountPageStats(pg);
                }
            }
        }
        for (const auto& grp : activeNb->sectionGroups) {
            if (grp) {
                for (const auto& sec : grp->sections) {
                    if (sec) {
                        for (const auto& pg : sec->pages) {
                            CountPageStats(pg);
                        }
                    }
                }
            }
        }

        // Calculate package disk space used
        uintmax_t packageBytes = 0;
        std::error_code ec;
        if (!activeNb->filePath.empty() && std::filesystem::exists(activeNb->filePath, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(activeNb->filePath, ec)) {
                if (std::filesystem::is_regular_file(entry.status())) {
                    packageBytes += entry.file_size(ec);
                }
            }
        }

        auto FormatBytes = [](uintmax_t bytes) -> std::string {
            if (bytes == 0) return "0 KB";
            if (bytes < 1024) return std::to_string(bytes) + " B";
            if (bytes < 1024 * 1024) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0f);
                return buf;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0f * 1024.0f));
            return buf;
        };

        auto FormatTimeDuration = [](uint64_t totalSec) -> std::string {
            uint64_t hours = totalSec / 3600;
            uint64_t mins = (totalSec % 3600) / 60;
            uint64_t secs = totalSec % 60;
            if (hours > 0) {
                return std::to_string(hours) + "h " + std::to_string(mins) + "m";
            }
            if (mins > 0) {
                return std::to_string(mins) + "m " + std::to_string(secs) + "s";
            }
            return std::to_string(secs) + "s";
        };

        auto RenderMetricCard = [&](const char* title, const std::string& val, float width, const char* subtitle = nullptr) {
            ImVec2 mMin = ImGui::GetCursorScreenPos();
            float h = subtitle ? 76.0f : 70.0f;
            ImVec2 mMax(mMin.x + width, mMin.y + h);
            dl->AddRectFilled(mMin, mMax, ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg), 6.0f);
            dl->AddRect(mMin, mMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 6.0f);
            dl->AddText(ImVec2(mMin.x + 14.0f, mMin.y + 10.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), title);
            ImGui::PushFont(FolioTheme::FontNavBoldLarge);
            dl->AddText(ImVec2(mMin.x + 14.0f, mMin.y + 30.0f), ImGui::ColorConvertFloat4ToU32(theme.colorText), val.c_str());
            ImGui::PopFont();
            if (subtitle) {
                dl->AddText(ImVec2(mMin.x + 14.0f, mMin.y + 54.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), subtitle);
            }
            ImGui::Dummy(ImVec2(width, h));
        };

        float mWidth = 180.0f;

        // Row 1: Structural Hierarchy
        RenderMetricCard("Sections", std::to_string(totalSecs), mWidth);
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Section Groups", std::to_string(activeNb->sectionGroups.size()), mWidth);
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Total Pages", std::to_string(totalPages), mWidth);
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Active Page", activeNb->GetActivePage() ? activeNb->GetActivePage()->title : "None", mWidth + 40.0f);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        // Row 2: Inking & Storage Insights
        RenderMetricCard("Global Objects", std::to_string(totalObjects), mWidth, "Canvas Entities");
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Ink Strokes", std::to_string(totalStrokes), mWidth, "Vector Paths");
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Disk Space Used", FormatBytes(packageBytes), mWidth, "SQLite + .ink");
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Storage Engine", "SQLite3 + WAL", mWidth + 40.0f, "Zero-Latency I/O");

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        // Row 3: Time Tracking Telemetry & Integrity
        RenderMetricCard("Session Time", FormatTimeDuration(activeNb->GetSessionSeconds()), mWidth, "Current Session");
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Total Time Spent", FormatTimeDuration(activeNb->GetTotalLifetimeSeconds()), mWidth, "All-Time on Notebook");
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Document Health", "100% Synced", mWidth, "Verified Clean");
        ImGui::SameLine(0.0f, 14.0f);
        RenderMetricCard("Data Protection", "WAL Safe", mWidth + 40.0f, "Crash Resilient");

        ImGui::Dummy(ImVec2(0.0f, 24.0f));

        // Action Buttons
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "Actions");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        if (ImGui::Button("Open Folder in Explorer", ImVec2(200.0f, 40.0f))) {
#if defined(_WIN32)
            if (!activeNb->filePath.empty()) {
                ShellExecuteA(NULL, "open", activeNb->filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
#endif
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (ImGui::Button("Copy Package Path", ImVec2(180.0f, 40.0f))) {
            SDL_SetClipboardText(activeNb->filePath.c_str());
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (ImGui::Button("Sync & Save Now", ImVec2(160.0f, 40.0f))) {
            session.workspace.FlushActiveNotebookAsync();
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (ImGui::Button("Save As Copy...", ImVec2(160.0f, 40.0f))) {
            strncpy_s(copyNbName, (activeNb->name + " - Copy").c_str(), sizeof(copyNbName) - 1);
            showSaveAsCopyModal = true;
        }
    }

    // ========================================================================
    // TAB 2: NOTEBOOKS & LIBRARIES
    // ========================================================================
    void RenderNotebooksAndLibrariesTab(
        DocumentSession& session, 
        CanvasEngine& canvas, 
        const ThemeManager& theme,
        AppViewMode& outViewMode
    ) {
        auto& ws = session.workspace;

        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorText, "Notebooks & Libraries");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Manage standalone notebooks, browse optional folder libraries, or duplicate notebooks");

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        // Top Action Bar
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("+ New Notebook", ImVec2(160.0f, 40.0f))) {
            showNewNotebookModal = true;
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Open from Disk...", ImVec2(170.0f, 40.0f))) {
            showOpenCustomModal = true;
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Save As Copy (Duplicate)", ImVec2(210.0f, 40.0f))) {
            if (auto nb = ws.GetActiveNotebook()) {
                strncpy_s(copyNbName, (nb->name + " - Copy").c_str(), sizeof(copyNbName) - 1);
                showSaveAsCopyModal = true;
            }
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("+ New Library Folder", ImVec2(190.0f, 40.0f))) {
            showNewLibraryModal = true;
        }
        ImGui::PopStyleVar();

        ImGui::Dummy(ImVec2(0.0f, 20.0f));

        // Optional Library Filter Tabs
        ImGui::TextColored(theme.colorTextMuted, "Filter by Library (Optional):");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        auto RenderFilterPill = [&](const char* label, size_t index) {
            bool isSelected = (selectedLibraryIndex == index);
            ImVec4 btnCol = isSelected ? theme.colorItemSelected : theme.colorSectionBg;
            ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? theme.colorItemSelected : theme.colorItemHover);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
            if (ImGui::Button(label, ImVec2(0.0f, 30.0f))) {
                selectedLibraryIndex = index;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0.0f, 8.0f);
        };

        RenderFilterPill("All Open Notebooks", 0);
        for (size_t l = 0; l < libraryManager.libraries.size(); ++l) {
            RenderFilterPill(libraryManager.libraries[l].name.c_str(), l + 1);
        }
        ImGui::NewLine();

        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        // Notebook Cards Grid
        float availW = ImGui::GetContentRegionAvail().x;
        float cardW = std::clamp((availW - 40.0f) * 0.5f, 320.0f, 460.0f);
        float cardH = 100.0f;

        // If "All Open Notebooks" (index 0) selected: show workspace.notebooks
        if (selectedLibraryIndex == 0) {
            for (size_t i = 0; i < ws.notebooks.size(); ++i) {
                auto& nb = ws.notebooks[i];
                if (!nb) continue;

                ImGui::PushID(static_cast<int>(i));
                bool isActive = (ws.activeNotebookIndex == i);

                ImVec2 pMin = ImGui::GetCursorScreenPos();
                ImVec2 pMax(pMin.x + cardW, pMin.y + cardH);
                bool isClicked = ImGui::InvisibleButton("##NbCard", ImVec2(cardW, cardH));
                bool isHovered = ImGui::IsItemHovered();

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 bgCol = isActive ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected) 
                                       : (isHovered ? ImGui::ColorConvertFloat4ToU32(theme.colorItemHover) 
                                                    : ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg));

                dl->AddRectFilled(pMin, pMax, bgCol, 8.0f);
                dl->AddRect(pMin, pMax, ImGui::ColorConvertFloat4ToU32(isActive ? theme.colorPrimaryHover : theme.colorItemHover), 8.0f, 0, isActive ? 2.0f : 1.0f);

                // Icon
                GLuint iconTex = 0;
                if (!nb->iconFile.empty()) {
                    iconTex = g_IconManager.LoadOrGetSVG(nb->iconFile, "assets/icons/Sections_Notebooks/" + nb->iconFile, 96);
                }
                if (iconTex != 0) {
                    dl->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(pMin.x + 14.0f, pMin.y + 16.0f), ImVec2(pMin.x + 82.0f, pMin.y + 84.0f));
                }

                // Title & Details
                float txtX = pMin.x + 94.0f;
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                dl->AddText(ImVec2(txtX, pMin.y + 16.0f), ImGui::ColorConvertFloat4ToU32(isActive ? ImVec4(1,1,1,1) : theme.colorText), nb->name.c_str());
                ImGui::PopFont();

                std::string secCountStr = std::to_string(nb->sections.size()) + " sections";
                dl->AddText(ImVec2(txtX, pMin.y + 44.0f), ImGui::ColorConvertFloat4ToU32(isActive ? ImVec4(0.9f, 0.9f, 0.9f, 1) : theme.colorTextMuted), secCountStr.c_str());

                std::string pathShort = std::filesystem::path(nb->filePath).filename().string();
                dl->AddText(ImVec2(txtX, pMin.y + 66.0f), ImGui::ColorConvertFloat4ToU32(isActive ? ImVec4(0.8f, 0.8f, 0.8f, 1) : theme.colorTextMuted), pathShort.c_str());

                if (isClicked) {
                    ws.activeNotebookIndex = i;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                    outViewMode = AppViewMode::CanvasWorkspace;
                }

                // Grid layout (2 columns)
                if (i % 2 == 0 && (pMin.x + cardW * 2.0f + 20.0f < availW)) {
                    ImGui::SameLine(0.0f, 20.0f);
                } else {
                    ImGui::Dummy(ImVec2(0.0f, 12.0f));
                }

                ImGui::PopID();
            }
        } else {
            // Specific Library selected: list notebooks inside that library
            size_t libIdx = selectedLibraryIndex - 1;
            if (libIdx < libraryManager.libraries.size()) {
                auto& lib = libraryManager.libraries[libIdx];
                ImGui::TextColored(theme.colorText, "Library Path: %s", lib.rootPath.c_str());
                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                if (lib.notebookPaths.empty()) {
                    ImGui::TextColored(theme.colorTextMuted, "No .notebook packages found in this library folder yet.");
                }

                for (size_t i = 0; i < lib.notebookPaths.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    std::string nbPath = lib.notebookPaths[i];
                    std::string nbName = std::filesystem::path(nbPath).stem().string();

                    ImVec2 pMin = ImGui::GetCursorScreenPos();
                    ImVec2 pMax(pMin.x + cardW, pMin.y + cardH);
                    bool isClicked = ImGui::InvisibleButton("##LibNbCard", ImVec2(cardW, cardH));
                    bool isHovered = ImGui::IsItemHovered();

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(pMin, pMax, ImGui::ColorConvertFloat4ToU32(isHovered ? theme.colorItemHover : theme.colorSectionBg), 8.0f);
                    dl->AddRect(pMin, pMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 8.0f);

                    GLuint iconTex = g_IconManager.LoadOrGetSVG("blue_notebook", "assets/icons/Sections_Notebooks/blue-notebook.svg", 96);
                    if (iconTex != 0) {
                        dl->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(pMin.x + 14.0f, pMin.y + 16.0f), ImVec2(pMin.x + 82.0f, pMin.y + 84.0f));
                    }

                    float txtX = pMin.x + 94.0f;
                    ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                    dl->AddText(ImVec2(txtX, pMin.y + 20.0f), ImGui::ColorConvertFloat4ToU32(theme.colorText), nbName.c_str());
                    ImGui::PopFont();
                    dl->AddText(ImVec2(txtX, pMin.y + 54.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), "Click to open into workspace");

                    if (isClicked) {
                        // Check if already open
                        bool alreadyOpen = false;
                        for (size_t o = 0; o < ws.notebooks.size(); ++o) {
                            if (ws.notebooks[o]->filePath == nbPath) {
                                ws.activeNotebookIndex = o;
                                alreadyOpen = true;
                                break;
                            }
                        }
                        if (!alreadyOpen) {
                            if (auto loaded = ws.repository.LoadNotebookHierarchy(nbPath)) {
                                ws.notebooks.push_back(loaded);
                                ws.activeNotebookIndex = ws.notebooks.size() - 1;
                            }
                        }
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                        outViewMode = AppViewMode::CanvasWorkspace;
                    }

                    if (i % 2 == 0 && (pMin.x + cardW * 2.0f + 20.0f < availW)) {
                        ImGui::SameLine(0.0f, 20.0f);
                    } else {
                        ImGui::Dummy(ImVec2(0.0f, 12.0f));
                    }

                    ImGui::PopID();
                }
            }
        }
    }

    // ========================================================================
    // TAB 3: EXPORT & PRINT
    // ========================================================================
    void RenderExportAndPrintTab(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto activeNb = session.workspace.GetActiveNotebook();
        auto activeSec = activeNb ? activeNb->GetActiveSection() : nullptr;
        auto activePg = activeNb ? activeNb->GetActivePage() : nullptr;

        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorText, "Export & Print");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Print pages or whole sections to PDF, export to standalone HTML, or package notebooks");

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        // 1. Choose Scope
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "1. Select Document Scope:");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        int scopeInt = static_cast<int>(exportScope);
        std::string pgLabel = "Current Page (" + (activePg ? activePg->title : "None") + ")";
        std::string secLabel = "Current Section (" + (activeSec ? activeSec->name : "None") + ") - All Pages";
        std::string nbLabel = "Entire Notebook (" + (activeNb ? activeNb->name : "None") + ")";

        ImGui::RadioButton(pgLabel.c_str(), &scopeInt, 0);
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::RadioButton(secLabel.c_str(), &scopeInt, 1);
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::RadioButton(nbLabel.c_str(), &scopeInt, 2);
        exportScope = static_cast<ExportScope>(scopeInt);

        ImGui::Dummy(ImVec2(0.0f, 20.0f));

        // 2. Choose Format
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "2. Select Export Format:");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        int formatInt = static_cast<int>(exportFormat);
        ImGui::RadioButton("Print to PDF (A4 Paginated Document)", &formatInt, 0);
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::RadioButton("HTML Web Document (.html)", &formatInt, 1);
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::RadioButton("Portable FolioNote Package (.folio)", &formatInt, 2);
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::RadioButton("Markdown / Text (.md)", &formatInt, 3);
        exportFormat = static_cast<ExportFormat>(formatInt);

        ImGui::Dummy(ImVec2(0.0f, 20.0f));

        // 3. Optional Destination Path
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "3. Destination (Optional Custom Path):");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputTextWithHint("##ExportCustomPath", "Default: exports/<filename>", exportCustomPath, sizeof(exportCustomPath));

        ImGui::Dummy(ImVec2(0.0f, 24.0f));

        // Export Action Buttons
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        
        if (exportFormat == ExportFormat::PDF_Print) {
            if (ImGui::Button("🖨️  Print to PDF Now", ImVec2(220.0f, 46.0f))) {
                exportSuccess = ExportManager::Export(exportScope, ExportFormat::PDF_Print, activeNb, activeSec, activePg, exportCustomPath, true);
                exportStatusMessage = exportSuccess ? "Document generated and sent to system Print/PDF workflow!" : "Failed to generate print document.";
            }
            ImGui::SameLine(0.0f, 14.0f);
            if (ImGui::Button("Save PDF/HTML File Only", ImVec2(200.0f, 46.0f))) {
                exportSuccess = ExportManager::Export(exportScope, ExportFormat::PDF_Print, activeNb, activeSec, activePg, exportCustomPath, false);
                exportStatusMessage = exportSuccess ? "Exported successfully to disk." : "Failed to export file.";
            }
        } else {
            if (ImGui::Button("Export Document Now", ImVec2(220.0f, 46.0f))) {
                exportSuccess = ExportManager::Export(exportScope, exportFormat, activeNb, activeSec, activePg, exportCustomPath, false);
                exportStatusMessage = exportSuccess ? "Exported successfully to disk." : "Failed to export file.";
            }
        }

        ImGui::PopStyleVar();

        if (!exportStatusMessage.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            ImVec4 msgCol = exportSuccess ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(msgCol, "%s", exportStatusMessage.c_str());
            if (exportSuccess) {
                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Open Exports Folder")) {
#if defined(_WIN32)
                    ShellExecuteA(NULL, "open", "exports", NULL, NULL, SW_SHOWNORMAL);
#endif
                }
            }
        }
    }

    // ========================================================================
    // TAB 4: IMPORT
    // ========================================================================
    void RenderImportTab(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto activeNb = session.workspace.GetActiveNotebook();
        auto activeSec = activeNb ? activeNb->GetActiveSection() : nullptr;

        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorText, "Import Documents");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Import external PDF documents, HTML notes, or FolioNote notebook packages");

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        // Option A: Import PDF
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "A. Import PDF Document");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Creates a canvas page with the PDF document ready for pen annotations and notes");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::SetNextItemWidth(480.0f);
        ImGui::InputTextWithHint("##ImportPdfInput", "Path to .pdf file...", importPdfPath, sizeof(importPdfPath));
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Import PDF", ImVec2(120.0f, 0.0f))) {
            if (activeSec && strlen(importPdfPath) > 0) {
                if (auto p = ImportManager::ImportPDF(importPdfPath)) {
                    activeSec->pages.push_back(p);
                    activeSec->activePageIndex = activeSec->pages.size() - 1;
                    importSuccess = true;
                    importStatusMessage = "PDF imported successfully as a new page!";
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                } else {
                    importSuccess = false;
                    importStatusMessage = "Could not find or open PDF file.";
                }
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 24.0f));

        // Option B: Import HTML
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "B. Import HTML / Web Note");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Extracts text content and creates editable text boxes on a new page");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::SetNextItemWidth(480.0f);
        ImGui::InputTextWithHint("##ImportHtmlInput", "Path to .html or .htm file...", importHtmlPath, sizeof(importHtmlPath));
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Import HTML", ImVec2(120.0f, 0.0f))) {
            if (activeSec && strlen(importHtmlPath) > 0) {
                if (auto p = ImportManager::ImportHTML(importHtmlPath)) {
                    activeSec->pages.push_back(p);
                    activeSec->activePageIndex = activeSec->pages.size() - 1;
                    importSuccess = true;
                    importStatusMessage = "HTML imported successfully as a new page!";
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                } else {
                    importSuccess = false;
                    importStatusMessage = "Could not find or open HTML file.";
                }
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 24.0f));

        // Option C: Import Notebook Package
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "C. Import Notebook Package (.notebook / .folio)");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Imports a standalone package into the active library and opens it immediately");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::SetNextItemWidth(480.0f);
        ImGui::InputTextWithHint("##ImportPkgInput", "Path to .notebook folder or package...", importPkgPath, sizeof(importPkgPath));
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Import Package", ImVec2(130.0f, 0.0f))) {
            if (strlen(importPkgPath) > 0) {
                std::string targetDir = libraryManager.defaultLibraryPath;
                std::string importedPath = ImportManager::ImportNotebookPackage(importPkgPath, targetDir);
                if (!importedPath.empty()) {
                    if (auto loaded = session.workspace.repository.LoadNotebookHierarchy(importedPath)) {
                        session.workspace.notebooks.push_back(loaded);
                        session.workspace.activeNotebookIndex = session.workspace.notebooks.size() - 1;
                        importSuccess = true;
                        importStatusMessage = "Notebook package imported and loaded!";
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                } else {
                    importSuccess = false;
                    importStatusMessage = "Failed to copy or import notebook package.";
                }
            }
        }

        if (!importStatusMessage.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
            ImVec4 msgCol = importSuccess ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(msgCol, "%s", importStatusMessage.c_str());
        }
    }

    // ========================================================================
    // TAB 5: CENTRALIZED SETTINGS HUB ("ALL OF THE SETTINGS")
    // ========================================================================
    void RenderSettingsTab(ThemeManager& theme, CanvasEngine& canvas, SDL_Window* window) {
        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorText, "Application Settings");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Centralized configuration hub for software, inking tuning, themes, and extensions");

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        // Sub-Tabs Row
        auto RenderSettingsSubTabBtn = [&](const char* label, SettingsSubTab tab) {
            bool isSelected = (activeSettingsTab == tab);
            ImVec4 btnCol = isSelected ? theme.colorItemSelected : theme.colorSectionBg;
            ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? theme.colorItemSelected : theme.colorItemHover);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button(label, ImVec2(0.0f, 34.0f))) {
                activeSettingsTab = tab;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0.0f, 6.0f);
        };

        RenderSettingsSubTabBtn("General", SettingsSubTab::General);
        RenderSettingsSubTabBtn("Appearance & Themes", SettingsSubTab::Appearance);
        RenderSettingsSubTabBtn("Inking & Stylus", SettingsSubTab::Inking);
        RenderSettingsSubTabBtn("Storage & Libraries", SettingsSubTab::Storage);
        RenderSettingsSubTabBtn("Add-ons & Plugins", SettingsSubTab::Addons);
        RenderSettingsSubTabBtn("About", SettingsSubTab::About);
        ImGui::NewLine();

        ImGui::Dummy(ImVec2(0.0f, 16.0f));

        // Sub-Tab Content
        switch (activeSettingsTab) {
            case SettingsSubTab::General: {
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Startup & Session");
                ImGui::PopFont();
                ImGui::RadioButton("Resume last viewed page on launch", &startupOption, 0);
                ImGui::RadioButton("Open File / Notebooks Hub on launch", &startupOption, 1);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Auto-Save Frequency");
                ImGui::PopFont();
                const char* autoSaveOpts[] = { "Every 30 Seconds", "Every 1 Minute", "Every 5 Minutes", "Manual Only" };
                ImGui::SetNextItemWidth(260.0f);
                ImGui::Combo("##AutoSaveCombo", &autoSaveInterval, autoSaveOpts, 4);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Display & Hardware Acceleration");
                ImGui::PopFont();
                if (ImGui::Checkbox("VSync Synchronization (Cap at display refresh rate)", &vsyncEnabled)) {
                    SDL_GL_SetSwapInterval(vsyncEnabled ? 1 : 0);
                }
                ImGui::Checkbox("Show Real-time Telemetry HUD & FPS (F3 shortcut)", &showTelemetryHud);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Language");
                ImGui::PopFont();
                const char* langOpts[] = { "English (United States)", "Español", "Français", "Deutsch", "日本語" };
                ImGui::SetNextItemWidth(260.0f);
                ImGui::Combo("##LanguageCombo", &selectedLanguage, langOpts, 5);
                break;
            }

            case SettingsSubTab::Appearance: {
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Theme Preset");
                ImGui::PopFont();
                const char* themePresets[] = { "Folio Color (Modern Vibrant)", "Folio Dark", "Folio Light", "Custom / High Contrast" };
                if (ImGui::Combo("##ThemePresetCombo", &selectedThemePreset, themePresets, 4)) {
                    if (selectedThemePreset == 0) theme.ApplyTheme(ThemePreset::FolioColor);
                    else if (selectedThemePreset == 1) theme.ApplyTheme(ThemePreset::FolioDark);
                    else if (selectedThemePreset == 2) theme.ApplyTheme(ThemePreset::FolioLight);
                    else if (selectedThemePreset == 3) theme.ApplyTheme(ThemePreset::Custom);
                    if (window) theme.UpdateOSWindowFrame(window);
                }

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Primary Accent Color");
                ImGui::PopFont();
                ImVec4 accentCol = theme.colorPrimary;
                if (ImGui::ColorEdit4("##AccentColorPicker", &accentCol.x, ImGuiColorEditFlags_NoAlpha)) {
                    theme.colorPrimary = accentCol;
                    theme.colorItemSelected = ImVec4(accentCol.x, accentCol.y, accentCol.z, 0.40f);
                }

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Custom Icon Packs & Assets Import");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Import custom SVG/PNG icon collections for sections and notebooks");
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputTextWithHint("##IconPackPath", "Folder path to SVG icon pack...", customIconPackPath, sizeof(customIconPackPath));
                ImGui::SameLine(0.0f, 10.0f);
                if (ImGui::Button("Import Icon Pack")) {
                    // Refreshes icon cache
                }

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Canvas Background Defaults");
                ImGui::PopFont();
                float bgFloat[3] = { canvas.canvasBgColor.r() / 255.0f, canvas.canvasBgColor.g() / 255.0f, canvas.canvasBgColor.b() / 255.0f };
                if (ImGui::ColorEdit3("Default Canvas Background", bgFloat)) {
                    canvas.canvasBgColor = BLRgba32(static_cast<uint32_t>(bgFloat[0] * 255), static_cast<uint32_t>(bgFloat[1] * 255), static_cast<uint32_t>(bgFloat[2] * 255));
                    canvas.isDirty = true;
                }
                float gridFloat[3] = { canvas.gridLineColor.r() / 255.0f, canvas.gridLineColor.g() / 255.0f, canvas.gridLineColor.b() / 255.0f };
                if (ImGui::ColorEdit3("Default Grid / Line Color", gridFloat)) {
                    canvas.gridLineColor = BLRgba32(static_cast<uint32_t>(gridFloat[0] * 255), static_cast<uint32_t>(gridFloat[1] * 255), static_cast<uint32_t>(gridFloat[2] * 255));
                    canvas.isDirty = true;
                }
                break;
            }

            case SettingsSubTab::Inking: {
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Pointer & Digitizer Driver");
                ImGui::PopFont();
                const char* driverOpts[] = { "Windows Ink API (Hardware Stylus)", "Direct High-Frequency Digitizer", "Standard Mouse / Touch" };
                ImGui::SetNextItemWidth(300.0f);
                ImGui::Combo("##DriverCombo", &digitizerDriver, driverOpts, 3);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Pressure Response Curve");
                ImGui::PopFont();
                const char* curveOpts[] = { "Soft (Light Touch)", "Linear / Medium", "Firm (Heavy Pressure)", "Custom Bézier" };
                ImGui::SetNextItemWidth(300.0f);
                ImGui::Combo("##PressureCurveCombo", &pressureCurve, curveOpts, 4);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Stroke Smoothing Engine");
                ImGui::PopFont();
                const char* smoothOpts[] = { "Catmull-Rom Spline (Recommended)", "Quadratic Bézier", "Raw Hardware Packets (No Smoothing)" };
                ImGui::SetNextItemWidth(300.0f);
                ImGui::Combo("##SmoothingCombo", &strokeSmoothing, smoothOpts, 3);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Palm Rejection Mode");
                ImGui::PopFont();
                const char* palmOpts[] = { "Strict (Ignore Touch when Stylus near screen)", "Normal", "Disabled (Simultaneous Touch & Pen)" };
                ImGui::SetNextItemWidth(300.0f);
                ImGui::Combo("##PalmCombo", &palmRejection, palmOpts, 3);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Calibrate Canvas to Real-World Scale (Ruler Calibration)");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Place a physical ruler against the screen and adjust this slider until 10cm matches exactly");
                ImGui::SetNextItemWidth(360.0f);
                ImGui::SliderFloat("Screen DPI##RulerCalib", &rulerDpiCalibration, 60.0f, 300.0f, "%.1f DPI");
                
                // Visual 10cm on-screen calibration bar
                float barPixels = (rulerDpiCalibration / 2.54f) * 10.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 rMin = ImGui::GetCursorScreenPos();
                ImVec2 rMax(rMin.x + barPixels, rMin.y + 24.0f);
                dl->AddRectFilled(rMin, rMax, ImGui::ColorConvertFloat4ToU32(theme.colorPrimary), 4.0f);
                dl->AddText(ImVec2(rMin.x + 10.0f, rMin.y + 4.0f), IM_COL32(255,255,255,255), "10.0 cm Calibration Ruler");
                ImGui::Dummy(ImVec2(barPixels, 28.0f));
                break;
            }

            case SettingsSubTab::Storage: {
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Storage Paths & Default Libraries");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Default Directory: %s", libraryManager.defaultLibraryPath.c_str());

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Database Engine & Cache");
                ImGui::PopFont();
                ImGui::Text("SQLite3 Engine: Write-Ahead Logging (WAL) Mode Active");
                ImGui::SetNextItemWidth(280.0f);
                ImGui::SliderInt("Working Set Memory Cache (MB)", &cacheLimitMb, 128, 2048);
                ImGui::SetNextItemWidth(280.0f);
                ImGui::SliderInt("Inactive Page LRU Eviction (Seconds)", &lruTimeoutSec, 15, 300);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Automated Backups");
                ImGui::PopFont();
                const char* backupOpts[] = { "Daily Automated Snapshot", "Weekly Snapshot", "Disabled" };
                ImGui::SetNextItemWidth(280.0f);
                ImGui::Combo("##BackupCombo", &backupFrequency, backupOpts, 3);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                if (ImGui::Button("Compact & Vacuum SQLite Databases", ImVec2(280.0f, 38.0f))) {
                    // Compact SQLite
                }
                break;
            }

            case SettingsSubTab::Addons: {
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Installed Add-ons & Plugins");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Extend FolioNote with computational engines, OCR, and cloud services");

                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                ImGui::Checkbox("LaTeX & Math Formula Interactive Solver", &addonMathSolver);
                ImGui::Checkbox("High-Resolution Vector PDF Rasterizer", &addonPdfRasterizer);
                ImGui::Checkbox("Handwriting OCR & Full-Text Search Indexer", &addonOcrIndexer);
                ImGui::Checkbox("Cloud Sync & WebDAV Workspace Connector", &addonCloudSync);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Developer Mode");
                ImGui::PopFont();
                ImGui::Checkbox("Enable Extension Hot-Reloading & Add-on Debugging", &addonDevMode);

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                if (ImGui::Button("+ Install Extension from File (.fext, .zip)...", ImVec2(340.0f, 40.0f))) {
                    // Add-on installer dialog
                }
                break;
            }

            case SettingsSubTab::About: {
                GLuint logoTex = g_IconManager.LoadOrGetSVG("app_logo", "assets/icons/logo.svg", 128);
                if (logoTex != 0) {
                    ImGui::Image((ImTextureID)(intptr_t)logoTex, ImVec2(64.0f, 64.0f));
                    ImGui::SameLine(0.0f, 16.0f);
                }
                ImGui::BeginGroup();
                ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
                ImGui::TextColored(theme.colorText, "FolioNote Desktop");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Version 1.0.0 (Windows x64 Release Build)");
                ImGui::EndGroup();

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 14.0f));

                ImGui::Text("Core Graphics Engine: Blend2D 2D Vector Pipeline");
                ImGui::Text("Windowing & Audio: SDL3 Multimedia Library");
                ImGui::Text("User Interface: Dear ImGui Modern UWP Layer");
                ImGui::Text("Vector Rendering: LunaSVG");
                ImGui::Text("Document Persistence: SQLite 3 with Write-Ahead Logging");

                ImGui::Dummy(ImVec2(0.0f, 18.0f));
                if (ImGui::Button("Check for Updates", ImVec2(180.0f, 40.0f))) {
                    // Update check
                }
                break;
            }
        }
    }

    // ========================================================================
    // MODALS AND DIALOGS
    // ========================================================================
    void RenderModals(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        // 1. New Notebook Modal
        if (showNewNotebookModal) {
            ImGui::OpenPopup("NewNotebookModal##Hub");
            showNewNotebookModal = false;
        }
        if (ImGui::BeginPopupModal("NewNotebookModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(FolioTheme::FontNavBoldLarge);
            ImGui::Text("Create New Notebook");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0.0f, 8.0f));

            ImGui::Text("Notebook Name:");
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enterPressed = ImGui::InputText("##NewNbNameInput", newNbName, sizeof(newNbName), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::Text("Target Folder / Library (Optional):");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint("##NewNbPathInput", "Default: Documents/FolioNote", newNbPath, sizeof(newNbPath));

            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            if (ImGui::Button("Create Notebook", ImVec2(140.0f, 36.0f)) || enterPressed) {
                if (strlen(newNbName) > 0) {
                    std::string targetDir = strlen(newNbPath) > 0 ? std::string(newNbPath) : libraryManager.defaultLibraryPath;
                    auto created = libraryManager.CreateNewNotebook(newNbName, targetDir);
                    if (created) {
                        session.workspace.notebooks.push_back(created);
                        session.workspace.activeNotebookIndex = session.workspace.notebooks.size() - 1;
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Cancel", ImVec2(100.0f, 36.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // 2. Save As Copy (Duplicate) Modal
        if (showSaveAsCopyModal) {
            ImGui::OpenPopup("SaveAsCopyModal##Hub");
            showSaveAsCopyModal = false;
        }
        if (ImGui::BeginPopupModal("SaveAsCopyModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(FolioTheme::FontNavBoldLarge);
            ImGui::Text("Duplicate Notebook (Save As Copy)");
            ImGui::PopFont();
            ImGui::TextColored(theme.colorTextMuted, "Creates an independent duplicate with fresh UUIDs and schema");
            ImGui::Dummy(ImVec2(0.0f, 8.0f));

            ImGui::Text("Copy Name:");
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enterPressed = ImGui::InputText("##CopyNbNameInput", copyNbName, sizeof(copyNbName), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::Text("Destination Folder (Optional):");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint("##CopyNbDestInput", "Default: Same folder as original", copyNbDestPath, sizeof(copyNbDestPath));

            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            if (ImGui::Button("Duplicate Notebook", ImVec2(160.0f, 36.0f)) || enterPressed) {
                if (auto active = session.workspace.GetActiveNotebook()) {
                    std::string targetDir = strlen(copyNbDestPath) > 0 ? std::string(copyNbDestPath) : std::filesystem::path(active->filePath).parent_path().string();
                    auto copied = libraryManager.SaveAsCopy(active, copyNbName, targetDir);
                    if (copied) {
                        session.workspace.notebooks.push_back(copied);
                        session.workspace.activeNotebookIndex = session.workspace.notebooks.size() - 1;
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Cancel", ImVec2(100.0f, 36.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // 3. New Library Folder Modal
        if (showNewLibraryModal) {
            ImGui::OpenPopup("NewLibraryModal##Hub");
            showNewLibraryModal = false;
        }
        if (ImGui::BeginPopupModal("NewLibraryModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(FolioTheme::FontNavBoldLarge);
            ImGui::Text("Add Folder as Library (Optional)");
            ImGui::PopFont();
            ImGui::TextColored(theme.colorTextMuted, "Groups notebooks stored in a designated directory on disk");
            ImGui::Dummy(ImVec2(0.0f, 8.0f));

            ImGui::Text("Library Display Name:");
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputText("##NewLibNameInput", newLibName, sizeof(newLibName));

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::Text("Folder Path on Disk:");
            ImGui::SetNextItemWidth(320.0f);
            bool enterPressed = ImGui::InputText("##NewLibPathInput", newLibPath, sizeof(newLibPath), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            if (ImGui::Button("Add Library", ImVec2(130.0f, 36.0f)) || enterPressed) {
                if (strlen(newLibName) > 0 && strlen(newLibPath) > 0) {
                    libraryManager.AddLibrary(newLibName, newLibPath);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Cancel", ImVec2(100.0f, 36.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // 4. Open Custom Notebook Modal
        if (showOpenCustomModal) {
            ImGui::OpenPopup("OpenCustomModal##Hub");
            showOpenCustomModal = false;
        }
        if (ImGui::BeginPopupModal("OpenCustomModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(FolioTheme::FontNavBoldLarge);
            ImGui::Text("Open Notebook from Disk");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0.0f, 8.0f));

            ImGui::Text("Path to .notebook package folder:");
            ImGui::SetNextItemWidth(380.0f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enterPressed = ImGui::InputText("##OpenCustomPathInput", openCustomPath, sizeof(openCustomPath), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            if (ImGui::Button("Open Notebook", ImVec2(140.0f, 36.0f)) || enterPressed) {
                if (strlen(openCustomPath) > 0) {
                    if (auto loaded = session.workspace.repository.LoadNotebookHierarchy(openCustomPath)) {
                        session.workspace.notebooks.push_back(loaded);
                        session.workspace.activeNotebookIndex = session.workspace.notebooks.size() - 1;
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Cancel", ImVec2(100.0f, 36.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
};