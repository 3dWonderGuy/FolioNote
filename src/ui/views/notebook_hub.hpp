#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <chrono>
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
#include <shobjidl.h>

inline std::string ShowNativeFolderPicker(const std::string& title = "Select Folder or Notebook Package") {
    std::string resultPath;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool coInited = (hr == S_OK || hr == S_FALSE);

    IFileOpenDialog* pFileOpen = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr) && pFileOpen) {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
        std::wstring wTitle(title.begin(), title.end());
        pFileOpen->SetTitle(wTitle.c_str());

        hr = pFileOpen->Show(NULL);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr) && pItem) {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr) && pszFilePath) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                    if (len > 1) {
                        resultPath.resize(len - 1);
                        WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &resultPath[0], len, NULL, NULL);
                    }
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    if (coInited) {
        CoUninitialize();
    }
    return resultPath;
}
#else
inline std::string ShowNativeFolderPicker(const std::string& /*title*/ = "") {
    return "";
}
#endif

// ============================================================================
// ENUMS FOR PRIMARY AND SECONDARY NAVIGATION
// ============================================================================

enum class HubMainCategory {
    NotebookManager = 0,
    NotebookInfo = 1,
    ExportAndPrint = 2,
    Settings = 3
};

enum class NbManagerSubTab {
    AllNotebooks = 0,
    LibraryFolders = 1,
    ImportAndMigration = 2,
    CreateNotebook = 3
};

enum class InfoSubTab {
    Overview = 0
};

enum class ExportSubTab {
    Export = 0
};

enum class SettingsSubCategory {
    General = 0,
    Appearance = 1,
    Inking = 2,
    Storage = 3,
    Addons = 4,
    About = 5
};

// ============================================================================
// NOTEBOOK HUB VIEW (FILE / BACKSTAGE REDESIGNED)
// ============================================================================

struct NotebookHubView {
    HubMainCategory activeMainCategory = HubMainCategory::NotebookManager;
    NbManagerSubTab activeNbSubTab = NbManagerSubTab::AllNotebooks;
    InfoSubTab activeInfoSubTab = InfoSubTab::Overview;
    ExportSubTab activeExportSubTab = ExportSubTab::Export;
    SettingsSubCategory activeSettingsSubCategory = SettingsSubCategory::General;

    LibraryManager libraryManager;
    bool isInitialized = false;

    // Search & Sorting state in Notebook Manager
    char nbSearchQuery[128] = "";
    int nbSortMode = 0; // 0=Recently Opened, 1=Title A-Z, 2=Section Count, 3=Disk Size

    // Modals & Dialog states
    bool showNewNotebookModal = false;
    bool showSaveAsCopyModal = false;
    bool showNewLibraryModal = false;
    bool showOpenCustomModal = false;
    bool showRenameNotebookModal = false;
    int renameNotebookIndex = -1;
    char renameNotebookBuffer[128] = "";

    // New Notebook inputs
    char newNbName[128] = "My Notebook";
    char newNbPath[256] = "";
    int newNbColorIndex = 0; // 0=Azure, 1=Emerald, 2=Purple, 3=Sunset, 4=Crimson, 5=Slate
    int newNbTemplateIndex = 0; // 0=Blank, 1=Ruled, 2=Engineering Grid, 3=Dot Grid, 4=Cornell Notes

    // Copy / Duplicate inputs
    char copyNbName[128] = "";
    char copyNbDestPath[256] = "";

    // Library inputs
    char newLibName[128] = "Research Library";
    char newLibPath[256] = "";
    char openCustomPath[256] = "";

    // Import & Migration state (OneNote, FolioNote, Library)
    char importOneNotePath[256] = "";
    std::string oneNoteStatusMessage = "";
    bool oneNoteSuccess = false;

    char importPkgPath[256] = "";
    std::string pkgStatusMessage = "";
    bool pkgSuccess = false;

    char importLibFolder[256] = "";
    char importLibCustomName[128] = "";
    std::string libStatusMessage = "";
    bool libSuccess = false;

    // Export & Print state
    ExportScope exportScope = ExportScope::CurrentPage;
    ExportFormat exportFormat = ExportFormat::PDF_Print;
    char exportCustomPath[256] = "";
    std::string exportStatusMessage = "";
    bool exportSuccess = false;

    // Settings state
    int startupOption = 0;
    int autoSaveInterval = 1;
    bool vsyncEnabled = true;
    bool showTelemetryHud = false;
    int selectedLanguage = 0;

    int selectedThemePreset = 0;
    char customIconPackPath[256] = "";

    int digitizerDriver = 0;
    int pressureCurve = 1;
    int strokeSmoothing = 0;
    int palmRejection = 1;
    float rulerDpiCalibration = 96.0f;

    int cacheLimitMb = 512;
    int lruTimeoutSec = 60;
    int backupFrequency = 1;

    bool addonMathSolver = true;
    bool addonPdfRasterizer = true;
    bool addonOcrIndexer = false;
    bool addonCloudSync = false;
    bool addonDevMode = false;

    size_t selectedLibraryIndex = 0; // 0 = All Open / Standalone, 1+ = specific library
    int newNbLocationType = 0;       // 0 = Add to Library, 1 = Standalone Notebook
    int newNbSelectedLibIdx = 0;     // Selected library index for creation

    struct CardItem {
        size_t workspaceIndex;
        std::string name;
        std::string path;
        std::string icon;
        size_t sections;
        size_t pages;
        bool isActive;
    };

    void OpenDiskPath(const std::string& picked, Workspace& ws, CanvasEngine& canvas) {
        if (picked.empty()) return;
        std::error_code ec;
        std::filesystem::path p(picked);
        if (!std::filesystem::exists(p, ec)) return;

        if (p.extension() == ".notebook" || std::filesystem::exists(p / "metadata.json", ec) || std::filesystem::exists(p / "pages.db", ec)) {
            // Check if already open
            for (size_t i = 0; i < ws.notebooks.size(); ++i) {
                if (std::filesystem::equivalent(std::filesystem::path(ws.notebooks[i]->filePath), p, ec)) {
                    ws.activeNotebookIndex = i;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                    return;
                }
            }
            if (auto nb = ws.repository.LoadNotebookHierarchy(p.string())) {
                ws.notebooks.push_back(nb);
                ws.activeNotebookIndex = ws.notebooks.size() - 1;
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            }
        } else if (std::filesystem::is_directory(p, ec)) {
            // Register as library
            std::string libName = p.filename().string();
            if (libName.empty()) libName = "Local Library";
            libraryManager.AddLibrary(libName, p.string());
            libraryManager.RefreshAll();
            for (const auto& lib : libraryManager.libraries) {
                if (std::filesystem::equivalent(std::filesystem::path(lib.rootPath), p, ec)) {
                    for (const auto& nbPath : lib.notebookPaths) {
                        bool found = false;
                        for (const auto& existing : ws.notebooks) {
                            if (std::filesystem::equivalent(std::filesystem::path(existing->filePath), std::filesystem::path(nbPath), ec)) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            if (auto nb = ws.repository.LoadNotebookHierarchy(nbPath)) {
                                ws.notebooks.push_back(nb);
                            }
                        }
                    }
                    break;
                }
            }
            canvas.needsFullRebake = true;
            canvas.isDirty = true;
        }
    }

    void RenderNotebookCard(
        const CardItem& item, 
        float cardW, 
        float cardH, 
        const ThemeManager& theme, 
        Workspace& ws, 
        CanvasEngine& canvas, 
        AppViewMode& outViewMode
    ) {
        ImGui::PushID(static_cast<int>(item.workspaceIndex));
        std::string childId = "##NbCardChild_" + std::to_string(item.workspaceIndex);

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f); // Clean square corners
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, item.isActive ? 2.0f : 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, item.isActive ? theme.colorItemSelected : theme.colorPanel);
        ImGui::PushStyleColor(ImGuiCol_Border, item.isActive ? ImVec4(0.12f, 0.12f, 0.12f, 1.0f) : theme.colorBorder);

        ImGui::BeginChild(childId.c_str(), ImVec2(cardW, cardH), true, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cardMin = ImGui::GetWindowPos();
        ImVec2 cardMax(cardMin.x + cardW, cardMin.y + cardH);

        // Icon
        GLuint iconTex = 0;
        if (!item.icon.empty()) {
            iconTex = g_IconManager.LoadOrGetSVG(item.icon, "assets/icons/Sections_Notebooks/" + item.icon, 96);
        }
        if (iconTex != 0) {
            dl->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(cardMin.x + 14.0f, cardMin.y + 16.0f), ImVec2(cardMin.x + 86.0f, cardMin.y + 88.0f));
        }

        float txtX = 96.0f;

        // Notebook title (strictly black when active)
        ImGui::SetCursorPos(ImVec2(txtX, 14.0f));
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(item.isActive ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : theme.colorText, "%s", item.name.c_str());
        ImGui::PopFont();

        // Metadata count
        ImGui::SetCursorPos(ImVec2(txtX, 40.0f));
        ImGui::TextColored(item.isActive ? ImVec4(0.15f, 0.15f, 0.15f, 1.0f) : theme.colorTextMuted, 
            "%zu sections  •  %zu pages", item.sections, item.pages);

        // Path
        std::string shortPath = std::filesystem::path(item.path).filename().string();
        if (shortPath.empty()) shortPath = "(In Memory)";
        ImGui::SetCursorPos(ImVec2(txtX, 60.0f));
        ImGui::TextColored(item.isActive ? ImVec4(0.20f, 0.20f, 0.20f, 1.0f) : theme.colorTextMuted, "%s", shortPath.c_str());

        // Active badge
        if (item.isActive) {
            dl->AddRectFilled(ImVec2(cardMax.x - 76.0f, cardMin.y + 12.0f), ImVec2(cardMax.x - 12.0f, cardMin.y + 32.0f), IM_COL32(35, 140, 60, 240), 0.0f);
            dl->AddText(ImVec2(cardMax.x - 68.0f, cardMin.y + 14.0f), IM_COL32(255, 255, 255, 255), "ACTIVE");
        }

        // Clean bottom-aligned action row: Open, Rename, Explorer (Copy removed)
        ImGui::SetCursorPos(ImVec2(txtX, cardH - 36.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f); // Moderate rounded corners
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 4.0f));

        if (ImGui::Button("Open##NbCardOpen", ImVec2(68.0f, 26.0f))) {
            ws.activeNotebookIndex = item.workspaceIndex;
            canvas.needsFullRebake = true;
            canvas.isDirty = true;
            outViewMode = AppViewMode::CanvasWorkspace;
        }
        if (ImGui::IsItemActive()) {
            dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 6.0f, 0, 1.5f);
        }

        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Rename##NbCardRename", ImVec2(76.0f, 26.0f))) {
            renameNotebookIndex = static_cast<int>(item.workspaceIndex);
            strncpy_s(renameNotebookBuffer, item.name.c_str(), sizeof(renameNotebookBuffer) - 1);
            showRenameNotebookModal = true;
        }
        if (ImGui::IsItemActive()) {
            dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 6.0f, 0, 1.5f);
        }

        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Explorer##NbCardExp", ImVec2(80.0f, 26.0f))) {
#if defined(_WIN32)
            if (!item.path.empty()) {
                ShellExecuteA(NULL, "open", item.path.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
#endif
        }
        if (ImGui::IsItemActive()) {
            dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 6.0f, 0, 1.5f);
        }

        ImGui::PopStyleVar(2); // FrameRounding, FramePadding

        ImGui::EndChild();
        ImGui::PopStyleColor(2); // ChildBg, Border
        ImGui::PopStyleVar(3);   // ChildRounding, WindowPadding, ChildBorderSize
        ImGui::PopID();
    }

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
        std::string defaultPath = session.workspace.workspaceDirectory;
        if (defaultPath.empty()) {
            defaultPath = session.workspace.repository.currentPackagePath;
        }
        InitIfNeeded(defaultPath);

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(width, height));
        // Main backstage panel: zero rounding to prevent exposing orange background at edges
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.colorPanel);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

        ImGui::Begin("##BackstageFileView", nullptr, 
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

        float topBarHeight = 92.0f;
        float bodyHeight = height - topBarHeight;
        float subSidebarWidth = 260.0f;
        float contentWidth = width - subSidebarWidth;

        // 1. TOP SPACIOUS HEADER BAR (Custom Navigation Bar)
        RenderTopHeaderBar(width, topBarHeight, outViewMode, theme, session);

        // 2. SUB-SETTINGS / SUB-CATEGORY SIDEBAR (Left)
        ImGui::SetCursorPos(ImVec2(0.0f, topBarHeight));
        RenderSubSidebar(subSidebarWidth, bodyHeight, topBarHeight, theme);

        ImGui::SameLine(0.0f, 0.0f);

        // 3. MAIN CONTENT PANE (Right)
        ImGui::SetCursorPos(ImVec2(subSidebarWidth, topBarHeight));
        RenderContentPane(contentWidth, bodyHeight, session, canvas, theme, outViewMode, window);

        // 4. MODALS AND DIALOGS
        RenderModals(session, canvas, theme);

        ImGui::End();
        ImGui::PopStyleVar(5);
        ImGui::PopStyleColor();
    }

private:
    // ========================================================================
    // 1. TOP SPACIOUS HEADER BAR
    // ========================================================================
    void RenderTopHeaderBar(
        float width, 
        float height, 
        AppViewMode& outViewMode, 
        const ThemeManager& theme, 
        DocumentSession& session
    ) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorNavBg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::BeginChild("##BackstageTopHeader", ImVec2(width, height), false, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // --- ROW 1: Return Button (Left) | Centered "FolioNote Hub" (Center) | Active Badge (Right) ---
        
        // A. Return to Canvas Button
        ImGui::SetCursorPos(ImVec2(24.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPanel);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorItemHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f); // Clean square corners
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 6.0f));

        if (ImGui::Button("← Back to Canvas##TopBackBtn", ImVec2(160.0f, 32.0f))) {
            outViewMode = AppViewMode::CanvasWorkspace;
        }
        if (ImGui::IsItemActive()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 0.0f, 0, 1.5f);
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        // B. Hub Title (Strictly Centered horizontally across the window)
        const char* hubTitle = "FolioNote Hub";
        ImFont* hubFont = FolioTheme::FontRibbonBoldLarge ? FolioTheme::FontRibbonBoldLarge : FolioTheme::FontBold;
        if (hubFont) ImGui::PushFont(hubFont);
        ImVec2 titleSize = ImGui::CalcTextSize(hubTitle);
        if (hubFont) ImGui::PopFont();

        float centerX = (width - titleSize.x) * 0.5f;
        ImGui::SetCursorPos(ImVec2(centerX, 10.0f));
        if (hubFont) ImGui::PushFont(hubFont);
        ImGui::TextColored(theme.colorText, "%s", hubTitle);
        if (hubFont) ImGui::PopFont();

        // C. Right-Side Active Notebook Badge
        auto activeNb = session.workspace.GetActiveNotebook();
        if (activeNb) {
            std::string badgeStr = "Notebook: " + activeNb->name;
            ImVec2 tSize = ImGui::CalcTextSize(badgeStr.c_str());
            float rightX = width - tSize.x - 24.0f;
            if (rightX > centerX + titleSize.x + 16.0f) {
                ImGui::SetCursorPos(ImVec2(rightX, 15.0f));
                ImGui::TextColored(theme.colorTextMuted, "%s", badgeStr.c_str());
            }
        }

        // --- ROW 2: Primary Category Tabs (Centered horizontally below Hub title) ---
        struct HeaderTabDef {
            const char* label;
            HubMainCategory cat;
            const char* iconKey;
            const char* iconPath;
        };

        HeaderTabDef tabs[4] = {
            { "Notebook Manager", HubMainCategory::NotebookManager, "icon_notebooks", "assets/icons/Sections_Notebooks/blue-notebook.svg" },
            { "Info & Health", HubMainCategory::NotebookInfo, "icon_info", "assets/icons/Navigation/view.svg" },
            { "Export & Print", HubMainCategory::ExportAndPrint, "icon_export", "assets/icons/Navigation/add.svg" },
            { "Settings & Preferences", HubMainCategory::Settings, "icon_settings", "assets/icons/Navigation/settings.svg" }
        };

        float tabGap = 12.0f;
        float totalTabsW = 0.0f;
        float tabWidths[4];
        for (int i = 0; i < 4; ++i) {
            ImVec2 sz = ImGui::CalcTextSize(tabs[i].label);
            tabWidths[i] = sz.x + 44.0f; // 22px padding on each side
            totalTabsW += tabWidths[i];
        }
        totalTabsW += tabGap * 3.0f;

        float tabsStartX = (width - totalTabsW) * 0.5f;
        if (tabsStartX < 24.0f) tabsStartX = 24.0f; // Guard on narrower screens

        float curTabX = tabsStartX;
        float tabY = 48.0f;
        float tabHeight = 34.0f;

        for (int i = 0; i < 4; ++i) {
            const auto& tab = tabs[i];
            bool isSelected = (activeMainCategory == tab.cat);
            ImVec4 btnCol = isSelected ? theme.colorItemSelected : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            ImVec4 textCol = isSelected ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : theme.colorText;

            ImGui::SetCursorPos(ImVec2(curTabX, tabY));
            ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? theme.colorItemSelected : theme.colorItemHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f); // Clean square corners
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(22.0f, 6.0f));

            std::string id = std::string(tab.label) + "##TopCatBtn";
            if (ImGui::Button(id.c_str(), ImVec2(tabWidths[i], tabHeight))) {
                activeMainCategory = tab.cat;
            }

            ImVec2 bMin = ImGui::GetItemRectMin();
            ImVec2 bMax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (isSelected || ImGui::IsItemActive()) {
                // Crisp dark square outline on selection and click
                dl->AddRect(bMin, bMax, IM_COL32(30, 30, 30, 240), 0.0f, 0, 1.5f);
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);

            curTabX += tabWidths[i] + tabGap;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor();

        // Dividing border below the top header
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        dl->AddLine(
            ImVec2(winPos.x, winPos.y + height),
            ImVec2(winPos.x + width, winPos.y + height),
            ImGui::ColorConvertFloat4ToU32(theme.colorBorder),
            1.0f
        );
    }

    // ========================================================================
    // 2. SUB-SETTINGS / SUB-CATEGORY SIDEBAR
    // ========================================================================
    void RenderSubSidebar(float width, float height, float topBarOffset, const ThemeManager& theme) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorNavBg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::BeginChild("##BackstageSubSidebar", ImVec2(width, height), false, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::Dummy(ImVec2(0.0f, 20.0f)); // Comfortable top margin
 
        // Sub-sidebar item renderer
        // CRITICAL REQUIREMENT: "Keep text black upon selection in the side bar"
        auto RenderSubRailTab = [&](const char* label, bool isSelected, auto onSelect, const char* iconKey = nullptr, const char* iconPath = nullptr) {
            ImVec2 itemSize(width - 32.0f, 44.0f);
            ImGui::SetCursorPosX(16.0f);

            ImVec4 bgCol = isSelected ? theme.colorItemSelected : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            // Selected text is strictly black for high-contrast visibility:
            ImVec4 textCol = isSelected ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : theme.colorText;

            ImGui::PushStyleColor(ImGuiCol_Button, bgCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? theme.colorItemSelected : theme.colorItemHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f); // Clean square corners

            std::string btnId = std::string("##SubTab_") + label;
            if (ImGui::Button(btnId.c_str(), itemSize)) {
                onSelect();
            }

            ImVec2 bMin = ImGui::GetItemRectMin();
            ImVec2 bMax = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();

            if (isSelected || ImGui::IsItemActive()) {
                // Crisp dark square outline on selection and click
                draw->AddRect(bMin, bMax, IM_COL32(30, 30, 30, 240), 0.0f, 0, 1.5f);
            }

            float textStartX = bMin.x + 16.0f;
            if (iconKey && iconPath) {
                GLuint iconTex = g_IconManager.LoadOrGetSVG(iconKey, iconPath, 48, false);
                if (iconTex != 0) {
                    draw->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(textStartX, bMin.y + 11.0f), ImVec2(textStartX + 22.0f, bMin.y + 33.0f));
                    textStartX += 32.0f;
                }
            }

            ImGui::PushFont(FolioTheme::FontNavBoldLarge);
            draw->AddText(ImVec2(textStartX, bMin.y + 12.0f), ImGui::ColorConvertFloat4ToU32(textCol), label);
            ImGui::PopFont();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        };

        // Render sub-items based on active primary category
        switch (activeMainCategory) {
            case HubMainCategory::NotebookManager:
                RenderSubRailTab("All Notebooks", activeNbSubTab == NbManagerSubTab::AllNotebooks, [&](){ activeNbSubTab = NbManagerSubTab::AllNotebooks; }, "icon_tab_all", "assets/icons/Navigation/expand.svg");
                RenderSubRailTab("Library Folders", activeNbSubTab == NbManagerSubTab::LibraryFolders, [&](){ activeNbSubTab = NbManagerSubTab::LibraryFolders; }, "icon_tab_lib", "assets/icons/Sections_Notebooks/purple-notebook.svg");
                RenderSubRailTab("Import & Migration", activeNbSubTab == NbManagerSubTab::ImportAndMigration, [&](){ activeNbSubTab = NbManagerSubTab::ImportAndMigration; }, "icon_tab_imp", "assets/icons/Tools/pencil.svg");
                RenderSubRailTab("+ New Notebook", activeNbSubTab == NbManagerSubTab::CreateNotebook, [&](){ activeNbSubTab = NbManagerSubTab::CreateNotebook; }, "icon_tab_new", "assets/icons/Navigation/add.svg");
                break;

            case HubMainCategory::NotebookInfo:
                RenderSubRailTab("Overview", true, [&](){ activeInfoSubTab = InfoSubTab::Overview; }, "icon_i_over", "assets/icons/Sections_Notebooks/blue-notebook.svg");
                break;

            case HubMainCategory::ExportAndPrint:
                RenderSubRailTab("Export & Print", true, [&](){ activeExportSubTab = ExportSubTab::Export; }, "icon_prt", "assets/icons/Insert/pdf.svg");
                break;

            case HubMainCategory::Settings:
                RenderSubRailTab("General & Session", activeSettingsSubCategory == SettingsSubCategory::General, [&](){ activeSettingsSubCategory = SettingsSubCategory::General; }, "icon_s_gen", "assets/icons/Tools/settings.svg");
                RenderSubRailTab("Appearance & Themes", activeSettingsSubCategory == SettingsSubCategory::Appearance, [&](){ activeSettingsSubCategory = SettingsSubCategory::Appearance; }, "icon_s_app", "assets/icons/Sections_Notebooks/purple-notebook.svg");
                RenderSubRailTab("Inking & Stylus", activeSettingsSubCategory == SettingsSubCategory::Inking, [&](){ activeSettingsSubCategory = SettingsSubCategory::Inking; }, "icon_s_ink", "assets/icons/Tools/brush.svg");
                RenderSubRailTab("Storage & Database", activeSettingsSubCategory == SettingsSubCategory::Storage, [&](){ activeSettingsSubCategory = SettingsSubCategory::Storage; }, "icon_s_sto", "assets/icons/Sections_Notebooks/blue-notebook.svg");
                RenderSubRailTab("Add-ons & Plugins", activeSettingsSubCategory == SettingsSubCategory::Addons, [&](){ activeSettingsSubCategory = SettingsSubCategory::Addons; }, "icon_s_add", "assets/icons/Navigation/add.svg");
                RenderSubRailTab("About & Updates", activeSettingsSubCategory == SettingsSubCategory::About, [&](){ activeSettingsSubCategory = SettingsSubCategory::About; }, "icon_s_abo", "assets/icons/logo.svg");
                break;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor();

        // 1px Vertical divider border
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        dl->AddLine(
            ImVec2(winPos.x + width, winPos.y + topBarOffset),
            ImVec2(winPos.x + width, winPos.y + topBarOffset + height),
            ImGui::ColorConvertFloat4ToU32(theme.colorBorder),
            1.0f
        );
    }

    // ========================================================================
    // 3. MAIN CONTENT PANE
    // ========================================================================
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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40.0f, 32.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 16.0f));
        ImGui::BeginChild("##HubContent", ImVec2(contentWidth, contentHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        switch (activeMainCategory) {
            case HubMainCategory::NotebookManager:
                RenderNotebookManagerContent(session, canvas, theme, outViewMode);
                break;
            case HubMainCategory::NotebookInfo:
                RenderNotebookInfoContent(session, canvas, theme);
                break;
            case HubMainCategory::ExportAndPrint:
                RenderExportContent(session, canvas, theme);
                break;
            case HubMainCategory::Settings:
                RenderSettingsContent(theme, canvas, window);
                break;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(6);
        ImGui::PopStyleColor();
    }

    // ========================================================================
    // CATEGORY 1: NOTEBOOK MANAGER CONTENT
    // ========================================================================
    void RenderNotebookManagerContent(
        DocumentSession& session, 
        CanvasEngine& canvas, 
        const ThemeManager& theme, 
        AppViewMode& outViewMode
    ) {
        auto& ws = session.workspace;

        switch (activeNbSubTab) {
            // ----------------------------------------------------------------
            // SUB-TAB 1: ALL OPEN NOTEBOOKS
            // ----------------------------------------------------------------
            case NbManagerSubTab::AllNotebooks: {
                ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
                ImGui::TextColored(theme.colorText, "Notebook Manager");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Search, inspect, organize, and switch between your standalone notebooks and library packages");

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 14.0f));

                // Search Bar + Sort Selector + Top Action Buttons (Moderate rounded corners, spacious margins)
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f); // Moderate rounded corners
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 9.0f));

                ImGui::SetNextItemWidth(300.0f);
                ImGui::InputTextWithHint("##SearchNotebooksInput", "🔍 Search notebooks...", nbSearchQuery, sizeof(nbSearchQuery));

                ImGui::SameLine(0.0f, 16.0f);
                const char* sortOptions[] = { "Recently Opened", "Title (A-Z)", "Most Sections", "Disk Usage" };
                ImGui::SetNextItemWidth(170.0f);
                ImGui::Combo("##SortNotebooksCombo", &nbSortMode, sortOptions, 4);

                ImGui::SameLine(0.0f, 20.0f);
                if (ImGui::Button("+ New Notebook", ImVec2(154.0f, 40.0f))) {
                    activeNbSubTab = NbManagerSubTab::CreateNotebook;
                }
                if (ImGui::IsItemActive()) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 6.0f, 0, 1.5f);
                }

                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Open from Disk...", ImVec2(164.0f, 40.0f))) {
#if defined(_WIN32)
                    std::string picked = ShowNativeFolderPicker("Select FolioNote Notebook Package (.notebook) or Library Folder");
                    if (!picked.empty()) {
                        OpenDiskPath(picked, ws, canvas);
                    }
#else
                    showOpenCustomModal = true;
#endif
                }
                if (ImGui::IsItemActive()) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 6.0f, 0, 1.5f);
                }

                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Duplicate Active", ImVec2(154.0f, 40.0f))) {
                    if (auto nb = ws.GetActiveNotebook()) {
                        strncpy_s(copyNbName, (nb->name + " - Copy").c_str(), sizeof(copyNbName) - 1);
                        showSaveAsCopyModal = true;
                    }
                }
                if (ImGui::IsItemActive()) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 6.0f, 0, 1.5f);
                }
                ImGui::PopStyleVar(2);

                ImGui::Dummy(ImVec2(0.0f, 24.0f)); // Generous breathing space

                // Optional Library Filter Tabs (Moderate rounded tabs with black selected text and dark outline)
                ImGui::TextColored(theme.colorTextMuted, "Filter by Library (Optional):");
                ImGui::Dummy(ImVec2(0.0f, 6.0f));

                auto RenderFilterPill = [&](const char* label, size_t index) {
                    bool isSelected = (selectedLibraryIndex == index);
                    ImVec4 btnCol = isSelected ? theme.colorItemSelected : theme.colorSectionBg;
                    ImVec4 textCol = isSelected ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : theme.colorText;

                    ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? theme.colorItemSelected : theme.colorItemHover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
                    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f); // Moderate rounded tabs
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 7.0f));

                    if (ImGui::Button(label, ImVec2(0.0f, 34.0f))) {
                        selectedLibraryIndex = index;
                    }

                    if (isSelected || ImGui::IsItemActive()) {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(30, 30, 30, 240), 6.0f, 0, 1.5f);
                    }

                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(4);
                    ImGui::SameLine(0.0f, 12.0f);
                };

                RenderFilterPill("All Open Notebooks", 0);
                for (size_t l = 0; l < libraryManager.libraries.size(); ++l) {
                    RenderFilterPill(libraryManager.libraries[l].name.c_str(), l + 1);
                }
                ImGui::NewLine();

                ImGui::Dummy(ImVec2(0.0f, 24.0f)); // Generous margin before cards

                // Cards Grid Metrics
                float availW = ImGui::GetContentRegionAvail().x;
                float cardW = std::clamp((availW - 48.0f) * 0.5f, 360.0f, 540.0f);
                float cardH = 132.0f;

                std::string qLower = nbSearchQuery;
                std::transform(qLower.begin(), qLower.end(), qLower.begin(), ::tolower);

                std::vector<CardItem> cardList;

                for (size_t i = 0; i < ws.notebooks.size(); ++i) {
                    auto& nb = ws.notebooks[i];
                    if (!nb) continue;

                    std::string nLower = nb->name;
                    std::transform(nLower.begin(), nLower.end(), nLower.begin(), ::tolower);
                    if (!qLower.empty() && nLower.find(qLower) == std::string::npos) continue;

                    size_t pCount = 0;
                    for (const auto& s : nb->sections) if (s) pCount += s->pages.size();

                    cardList.push_back({ i, nb->name, nb->filePath, nb->iconFile, nb->sections.size(), pCount, ws.activeNotebookIndex == i });
                }

                // Apply Sorting
                if (nbSortMode == 1) { // Title A-Z
                    std::sort(cardList.begin(), cardList.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
                } else if (nbSortMode == 2) { // Most sections
                    std::sort(cardList.begin(), cardList.end(), [](const auto& a, const auto& b) { return a.sections > b.sections; });
                }

                if (cardList.empty()) {
                    ImGui::TextColored(theme.colorTextMuted, "No matching notebooks found.");
                }

                // Group representation by Library vs Standalone
                std::vector<std::vector<CardItem>> libCards(libraryManager.libraries.size());
                std::vector<CardItem> standaloneCards;

                for (const auto& item : cardList) {
                    bool assigned = false;
                    std::error_code ec;
                    std::filesystem::path nbDir(item.path);
                    if (nbDir.extension() == ".notebook") nbDir = nbDir.parent_path();

                    for (size_t l = 0; l < libraryManager.libraries.size(); ++l) {
                        std::filesystem::path libRoot(libraryManager.libraries[l].rootPath);
                        if (!item.path.empty() && std::filesystem::exists(nbDir, ec) && std::filesystem::exists(libRoot, ec)) {
                            if (std::filesystem::equivalent(nbDir, libRoot, ec)) {
                                libCards[l].push_back(item);
                                assigned = true;
                                break;
                            }
                        }
                    }
                    if (!assigned) {
                        standaloneCards.push_back(item);
                    }
                }

                auto RenderSectionCards = [&](const std::vector<CardItem>& sectionCards) {
                    for (size_t c = 0; c < sectionCards.size(); ++c) {
                        RenderNotebookCard(sectionCards[c], cardW, cardH, theme, ws, canvas, outViewMode);
                        if (c % 2 == 0 && (c + 1 < sectionCards.size())) {
                            ImGui::SameLine(0.0f, 24.0f);
                        } else {
                            ImGui::Dummy(ImVec2(0.0f, 16.0f));
                        }
                    }
                };

                if (selectedLibraryIndex == 0) {
                    // "All Open Notebooks" view: Group by Library headers and Standalone header
                    for (size_t l = 0; l < libraryManager.libraries.size(); ++l) {
                        const auto& lib = libraryManager.libraries[l];
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorNavBg);
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 8.0f));
                        std::string libHdrId = "##LibHeaderBar_" + std::to_string(l);
                        ImGui::BeginChild(libHdrId.c_str(), ImVec2(availW, 40.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

                        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                        ImGui::TextColored(theme.colorText, "📁 Library: %s", lib.name.c_str());
                        ImGui::PopFont();
                        ImGui::SameLine(0.0f, 14.0f);
                        ImGui::TextColored(theme.colorTextMuted, "•  %s  (%zu open)", lib.rootPath.c_str(), libCards[l].size());

                        ImGui::EndChild();
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor();
                        ImGui::Dummy(ImVec2(0.0f, 10.0f));

                        if (libCards[l].empty()) {
                            ImGui::TextColored(theme.colorTextMuted, "    No open notebooks in this library folder.");
                            ImGui::Dummy(ImVec2(0.0f, 14.0f));
                        } else {
                            RenderSectionCards(libCards[l]);
                            ImGui::Dummy(ImVec2(0.0f, 16.0f));
                        }
                    }

                    // Standalone Notebooks section
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorNavBg);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 8.0f));
                    ImGui::BeginChild("##StandaloneHeaderBar", ImVec2(availW, 40.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

                    ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                    ImGui::TextColored(theme.colorText, "📄 Standalone Notebooks");
                    ImGui::PopFont();
                    ImGui::SameLine(0.0f, 14.0f);
                    ImGui::TextColored(theme.colorTextMuted, "•  Individual notebooks outside registered libraries (%zu open)", standaloneCards.size());

                    ImGui::EndChild();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                    ImGui::Dummy(ImVec2(0.0f, 10.0f));

                    if (standaloneCards.empty()) {
                        ImGui::TextColored(theme.colorTextMuted, "    No standalone notebooks open.");
                        ImGui::Dummy(ImVec2(0.0f, 14.0f));
                    } else {
                        RenderSectionCards(standaloneCards);
                        ImGui::Dummy(ImVec2(0.0f, 16.0f));
                    }
                } else {
                    // Filtered to a specific library
                    size_t l = selectedLibraryIndex - 1;
                    if (l < libraryManager.libraries.size()) {
                        const auto& lib = libraryManager.libraries[l];
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorNavBg);
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 8.0f));
                        std::string libHdrId = "##SingleLibHeaderBar_" + std::to_string(l);
                        ImGui::BeginChild(libHdrId.c_str(), ImVec2(availW, 40.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

                        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                        ImGui::TextColored(theme.colorText, "📁 Library: %s", lib.name.c_str());
                        ImGui::PopFont();
                        ImGui::SameLine(0.0f, 14.0f);
                        ImGui::TextColored(theme.colorTextMuted, "•  %s  (%zu open)", lib.rootPath.c_str(), libCards[l].size());

                        ImGui::EndChild();
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor();
                        ImGui::Dummy(ImVec2(0.0f, 10.0f));

                        if (libCards[l].empty()) {
                            ImGui::TextColored(theme.colorTextMuted, "    No open notebooks in this library folder.");
                        } else {
                            RenderSectionCards(libCards[l]);
                        }
                    }
                }
                break;
            }

            // ----------------------------------------------------------------
            // SUB-TAB 2: LIBRARY FOLDERS
            // ----------------------------------------------------------------
            case NbManagerSubTab::LibraryFolders: {
                ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
                ImGui::TextColored(theme.colorText, "Library Folders");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Organize your notebooks into physical library directories on your local drive or cloud storage");

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 14.0f));

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
                if (ImGui::Button("+ Register New Library Folder", ImVec2(240.0f, 42.0f))) {
                    showNewLibraryModal = true;
                }
                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Scan & Refresh All", ImVec2(180.0f, 42.0f))) {
                    libraryManager.RefreshAll();
                }
                ImGui::PopStyleVar(2);

                ImGui::Dummy(ImVec2(0.0f, 20.0f));

                ImGui::TextColored(theme.colorText, "Default Library Directory: %s", libraryManager.defaultLibraryPath.c_str());
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                if (libraryManager.libraries.empty()) {
                    ImGui::TextColored(theme.colorTextMuted, "No custom library folders registered yet.");
                }

                for (size_t l = 0; l < libraryManager.libraries.size(); ++l) {
                    auto& lib = libraryManager.libraries[l];
                    ImGui::PushID(static_cast<int>(l));

                    ImVec2 mMin = ImGui::GetCursorScreenPos();
                    float w = std::min(860.0f, ImGui::GetContentRegionAvail().x);
                    float h = 100.0f;
                    ImVec2 mMax(mMin.x + w, mMin.y + h);

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(mMin, mMax, ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg), 8.0f);
                    dl->AddRect(mMin, mMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 8.0f);

                    ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                    dl->AddText(ImVec2(mMin.x + 18.0f, mMin.y + 16.0f), ImGui::ColorConvertFloat4ToU32(theme.colorText), lib.name.c_str());
                    ImGui::PopFont();

                    dl->AddText(ImVec2(mMin.x + 18.0f, mMin.y + 44.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), lib.rootPath.c_str());
                    std::string countStr = std::to_string(lib.notebookPaths.size()) + " notebook package(s) found in this library";
                    dl->AddText(ImVec2(mMin.x + 18.0f, mMin.y + 68.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), countStr.c_str());

                    ImGui::SetCursorPos(ImVec2(w - 220.0f, mMin.y - ImGui::GetWindowPos().y + 30.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                    if (ImGui::Button("Open in Explorer", ImVec2(130.0f, 34.0f))) {
#if defined(_WIN32)
                        ShellExecuteA(NULL, "open", lib.rootPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
                    }
                    ImGui::SameLine(0.0f, 8.0f);
                    if (ImGui::Button("Unlink", ImVec2(70.0f, 34.0f))) {
                        libraryManager.libraries.erase(libraryManager.libraries.begin() + l);
                        ImGui::PopStyleVar();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleVar();

                    ImGui::Dummy(ImVec2(w, h + 12.0f));
                    ImGui::PopID();
                }
                break;
            }

            // ----------------------------------------------------------------
            // SUB-TAB 3: BLENDED IMPORT & MIGRATION
            // ----------------------------------------------------------------
            case NbManagerSubTab::ImportAndMigration: {
                ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
                ImGui::TextColored(theme.colorText, "Import & Document Migration");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Migrate notebooks from Microsoft OneNote or import existing FolioNote packages and libraries");

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 14.0f));

                // 1. OneNote Migration
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "1. Microsoft OneNote Migration (.one / .onetoc2 / .onepkg / Folder)");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Reverse-engineers and parses OneNote files, extracting sections, pages, and rich text into native FolioNote notebooks");
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));
                ImGui::SetNextItemWidth(540.0f);
                ImGui::InputTextWithHint("##OneNotePathInput", "Path to .one file, .onepkg archive, or OneNote notebook folder...", importOneNotePath, sizeof(importOneNotePath));
                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Migrate OneNote Notebook", ImVec2(220.0f, 38.0f))) {
                    if (strlen(importOneNotePath) > 0) {
                        auto converted = Folio::ImportManager::ImportOneNote(importOneNotePath, libraryManager.defaultLibraryPath, ws.repository);
                        if (converted) {
                            ws.notebooks.push_back(converted);
                            ws.activeNotebookIndex = ws.notebooks.size() - 1;
                            oneNoteSuccess = true;
                            oneNoteStatusMessage = "Successfully migrated OneNote notebook: " + converted->name + "!";
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                        } else {
                            oneNoteSuccess = false;
                            oneNoteStatusMessage = "Could not parse or migrate OneNote file. Verify file path and format.";
                        }
                    }
                }
                ImGui::PopStyleVar(2);

                if (!oneNoteStatusMessage.empty()) {
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                    ImVec4 col = oneNoteSuccess ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    ImGui::TextColored(col, "%s", oneNoteStatusMessage.c_str());
                }

                ImGui::Dummy(ImVec2(0.0f, 28.0f));

                // 2. FolioNote Package Import
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "2. FolioNote Standalone Package (.notebook / .folio)");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Imports a standalone package into your active library and opens it immediately");
                ImGui::Dummy(ImVec2(0.0f, 6.0f));

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));
                ImGui::SetNextItemWidth(430.0f);
                ImGui::InputTextWithHint("##FolioPkgPathInput", "Path to .notebook package folder...", importPkgPath, sizeof(importPkgPath));
                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button("Browse...##ImportPkgBrowse", ImVec2(90.0f, 38.0f))) {
                    std::string picked = ShowNativeFolderPicker("Select FolioNote Package (.notebook)");
                    if (!picked.empty()) {
                        strncpy_s(importPkgPath, picked.c_str(), sizeof(importPkgPath) - 1);
                    }
                }
                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Import Package", ImVec2(180.0f, 38.0f))) {
                    if (strlen(importPkgPath) > 0) {
                        std::string targetDir = libraryManager.defaultLibraryPath;
                        std::string importedPath = Folio::ImportManager::ImportNotebookPackage(importPkgPath, targetDir);
                        if (!importedPath.empty()) {
                            if (auto loaded = ws.repository.LoadNotebookHierarchy(importedPath)) {
                                ws.notebooks.push_back(loaded);
                                ws.activeNotebookIndex = ws.notebooks.size() - 1;
                                pkgSuccess = true;
                                pkgStatusMessage = "Package imported and mounted into library!";
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                            }
                        } else {
                            pkgSuccess = false;
                            pkgStatusMessage = "Failed to copy or load package.";
                        }
                    }
                }
                ImGui::PopStyleVar(2);

                if (!pkgStatusMessage.empty()) {
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                    ImVec4 col = pkgSuccess ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    ImGui::TextColored(col, "%s", pkgStatusMessage.c_str());
                }

                ImGui::Dummy(ImVec2(0.0f, 28.0f));

                // 3. Register Existing Folder as Library
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "3. Register External Library Folder (.foliolib / folder)");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Points FolioNote to a folder containing existing notebooks without copying files");
                ImGui::Dummy(ImVec2(0.0f, 6.0f));

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));
                ImGui::SetNextItemWidth(300.0f);
                ImGui::InputTextWithHint("##LibFolderPathInput", "Folder directory path on disk...", importLibFolder, sizeof(importLibFolder));
                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button("Browse...##ImportLibBrowse", ImVec2(90.0f, 38.0f))) {
                    std::string picked = ShowNativeFolderPicker("Select External Library Folder");
                    if (!picked.empty()) {
                        strncpy_s(importLibFolder, picked.c_str(), sizeof(importLibFolder) - 1);
                    }
                }
                ImGui::SameLine(0.0f, 10.0f);
                ImGui::SetNextItemWidth(170.0f);
                ImGui::InputTextWithHint("##LibCustomNameInput", "Library Name (Optional)...", importLibCustomName, sizeof(importLibCustomName));
                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Register Library", ImVec2(180.0f, 38.0f))) {
                    if (strlen(importLibFolder) > 0) {
                        if (Folio::ImportManager::ImportLibraryFolder(importLibFolder, libraryManager, importLibCustomName)) {
                            libSuccess = true;
                            libStatusMessage = "Library folder registered successfully!";
                        } else {
                            libSuccess = false;
                            libStatusMessage = "Directory does not exist or cannot be accessed.";
                        }
                    }
                }
                ImGui::PopStyleVar(2);

                if (!libStatusMessage.empty()) {
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                    ImVec4 col = libSuccess ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    ImGui::TextColored(col, "%s", libStatusMessage.c_str());
                }
                break;
            }

            // ----------------------------------------------------------------
            // SUB-TAB 4: CREATE NOTEBOOK WITH TEMPLATES
            // ----------------------------------------------------------------
            case NbManagerSubTab::CreateNotebook: {
                ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
                ImGui::TextColored(theme.colorText, "Create New Notebook");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Select title, accent palette, and starting page template (ruled paper, engineering grid, Cornell notes)");

                ImGui::Dummy(ImVec2(0.0f, 14.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 14.0f));

                ImGui::Text("Notebook Title:");
                ImGui::SetNextItemWidth(360.0f);
                ImGui::InputText("##NewNbNameBox", newNbName, sizeof(newNbName));

                ImGui::Dummy(ImVec2(0.0f, 12.0f));
                ImGui::Text("Storage Location:");
                ImGui::RadioButton("Add to Library Folder", &newNbLocationType, 0);
                ImGui::SameLine(0.0f, 24.0f);
                ImGui::RadioButton("Create Standalone Notebook", &newNbLocationType, 1);

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                if (newNbLocationType == 0) {
                    ImGui::TextColored(theme.colorTextMuted, "Target Library Folder:");
                    if (newNbSelectedLibIdx < 0 || newNbSelectedLibIdx >= static_cast<int>(libraryManager.libraries.size())) {
                        newNbSelectedLibIdx = 0;
                    }
                    std::string currentLibPreview = libraryManager.libraries.empty() ? "None" : libraryManager.libraries[newNbSelectedLibIdx].name + "  (" + libraryManager.libraries[newNbSelectedLibIdx].rootPath + ")";
                    ImGui::SetNextItemWidth(460.0f);
                    if (ImGui::BeginCombo("##NewNbLibSelectCombo", currentLibPreview.c_str())) {
                        for (int l = 0; l < static_cast<int>(libraryManager.libraries.size()); ++l) {
                            bool isSel = (newNbSelectedLibIdx == l);
                            std::string libLabel = libraryManager.libraries[l].name + "  [" + libraryManager.libraries[l].rootPath + "]";
                            if (ImGui::Selectable(libLabel.c_str(), isSel)) {
                                newNbSelectedLibIdx = l;
                            }
                            if (isSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::TextColored(theme.colorTextMuted, "Custom Directory:");
                    ImGui::SetNextItemWidth(360.0f);
                    ImGui::InputTextWithHint("##NewNbPathBox", "Default: Documents/FolioNote", newNbPath, sizeof(newNbPath));
                    ImGui::SameLine(0.0f, 8.0f);
                    if (ImGui::Button("Browse...##NewNbBrowseBtn", ImVec2(90.0f, 0.0f))) {
                        std::string picked = ShowNativeFolderPicker("Select Custom Folder for Standalone Notebook");
                        if (!picked.empty()) {
                            strncpy_s(newNbPath, picked.c_str(), sizeof(newNbPath) - 1);
                        }
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Select Page Template:");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0.0f, 6.0f));

                // Clean horizontal template selection cards
                struct PageTemplateOption {
                    const char* id;
                    const char* title;
                    const char* subtitle;
                };
                const PageTemplateOption templateOptions[5] = {
                    { "##tpl_blank",   "Blank Canvas",  "Freeform sketch" },
                    { "##tpl_ruled",   "College Ruled", "Lined paper" },
                    { "##tpl_grid",    "5mm Grid",      "Engineering" },
                    { "##tpl_dot",     "Dot Grid",      "Dot matrix" },
                    { "##tpl_cornell", "Cornell Notes", "Notes & summary" }
                };

                float availWidth = ImGui::GetContentRegionAvail().x;
                float gap = 10.0f;
                float tileW = 145.0f;
                float tileH = 56.0f;
                if (availWidth > 0.0f && availWidth < (5 * tileW + 4 * gap)) {
                    tileW = std::max(105.0f, (availWidth - 4 * gap) / 5.0f);
                }

                for (int t = 0; t < 5; ++t) {
                    bool isSelected = (newNbTemplateIndex == t);
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 p1(p0.x + tileW, p0.y + tileH);

                    bool clicked = ImGui::InvisibleButton(templateOptions[t].id, ImVec2(tileW, tileH));
                    if (clicked) {
                        newNbTemplateIndex = t;
                    }
                    bool hovered = ImGui::IsItemHovered();

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bgCol = isSelected ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected)
                                             : (hovered ? ImGui::ColorConvertFloat4ToU32(theme.colorItemHover)
                                                        : ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg));
                    dl->AddRectFilled(p0, p1, bgCol, 6.0f);

                    if (isSelected) {
                        dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(theme.colorPrimary), 6.0f, 0, 2.0f);
                    } else {
                        dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 6.0f, 0, 1.0f);
                    }

                    ImU32 titleCol = isSelected ? IM_COL32(20, 20, 20, 255) : ImGui::ColorConvertFloat4ToU32(theme.colorText);
                    ImU32 subCol = ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted);

                    dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 10.0f), titleCol, templateOptions[t].title);
                    dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 32.0f), subCol, templateOptions[t].subtitle);

                    if (t < 4) {
                        ImGui::SameLine(0.0f, gap);
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 18.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 12.0f));

                if (ImGui::Button("Create & Open Notebook", ImVec2(240.0f, 46.0f))) {
                    if (strlen(newNbName) > 0) {
                        std::string targetDir;
                        if (newNbLocationType == 0 && !libraryManager.libraries.empty()) {
                            if (newNbSelectedLibIdx < 0 || newNbSelectedLibIdx >= static_cast<int>(libraryManager.libraries.size())) newNbSelectedLibIdx = 0;
                            targetDir = libraryManager.libraries[newNbSelectedLibIdx].rootPath;
                        } else {
                            targetDir = strlen(newNbPath) > 0 ? std::string(newNbPath) : libraryManager.defaultLibraryPath;
                        }
                        auto created = libraryManager.CreateNewNotebook(newNbName, targetDir);
                        if (created) {
                            // Apply template configuration to first page
                            if (!created->sections.empty() && !created->sections[0]->pages.empty()) {
                                auto p = created->sections[0]->pages[0];
                                if (newNbTemplateIndex == 1) p->title = "Notes";
                                else if (newNbTemplateIndex == 2) p->title = "Engineering Calc";
                                else if (newNbTemplateIndex == 4) p->title = "Cornell Summary";
                            }
                            // Set canvas paper style according to template
                            if (newNbTemplateIndex == 0) canvas.currentPaperStyle = PaperStyle::Blank;
                            else if (newNbTemplateIndex == 1) canvas.currentPaperStyle = PaperStyle::Lined;
                            else if (newNbTemplateIndex == 2) canvas.currentPaperStyle = PaperStyle::Grid;
                            else if (newNbTemplateIndex == 3) canvas.currentPaperStyle = PaperStyle::Dotted;
                            else if (newNbTemplateIndex == 4) canvas.currentPaperStyle = PaperStyle::Lined;
                            canvas.defaultTemplate.paperStyle = canvas.currentPaperStyle;

                            ws.notebooks.push_back(created);
                            ws.activeNotebookIndex = ws.notebooks.size() - 1;
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                            outViewMode = AppViewMode::CanvasWorkspace;
                        }
                    }
                }
                ImGui::PopStyleVar(2);
                break;
            }
        }
    }

    // ========================================================================
    // CATEGORY 2: NOTEBOOK INFO & HEALTH CONTENT
    // ========================================================================
    void RenderNotebookInfoContent(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto activeNb = session.workspace.GetActiveNotebook();

        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorText, "Notebook Information & Telemetry");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Overview, metadata, storage path, lifetime tracking, and synchronization state");

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        if (!activeNb) {
            ImGui::TextColored(theme.colorTextMuted, "No notebook currently open.");
            return;
        }

        // Active Notebook Header Card
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cardMin = ImGui::GetCursorScreenPos();
        float cardWidth = std::min(840.0f, ImGui::GetContentRegionAvail().x);
        float cardHeight = 110.0f;
        ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);

        dl->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg), 0.0f);
        dl->AddRect(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 0.0f, 0, 1.0f);

        GLuint iconTex = 0;
        if (!activeNb->iconFile.empty()) {
            iconTex = g_IconManager.LoadOrGetSVG(activeNb->iconFile, "assets/icons/Sections_Notebooks/" + activeNb->iconFile, 128);
        }
        if (iconTex != 0) {
            dl->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(cardMin.x + 18.0f, cardMin.y + 18.0f), ImVec2(cardMin.x + 92.0f, cardMin.y + 92.0f));
        }

        float textX = cardMin.x + 108.0f;
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        dl->AddText(ImVec2(textX, cardMin.y + 18.0f), ImGui::ColorConvertFloat4ToU32(theme.colorText), activeNb->name.c_str());
        ImGui::PopFont();

        std::string pathStr = activeNb->filePath.empty() ? "(In Memory Package)" : activeNb->filePath;
        dl->AddText(ImVec2(textX, cardMin.y + 50.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), pathStr.c_str());
        std::string guidStr = "UUID: " + activeNb->guid;
        dl->AddText(ImVec2(textX, cardMin.y + 74.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), guidStr.c_str());

        ImGui::Dummy(ImVec2(0.0f, cardHeight + 20.0f));

        // Format helpers
        auto FormatTimeDuration = [](uint64_t totalSec) -> std::string {
            uint64_t hours = totalSec / 3600;
            uint64_t mins = (totalSec % 3600) / 60;
            uint64_t secs = totalSec % 60;
            if (hours > 0) return std::to_string(hours) + "h " + std::to_string(mins) + "m";
            if (mins > 0) return std::to_string(mins) + "m " + std::to_string(secs) + "s";
            return std::to_string(secs) + "s";
        };

        auto RenderMetricCard = [&](const char* title, const std::string& val, float width, const char* subtitle = nullptr) {
            ImVec2 mMin = ImGui::GetCursorScreenPos();
            float h = subtitle ? 76.0f : 70.0f;
            ImVec2 mMax(mMin.x + width, mMin.y + h);
            dl->AddRectFilled(mMin, mMax, ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg), 0.0f);
            dl->AddRect(mMin, mMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 0.0f);
            dl->AddText(ImVec2(mMin.x + 14.0f, mMin.y + 10.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), title);
            ImGui::PushFont(FolioTheme::FontNavBoldLarge);
            dl->AddText(ImVec2(mMin.x + 14.0f, mMin.y + 30.0f), ImGui::ColorConvertFloat4ToU32(theme.colorText), val.c_str());
            ImGui::PopFont();
            if (subtitle) {
                dl->AddText(ImVec2(mMin.x + 14.0f, mMin.y + 54.0f), ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), subtitle);
            }
            ImGui::Dummy(ImVec2(width, h));
        };

        // Statistics calculation
        size_t totalSecs = activeNb->sections.size();
        for (const auto& grp : activeNb->sectionGroups) if (grp) totalSecs += grp->sections.size();
        size_t totalPages = 0;
        size_t totalObjects = 0;
        size_t totalStrokes = 0;

        auto CountStats = [&](const std::shared_ptr<CanvasPage>& pg) {
            if (!pg) return;
            totalPages++;
            totalObjects += pg->objects.size();
            for (const auto& obj : pg->objects) {
                if (auto ink = std::dynamic_pointer_cast<InkContainer>(obj)) {
                    totalStrokes += ink->strokes.size();
                }
            }
        };

        for (const auto& sec : activeNb->sections) if (sec) for (const auto& pg : sec->pages) CountStats(pg);
        for (const auto& grp : activeNb->sectionGroups) if (grp) for (const auto& s : grp->sections) if (s) for (const auto& pg : s->pages) CountStats(pg);

        float mWidth = 185.0f;

        // Metric Rows
        RenderMetricCard("Sections", std::to_string(totalSecs), mWidth);
        ImGui::SameLine(0.0f, 16.0f);
        RenderMetricCard("Section Groups", std::to_string(activeNb->sectionGroups.size()), mWidth);
        ImGui::SameLine(0.0f, 16.0f);
        RenderMetricCard("Total Pages", std::to_string(totalPages), mWidth);
        ImGui::SameLine(0.0f, 16.0f);
        RenderMetricCard("Active Page", activeNb->GetActivePage() ? activeNb->GetActivePage()->title : "None", mWidth + 40.0f);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        RenderMetricCard("Global Objects", std::to_string(totalObjects), mWidth, "Canvas Entities");
        ImGui::SameLine(0.0f, 16.0f);
        RenderMetricCard("Ink Strokes", std::to_string(totalStrokes), mWidth, "Vector Paths");
        ImGui::SameLine(0.0f, 16.0f);
        RenderMetricCard("Session Time", FormatTimeDuration(activeNb->GetSessionSeconds()), mWidth, "Active Session");
        ImGui::SameLine(0.0f, 16.0f);
        RenderMetricCard("Lifetime Time", FormatTimeDuration(activeNb->GetTotalLifetimeSeconds()), mWidth + 40.0f, "All-Time on Notebook");

        // Overview statistics are complete and self-contained
    }

    // ========================================================================
    // CATEGORY 3: EXPORT & PRINT CONTENT
    // ========================================================================
    void RenderExportContent(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
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

        struct ScopeCard {
            const char* id;
            const char* title;
            std::string subtitle;
        };
        std::string curPgName = activePg ? activePg->title : "No Active Page";
        std::string curSecName = activeSec ? (activeSec->name + " (" + std::to_string(activeSec->pages.size()) + " pages)") : "No Active Section";
        std::string curNbName = activeNb ? (activeNb->name + " (Entire Book)") : "No Active Notebook";

        ScopeCard scopeCards[3] = {
            { "##scope_page", "Current Page", curPgName },
            { "##scope_sec",  "Current Section", curSecName },
            { "##scope_nb",   "Entire Notebook", curNbName }
        };

        float availW = ImGui::GetContentRegionAvail().x;
        float scopeGap = 12.0f;
        float scopeCardW = 210.0f;
        float cardH = 54.0f;
        if (availW > 0.0f && availW < (3 * scopeCardW + 2 * scopeGap)) {
            scopeCardW = std::max(140.0f, (availW - 2 * scopeGap) / 3.0f);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int s = 0; s < 3; ++s) {
            bool isSel = (static_cast<int>(exportScope) == s);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + scopeCardW, p0.y + cardH);

            bool clicked = ImGui::InvisibleButton(scopeCards[s].id, ImVec2(scopeCardW, cardH));
            if (clicked) {
                exportScope = static_cast<ExportScope>(s);
            }
            bool hovered = ImGui::IsItemHovered();

            ImU32 bgCol = isSel ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected)
                                : (hovered ? ImGui::ColorConvertFloat4ToU32(theme.colorItemHover)
                                           : ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg));
            dl->AddRectFilled(p0, p1, bgCol, 6.0f);

            if (isSel) {
                dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(theme.colorPrimary), 6.0f, 0, 2.0f);
            } else {
                dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 6.0f, 0, 1.0f);
            }

            ImU32 titleCol = isSel ? IM_COL32(20, 20, 20, 255) : ImGui::ColorConvertFloat4ToU32(theme.colorText);
            ImU32 subCol = ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted);

            dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 9.0f), titleCol, scopeCards[s].title);
            dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 30.0f), subCol, scopeCards[s].subtitle.c_str());

            if (s < 2) {
                ImGui::SameLine(0.0f, scopeGap);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 18.0f));

        // 2. Choose Format
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "2. Select Export Format:");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        struct FormatCard {
            const char* id;
            const char* title;
            const char* subtitle;
        };
        FormatCard formatCards[4] = {
            { "##fmt_pdf",   "Print to PDF",       "A4 paginated" },
            { "##fmt_html",  "HTML Document",      "Standalone web" },
            { "##fmt_folio", "Folio Package",      "Portable archive" },
            { "##fmt_md",    "Markdown Notes",     "Clean text .md" }
        };

        float fmtGap = 10.0f;
        float fmtCardW = 160.0f;
        if (availW > 0.0f && availW < (4 * fmtCardW + 3 * fmtGap)) {
            fmtCardW = std::max(115.0f, (availW - 3 * fmtGap) / 4.0f);
        }

        for (int f = 0; f < 4; ++f) {
            bool isSel = (static_cast<int>(exportFormat) == f);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + fmtCardW, p0.y + cardH);

            bool clicked = ImGui::InvisibleButton(formatCards[f].id, ImVec2(fmtCardW, cardH));
            if (clicked) {
                exportFormat = static_cast<ExportFormat>(f);
            }
            bool hovered = ImGui::IsItemHovered();

            ImU32 bgCol = isSel ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected)
                                : (hovered ? ImGui::ColorConvertFloat4ToU32(theme.colorItemHover)
                                           : ImGui::ColorConvertFloat4ToU32(theme.colorSectionBg));
            dl->AddRectFilled(p0, p1, bgCol, 6.0f);

            if (isSel) {
                dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(theme.colorPrimary), 6.0f, 0, 2.0f);
            } else {
                dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 6.0f, 0, 1.0f);
            }

            ImU32 titleCol = isSel ? IM_COL32(20, 20, 20, 255) : ImGui::ColorConvertFloat4ToU32(theme.colorText);
            ImU32 subCol = ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted);

            dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 9.0f), titleCol, formatCards[f].title);
            dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 30.0f), subCol, formatCards[f].subtitle);

            if (f < 3) {
                ImGui::SameLine(0.0f, fmtGap);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 20.0f));

        // 3. Optional Destination Path
        ImGui::PushFont(FolioTheme::FontNavBoldLarge);
        ImGui::TextColored(theme.colorText, "3. Destination (Optional Custom Folder / Path):");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        float browseBtnW = 86.0f;
        float inputW = std::min(480.0f, availW - browseBtnW - 8.0f);
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputTextWithHint("##ExportCustomPath", "Default: exports/<filename>", exportCustomPath, sizeof(exportCustomPath));
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Browse...##ExportBrowse", ImVec2(browseBtnW, 32.0f))) {
            std::string picked = ShowNativeFolderPicker("Select Export Destination Folder");
            if (!picked.empty()) {
                strncpy_s(exportCustomPath, picked.c_str(), sizeof(exportCustomPath) - 1);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 22.0f));

        // Export Action Buttons
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 10.0f));

        ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (exportFormat == ExportFormat::PDF_Print) {
            if (ImGui::Button("Print to PDF Now", ImVec2(190.0f, 40.0f))) {
                exportSuccess = ExportManager::Export(exportScope, ExportFormat::PDF_Print, activeNb, activeSec, activePg, exportCustomPath, true);
                exportStatusMessage = exportSuccess ? "Document generated and sent to system Print/PDF workflow!" : "Failed to generate print document.";
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0.0f, 12.0f);
            if (ImGui::Button("Save PDF File Only", ImVec2(180.0f, 40.0f))) {
                exportSuccess = ExportManager::Export(exportScope, ExportFormat::PDF_Print, activeNb, activeSec, activePg, exportCustomPath, false);
                exportStatusMessage = exportSuccess ? "Exported successfully to disk." : "Failed to export file.";
            }
        } else {
            if (ImGui::Button("Export Document Now", ImVec2(200.0f, 40.0f))) {
                exportSuccess = ExportManager::Export(exportScope, exportFormat, activeNb, activeSec, activePg, exportCustomPath, false);
                exportStatusMessage = exportSuccess ? "Exported successfully to disk." : "Failed to export file.";
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::PopStyleVar(2);

        if (!exportStatusMessage.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            ImVec4 msgCol = exportSuccess ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(msgCol, "%s", exportStatusMessage.c_str());
            if (exportSuccess) {
                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Open Exports Folder", ImVec2(160.0f, 30.0f))) {
#if defined(_WIN32)
                    ShellExecuteA(NULL, "open", "exports", NULL, NULL, SW_SHOWNORMAL);
#endif
                }
            }
        }
    }

    // ========================================================================
    // CATEGORY 4: SETTINGS & PREFERENCES CONTENT
    // ========================================================================
    void RenderSettingsContent(ThemeManager& theme, CanvasEngine& canvas, SDL_Window* window) {
        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorText, "Application Settings");
        ImGui::PopFont();
        ImGui::TextColored(theme.colorTextMuted, "Centralized configuration hub for software, inking tuning, themes, and extensions");

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        switch (activeSettingsSubCategory) {
            case SettingsSubCategory::General: {
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

            case SettingsSubCategory::Appearance: {
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

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Custom Icon Packs & Assets Import");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Import custom SVG/PNG icon collections for sections and notebooks");
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputTextWithHint("##IconPackPath", "Folder path to SVG icon pack...", customIconPackPath, sizeof(customIconPackPath));
                ImGui::SameLine(0.0f, 14.0f);
                if (ImGui::Button("Import Icon Pack", ImVec2(160.0f, 38.0f))) {
                    // Refreshes icon cache
                }
                ImGui::PopStyleVar(2);

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
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

            case SettingsSubCategory::Inking: {
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
                
                // Visual 10cm calibration bar
                float barPixels = (rulerDpiCalibration / 2.54f) * 10.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 rMin = ImGui::GetCursorScreenPos();
                ImVec2 rMax(rMin.x + barPixels, rMin.y + 24.0f);
                dl->AddRectFilled(rMin, rMax, ImGui::ColorConvertFloat4ToU32(theme.colorPrimary), 4.0f);
                dl->AddText(ImVec2(rMin.x + 10.0f, rMin.y + 4.0f), IM_COL32(255,255,255,255), "10.0 cm Calibration Ruler");
                ImGui::Dummy(ImVec2(barPixels, 28.0f));
                break;
            }

            case SettingsSubCategory::Storage: {
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

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
                if (ImGui::Button("Compact & Vacuum SQLite Databases", ImVec2(300.0f, 40.0f))) {
                    // Compact SQLite
                }
                ImGui::PopStyleVar(2);
                break;
            }

            case SettingsSubCategory::Addons: {
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

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
                if (ImGui::Button("+ Install Extension from File (.fext, .zip)...", ImVec2(360.0f, 42.0f))) {
                    // Add-on installer dialog
                }
                ImGui::PopStyleVar(2);
                break;
            }

            case SettingsSubCategory::About: {
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

                ImGui::Dummy(ImVec2(0.0f, 20.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
                if (ImGui::Button("Check for Updates", ImVec2(200.0f, 42.0f))) {
                    // Update check
                }
                ImGui::PopStyleVar(2);
                break;
            }
        }
    }

    // ========================================================================
    // 4. MODALS AND POPUPS
    // ========================================================================
    void RenderModals(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        // 1. New Notebook Modal
        if (showNewNotebookModal) {
            ImGui::OpenPopup("NewNotebookModal##Hub");
            showNewNotebookModal = false;
        }
        ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
        {
            ModalThemeScope modalScope(theme);
            if (ImGui::BeginPopupModal("NewNotebookModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                float availW = ImGui::GetContentRegionAvail().x;

                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Create New Notebook");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Configure notebook title and target storage location");
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                ImGui::TextColored(theme.colorText, "Notebook Name:");
                ImGui::SetNextItemWidth(availW);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                bool enterPressed = ImGui::InputText("##NewNbNameInput", newNbName, sizeof(newNbName), ImGuiInputTextFlags_EnterReturnsTrue);

                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImGui::TextColored(theme.colorText, "Storage Location:");
                ImGui::RadioButton("Add to Library Folder##Modal", &newNbLocationType, 0);
                ImGui::SameLine(0.0f, 20.0f);
                ImGui::RadioButton("Standalone Notebook##Modal", &newNbLocationType, 1);

                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                if (newNbLocationType == 0) {
                    if (newNbSelectedLibIdx < 0 || newNbSelectedLibIdx >= static_cast<int>(libraryManager.libraries.size())) {
                        newNbSelectedLibIdx = 0;
                    }
                    std::string currentLibPreview = libraryManager.libraries.empty() ? "None" : libraryManager.libraries[newNbSelectedLibIdx].name;
                    ImGui::SetNextItemWidth(availW);
                    if (ImGui::BeginCombo("##ModalLibCombo", currentLibPreview.c_str())) {
                        for (int l = 0; l < static_cast<int>(libraryManager.libraries.size()); ++l) {
                            bool isSel = (newNbSelectedLibIdx == l);
                            std::string libLabel = libraryManager.libraries[l].name + " (" + libraryManager.libraries[l].rootPath + ")";
                            if (ImGui::Selectable(libLabel.c_str(), isSel)) {
                                newNbSelectedLibIdx = l;
                            }
                            if (isSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    float browseBtnW = 86.0f;
                    float gap = 8.0f;
                    float inputW = availW - browseBtnW - gap;
                    ImGui::SetNextItemWidth(inputW);
                    ImGui::InputTextWithHint("##NewNbPathInput", "Default: Documents/FolioNote", newNbPath, sizeof(newNbPath));
                    ImGui::SameLine(0.0f, gap);
                    if (ImGui::Button("Browse...##ModalBrowse", ImVec2(browseBtnW, 32.0f))) {
                        std::string picked = ShowNativeFolderPicker("Select Custom Folder for Standalone Notebook");
                        if (!picked.empty()) {
                            strncpy_s(newNbPath, picked.c_str(), sizeof(newNbPath) - 1);
                        }
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                // Bottom Right-Aligned Buttons
                float createBtnW = 140.0f;
                float cancelBtnW = 86.0f;
                float btnGap = 10.0f;
                float rightAlignX = ImGui::GetCursorPosX() + availW - (createBtnW + cancelBtnW + btnGap);
                if (rightAlignX > ImGui::GetCursorPosX()) {
                    ImGui::SetCursorPosX(rightAlignX);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                bool createClicked = ImGui::Button("Create Notebook", ImVec2(createBtnW, 32.0f));
                ImGui::PopStyleColor(3);

                if (createClicked || enterPressed) {
                    if (strlen(newNbName) > 0) {
                        std::string targetDir;
                        if (newNbLocationType == 0 && !libraryManager.libraries.empty()) {
                            if (newNbSelectedLibIdx < 0 || newNbSelectedLibIdx >= static_cast<int>(libraryManager.libraries.size())) {
                                newNbSelectedLibIdx = 0;
                            }
                            targetDir = libraryManager.libraries[newNbSelectedLibIdx].rootPath;
                        } else {
                            targetDir = strlen(newNbPath) > 0 ? std::string(newNbPath) : libraryManager.defaultLibraryPath;
                        }
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

                ImGui::SameLine(0.0f, btnGap);
                if (ImGui::Button("Cancel", ImVec2(cancelBtnW, 32.0f))) {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        // 2. Save As Copy (Duplicate) Modal
        if (showSaveAsCopyModal) {
            ImGui::OpenPopup("SaveAsCopyModal##Hub");
            showSaveAsCopyModal = false;
        }
        ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
        {
            ModalThemeScope modalScope(theme);
            if (ImGui::BeginPopupModal("SaveAsCopyModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                float availW = ImGui::GetContentRegionAvail().x;

                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Duplicate Notebook (Save As Copy)");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Creates an independent duplicate with fresh UUIDs and schema");
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                ImGui::TextColored(theme.colorText, "Copy Name:");
                ImGui::SetNextItemWidth(availW);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                bool enterPressed = ImGui::InputText("##CopyNbNameInput", copyNbName, sizeof(copyNbName), ImGuiInputTextFlags_EnterReturnsTrue);

                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImGui::TextColored(theme.colorText, "Destination Folder (Optional):");
                float browseBtnW = 86.0f;
                float gap = 8.0f;
                float inputW = availW - browseBtnW - gap;
                ImGui::SetNextItemWidth(inputW);
                ImGui::InputTextWithHint("##CopyNbDestInput", "Default: Same folder as original", copyNbDestPath, sizeof(copyNbDestPath));
                ImGui::SameLine(0.0f, gap);
                if (ImGui::Button("Browse...##CopyBrowse", ImVec2(browseBtnW, 32.0f))) {
                    std::string picked = ShowNativeFolderPicker("Select Destination Folder for Duplicate Notebook");
                    if (!picked.empty()) {
                        strncpy_s(copyNbDestPath, picked.c_str(), sizeof(copyNbDestPath) - 1);
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                // Bottom Right-Aligned Buttons
                float dupBtnW = 150.0f;
                float cancelBtnW = 86.0f;
                float btnGap = 10.0f;
                float rightAlignX = ImGui::GetCursorPosX() + availW - (dupBtnW + cancelBtnW + btnGap);
                if (rightAlignX > ImGui::GetCursorPosX()) {
                    ImGui::SetCursorPosX(rightAlignX);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                bool dupClicked = ImGui::Button("Duplicate Notebook", ImVec2(dupBtnW, 32.0f));
                ImGui::PopStyleColor(3);

                if (dupClicked || enterPressed) {
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

                ImGui::SameLine(0.0f, btnGap);
                if (ImGui::Button("Cancel", ImVec2(cancelBtnW, 32.0f))) {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        // 3. New Library Folder Modal
        if (showNewLibraryModal) {
            ImGui::OpenPopup("NewLibraryModal##Hub");
            showNewLibraryModal = false;
        }
        ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
        {
            ModalThemeScope modalScope(theme);
            if (ImGui::BeginPopupModal("NewLibraryModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                float availW = ImGui::GetContentRegionAvail().x;

                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Register Folder as Library");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Groups notebooks stored in a designated directory on disk");
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                ImGui::TextColored(theme.colorText, "Library Display Name:");
                ImGui::SetNextItemWidth(availW);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::InputText("##NewLibNameInput", newLibName, sizeof(newLibName));

                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImGui::TextColored(theme.colorText, "Folder Path on Disk:");
                float browseBtnW = 86.0f;
                float gap = 8.0f;
                float inputW = availW - browseBtnW - gap;
                ImGui::SetNextItemWidth(inputW);
                bool enterPressed = ImGui::InputText("##NewLibPathInput", newLibPath, sizeof(newLibPath), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine(0.0f, gap);
                if (ImGui::Button("Browse...##LibBrowse", ImVec2(browseBtnW, 32.0f))) {
                    std::string picked = ShowNativeFolderPicker("Select Library Folder on Disk");
                    if (!picked.empty()) {
                        strncpy_s(newLibPath, picked.c_str(), sizeof(newLibPath) - 1);
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                // Bottom Right-Aligned Buttons: Add Library & Cancel (both 32px height)
                float addBtnW = 110.0f;
                float cancelBtnW = 86.0f;
                float btnGap = 10.0f;
                float rightAlignX = ImGui::GetCursorPosX() + availW - (addBtnW + cancelBtnW + btnGap);
                if (rightAlignX > ImGui::GetCursorPosX()) {
                    ImGui::SetCursorPosX(rightAlignX);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                bool addClicked = ImGui::Button("Add Library", ImVec2(addBtnW, 32.0f));
                ImGui::PopStyleColor(3);

                if (addClicked || enterPressed) {
                    if (strlen(newLibName) > 0 && strlen(newLibPath) > 0) {
                        libraryManager.AddLibrary(newLibName, newLibPath);
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine(0.0f, btnGap);
                if (ImGui::Button("Cancel", ImVec2(cancelBtnW, 32.0f))) {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        // 4. Open Custom Notebook Modal
        if (showOpenCustomModal) {
            ImGui::OpenPopup("OpenCustomModal##Hub");
            showOpenCustomModal = false;
        }
        ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
        {
            ModalThemeScope modalScope(theme);
            if (ImGui::BeginPopupModal("OpenCustomModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                float availW = ImGui::GetContentRegionAvail().x;

                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Open Notebook from Disk");
                ImGui::PopFont();
                ImGui::TextColored(theme.colorTextMuted, "Select a .notebook package folder or registered Library directory");
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                ImGui::TextColored(theme.colorText, "Folder Path on Disk:");
                float browseBtnW = 86.0f;
                float gap = 8.0f;
                float inputW = availW - browseBtnW - gap;
                ImGui::SetNextItemWidth(inputW);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                bool enterPressed = ImGui::InputText("##OpenCustomPathInput", openCustomPath, sizeof(openCustomPath), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine(0.0f, gap);
                if (ImGui::Button("Browse...##OpenCustomBrowse", ImVec2(browseBtnW, 32.0f))) {
                    std::string picked = ShowNativeFolderPicker("Select Notebook Package (.notebook) or Library Folder");
                    if (!picked.empty()) {
                        strncpy_s(openCustomPath, picked.c_str(), sizeof(openCustomPath) - 1);
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                // Bottom Right-Aligned Buttons
                float openBtnW = 130.0f;
                float cancelBtnW = 86.0f;
                float btnGap = 10.0f;
                float rightAlignX = ImGui::GetCursorPosX() + availW - (openBtnW + cancelBtnW + btnGap);
                if (rightAlignX > ImGui::GetCursorPosX()) {
                    ImGui::SetCursorPosX(rightAlignX);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                bool openClicked = ImGui::Button("Open from Disk", ImVec2(openBtnW, 32.0f));
                ImGui::PopStyleColor(3);

                if (openClicked || enterPressed) {
                    if (strlen(openCustomPath) > 0) {
                        OpenDiskPath(openCustomPath, session.workspace, canvas);
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine(0.0f, btnGap);
                if (ImGui::Button("Cancel", ImVec2(cancelBtnW, 32.0f))) {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        // 5. Rename Notebook Modal
        if (showRenameNotebookModal) {
            ImGui::OpenPopup("RenameNotebookModal##Hub");
            showRenameNotebookModal = false;
        }
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        {
            ModalThemeScope modalScope(theme);
            if (ImGui::BeginPopupModal("RenameNotebookModal##Hub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                float availW = ImGui::GetContentRegionAvail().x;

                ImGui::PushFont(FolioTheme::FontNavBoldLarge);
                ImGui::TextColored(theme.colorText, "Rename Notebook");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                ImGui::TextColored(theme.colorText, "New Notebook Title:");
                ImGui::SetNextItemWidth(availW);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                bool enterPressed = ImGui::InputText("##RenameNbInput", renameNotebookBuffer, sizeof(renameNotebookBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                // Bottom Right-Aligned Buttons
                float renameBtnW = 96.0f;
                float cancelBtnW = 86.0f;
                float btnGap = 10.0f;
                float rightAlignX = ImGui::GetCursorPosX() + availW - (renameBtnW + cancelBtnW + btnGap);
                if (rightAlignX > ImGui::GetCursorPosX()) {
                    ImGui::SetCursorPosX(rightAlignX);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                bool renameClicked = ImGui::Button("Rename", ImVec2(renameBtnW, 32.0f));
                ImGui::PopStyleColor(3);

                if (renameClicked || enterPressed) {
                    if (renameNotebookIndex >= 0 && renameNotebookIndex < static_cast<int>(session.workspace.notebooks.size())) {
                        if (strlen(renameNotebookBuffer) > 0) {
                            session.workspace.notebooks[renameNotebookIndex]->name = renameNotebookBuffer;
                            session.workspace.FlushActiveNotebookAsync();
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine(0.0f, btnGap);
                if (ImGui::Button("Cancel", ImVec2(cancelBtnW, 32.0f))) {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }
    }
};