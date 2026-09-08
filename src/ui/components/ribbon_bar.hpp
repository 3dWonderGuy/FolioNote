#pragma once
#include "imgui.h"
#include "core/engine/canvas_engine.hpp"
#include "input/input_state_machine.hpp"
#include "app/theme_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/icon_manager.hpp"
#include "ui/components/toolbar_builder.hpp"
#include "input/preset_manager.hpp"
#include <string>

#include "app/app_view_mode.hpp"
enum class RibbonTab { Home, Insert, Draw, History, Review, View, Help };

enum class RibbonDisplayMode {
    FullyHidden,    // 0.0f px  (canvas-only fullscreen)
    Collapsed,      // 58.0f px (headings / tabs only)
    MiniToolbar,    // 96.0f px (slim compact toolbar)
    FullRibbon      // 160.0f px (full rich toolbar with large buttons & category footers)
};

class RibbonBar {
public:
    RibbonTab activeTab = RibbonTab::Draw;
    RibbonTab previousTab = RibbonTab::Draw;
    RibbonDisplayMode displayMode = RibbonDisplayMode::FullRibbon;
    RibbonDisplayMode previousActiveMode = RibbonDisplayMode::FullRibbon;
    float animatedHeight = 160.0f;
    float tabTransitionTimer = 1.0f;
    bool isCollapsed = false; // legacy flag kept in sync
    bool isCollapsedPopupOpen = false;
    bool showDemoOverlay = false;

    // Fluent Draw Toolbar Model & State
    PresetManager presetManager;
    bool drawWithTouch = false;
    bool rulerEnabled = false;
    bool autoShapesEnabled = false;
    bool isInsertSpaceActive = false;
    bool isStrokeEraser = true;
    float eraserSizeMm = 6.0f;
    DocumentSession* currentSession = nullptr;

    // Invert Canvas (canvas dark mode + ink color inversion toggle)
    bool isCanvasInverted = false;

    // Advanced Document Options side panel
    bool advancedOptionsOpen = false;
    float advPanelAnimX = 0.0f; // 0.0=fully hidden offset, 1.0=fully open (lerped)

    // --- Legacy compatibility wrappers ---
    static bool IconButton(
        const char* strId,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        const ThemeManager& theme,
        bool isActive = false,
        bool flipHorizontal = false,
        ImVec2 size = ImVec2(56, 48)
    ) {
        return FolioUI::ToolbarControls::RenderLargeButton(
            strId, iconTex, label, tooltip, theme, isActive, flipHorizontal, size
        );
    }

    void SetDisplayMode(RibbonDisplayMode mode) {
        if (mode != RibbonDisplayMode::Collapsed && mode != RibbonDisplayMode::FullyHidden) {
            previousActiveMode = mode;
        }
        displayMode = mode;
        isCollapsed = (displayMode == RibbonDisplayMode::Collapsed);
        if (displayMode != RibbonDisplayMode::Collapsed) {
            isCollapsedPopupOpen = false;
        }
    }

    void ToggleCollapse() {
        if (displayMode == RibbonDisplayMode::Collapsed) {
            SetDisplayMode(previousActiveMode);
        } else {
            SetDisplayMode(RibbonDisplayMode::Collapsed);
        }
    }

    void CloseCollapsedPopup() {
        isCollapsedPopupOpen = false;
    }

    void CycleDisplayMode() {
        if (displayMode == RibbonDisplayMode::FullRibbon) {
            SetDisplayMode(RibbonDisplayMode::MiniToolbar);
        } else if (displayMode == RibbonDisplayMode::MiniToolbar) {
            SetDisplayMode(RibbonDisplayMode::Collapsed);
        } else if (displayMode == RibbonDisplayMode::Collapsed) {
            SetDisplayMode(RibbonDisplayMode::FullyHidden);
        } else {
            SetDisplayMode(RibbonDisplayMode::FullRibbon);
        }
    }

    float GetTargetHeight() const {
        if (displayMode == RibbonDisplayMode::FullyHidden) {
            return 0.0f;
        }
        if (displayMode == RibbonDisplayMode::Collapsed) {
            return 58.0f;
        }
        if (displayMode == RibbonDisplayMode::MiniToolbar) {
            return 94.0f;
        }
        return 154.0f;
    }

    float GetCurrentHeight() const {
        return std::round(animatedHeight);
    }

    float GetAnimatedHeight() const {
        return std::round(animatedHeight);
    }

    bool IsAnimating() const {
        return std::abs(animatedHeight - GetTargetHeight()) > 0.5f;
    }

    void Render(float width, AppViewMode& outViewMode, CanvasEngine& canvas, InputStateMachine& inputSM, const ThemeManager& theme, DocumentSession* session = nullptr) {
        if (session) currentSession = session;
        float dt = ImGui::GetIO().DeltaTime;
        if (dt > 0.0f && dt < 0.1f) {
            animatedHeight += (GetTargetHeight() - animatedHeight) * (1.0f - std::exp(-dt * 20.0f));
            if (std::abs(animatedHeight - GetTargetHeight()) < 0.25f) {
                animatedHeight = GetTargetHeight();
            }
        }
        if (tabTransitionTimer < 1.0f) {
            tabTransitionTimer += dt * 7.0f;
            if (tabTransitionTimer > 1.0f) tabTransitionTimer = 1.0f;
        }

        if (animatedHeight <= 0.5f) return;
        if (width <= 0.0f) width = ImGui::GetContentRegionAvail().x;

        // 1. Unified Modern Ribbon Tabs Row ("File" + All Ribbon Tabs)
        // Fixed slot widths computed via FontRibbonBoldLarge so tabs NEVER shift when active tab changes!
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        struct TabDef {
            const char* name;
            bool isFile;
            RibbonTab tabEnum;
        };

        const TabDef tabs[] = {
            { "File",    true,  RibbonTab::Home },
            { "Home",    false, RibbonTab::Home },
            { "Insert",  false, RibbonTab::Insert },
            { "Draw",    false, RibbonTab::Draw },
            { "View",    false, RibbonTab::View }
        };
        constexpr int numTabs = static_cast<int>(sizeof(tabs) / sizeof(tabs[0]));

        float tabWidths[numTabs];
        float boldTextWidths[numTabs];
        float totalTabsWidth = 0.0f;
        const float tabSpacing = 4.0f;

        ImFont* boldMeasureFont = FolioTheme::FontRibbonBoldLarge ? FolioTheme::FontRibbonBoldLarge : FolioTheme::FontRibbonLarge;
        ImGui::PushFont(boldMeasureFont);
        for (int i = 0; i < numTabs; ++i) {
            boldTextWidths[i] = ImGui::CalcTextSize(tabs[i].name).x;
            tabWidths[i] = std::max(boldTextWidths[i] + 28.0f, tabs[i].isFile ? 76.0f : 86.0f);
            totalTabsWidth += tabWidths[i];
            if (i > 0) totalTabsWidth += tabSpacing;
        }
        ImGui::PopFont();

        // Center the ribbon category selection row across ribbon width
        float startX = (width - totalTabsWidth) * 0.5f;
        if (startX < 10.0f) startX = 10.0f;
        if (startX + totalTabsWidth > width - 200.0f) {
            startX = std::max(10.0f, width - 200.0f - totalTabsWidth);
        }

        ImGui::SetCursorPos(ImVec2(startX, 4.0f));

        for (int i = 0; i < numTabs; ++i) {
            const auto& tab = tabs[i];
            bool isSelected = tab.isFile 
                ? (outViewMode == AppViewMode::NotebookHub)
                : (activeTab == tab.tabEnum && (displayMode != RibbonDisplayMode::Collapsed || isCollapsedPopupOpen) && outViewMode == AppViewMode::CanvasWorkspace);

            ImGui::PushID(i);
            ImVec2 tabPos = ImGui::GetCursorScreenPos();

            // FIXED SLOT WIDTH: Pre-measured so slot width NEVER changes when tab is selected or hovered
            float boldTextW = boldTextWidths[i];
            float tabWidth = tabWidths[i];
            float tabHeight = 48.0f;

            // Invisible button for clean interaction without bounding boxes
            bool clicked = ImGui::InvisibleButton(tab.name, ImVec2(tabWidth, tabHeight));
            bool isHovered = ImGui::IsItemHovered();

            if (clicked) {
                if (tab.isFile) {
                    isCollapsedPopupOpen = false;
                    outViewMode = (outViewMode == AppViewMode::NotebookHub) 
                        ? AppViewMode::CanvasWorkspace 
                        : AppViewMode::NotebookHub;
                } else {
                    if (outViewMode == AppViewMode::NotebookHub) outViewMode = AppViewMode::CanvasWorkspace;
                    if (displayMode == RibbonDisplayMode::Collapsed) {
                        if (isCollapsedPopupOpen && activeTab == tab.tabEnum) {
                            // Clicking the already open tab toggles it closed
                            isCollapsedPopupOpen = false;
                        } else {
                            // Open floating popup for this tab without moving the canvas
                            previousTab = activeTab;
                            activeTab = tab.tabEnum;
                            isCollapsedPopupOpen = true;
                            tabTransitionTimer = 1.0f;
                        }
                    } else {
                        if (activeTab == tab.tabEnum) {
                            ToggleCollapse();
                        } else {
                            if (activeTab != tab.tabEnum) {
                                previousTab = activeTab;
                                activeTab = tab.tabEnum;
                                tabTransitionTimer = 0.0f; // Smooth fade-in
                            }
                            if (displayMode == RibbonDisplayMode::FullyHidden) {
                                SetDisplayMode(previousActiveMode);
                            }
                        }
                    }
                }
            }

            // Double-clicking a tab pins/uncollapses into FullRibbon mode
            if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (displayMode == RibbonDisplayMode::Collapsed) {
                    isCollapsedPopupOpen = false;
                    SetDisplayMode(RibbonDisplayMode::FullRibbon);
                }
            }

            // Draw Clean Modern Typography
            ImFont* font = isSelected ? FolioTheme::FontRibbonBoldLarge : FolioTheme::FontRibbonLarge;
            ImGui::PushFont(font);
            ImVec2 currentTextSize = ImGui::CalcTextSize(tab.name);
            float textX = tabPos.x + (tabWidth - currentTextSize.x) * 0.5f;
            float textY = tabPos.y + (tabHeight - currentTextSize.y) * 0.5f - 1.0f;
            ImU32 textCol = isSelected 
                ? ImGui::ColorConvertFloat4ToU32(theme.colorHeaderText)
                : ImGui::ColorConvertFloat4ToU32(isHovered ? theme.colorTabHoverText : theme.colorHeaderTextMuted);

            drawList->AddText(ImVec2(textX, textY), textCol, tab.name);
            ImGui::PopFont();

            // Underline Indicator: Glowing White with a crisp gap above the shelf
            float lineW = boldTextW + 10.0f; // Stable underline width centered in slot!
            float lineX1 = tabPos.x + (tabWidth - lineW) * 0.5f;
            float lineX2 = lineX1 + lineW;
            float lineY = tabPos.y + 49.0f; // Gap between underline and shelf at 58.0f

            if (isSelected) {
                // 1. Soft glowing aura bloom
                drawList->AddLine(
                    ImVec2(lineX1 - 2.0f, lineY),
                    ImVec2(lineX2 + 2.0f, lineY),
                    ImGui::ColorConvertFloat4ToU32(theme.colorTabGlow),
                    5.0f
                );
                // 2. Crisp glowing white core line
                drawList->AddLine(
                    ImVec2(lineX1, lineY),
                    ImVec2(lineX2, lineY),
                    ImGui::ColorConvertFloat4ToU32(theme.colorTabUnderline),
                    3.0f
                );
            } else if (isHovered) {
                // Subtle faint preview indicator on hover
                drawList->AddLine(
                    ImVec2(lineX1 + 4.0f, lineY),
                    ImVec2(lineX2 - 4.0f, lineY),
                    ImGui::ColorConvertFloat4ToU32(theme.colorTabHoverUnderline),
                    2.0f
                );
            }

            ImGui::PopID();
            ImGui::SameLine(0, tabSpacing);
        }

        // 2. Right Controls: Toolbar Studio Demo Launcher & Ribbon Display Mode Controls
        ImGui::SameLine(width - 195.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_Text, showDemoOverlay ? theme.colorHeaderText : theme.colorHeaderTextMuted);
        ImGui::PushFont(FolioTheme::FontRegular);
        if (ImGui::Button("Editor [F6]", ImVec2(90, 48))) {
            showDemoOverlay = !showDemoOverlay;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Open Ribbon Customizer & Layout Editor (F6)");
        }
        ImGui::PopFont();
        ImGui::SameLine(0, 4.0f);

        // Display Mode Toggle Icon & Action
        const char* modeIcon = "^";
        const char* modeTooltip = "Full Ribbon (Click to collapse, right-click for modes)";
        if (displayMode == RibbonDisplayMode::MiniToolbar) {
            modeIcon = "=";
            modeTooltip = "Mini Toolbar (Click to collapse, right-click for modes)";
        } else if (displayMode == RibbonDisplayMode::Collapsed) {
            modeIcon = "v";
            modeTooltip = "Collapsed (Click to expand, right-click for modes)";
        }

        ImGui::PushFont(FolioTheme::FontRibbonLarge);
        bool modeClicked = ImGui::Button(modeIcon, ImVec2(44, 48));
        bool modeRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", modeTooltip);
        }
        ImGui::PopFont();

        ImGui::SameLine(0, 2.0f);
        ImGui::PushFont(FolioTheme::FontRegular);
        bool chevronClicked = ImGui::Button("...", ImVec2(34, 48));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Ribbon Display Modes (Full, Mini, Collapsed, Fullscreen)");
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(4);

        if (modeClicked) {
            ToggleCollapse();
        }
        if (modeRightClicked || chevronClicked) {
            ImGui::OpenPopup("##RibbonDisplayModePopup");
        }

        if (ImGui::BeginPopup("##RibbonDisplayModePopup")) {
            ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
            ImGui::TextUnformatted("Ribbon Display Modes");
            ImGui::PopFont();
            ImGui::Separator();

            if (ImGui::MenuItem("Full Ribbon", "154px", displayMode == RibbonDisplayMode::FullRibbon)) {
                SetDisplayMode(RibbonDisplayMode::FullRibbon);
            }
            if (ImGui::MenuItem("Mini Toolbar", "94px", displayMode == RibbonDisplayMode::MiniToolbar)) {
                SetDisplayMode(RibbonDisplayMode::MiniToolbar);
            }
            if (ImGui::MenuItem("Collapsed (Tabs Only)", "58px", displayMode == RibbonDisplayMode::Collapsed)) {
                SetDisplayMode(RibbonDisplayMode::Collapsed);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Fullscreen Canvas (Hide)", "0px (Ctrl+F1)", displayMode == RibbonDisplayMode::FullyHidden)) {
                SetDisplayMode(RibbonDisplayMode::FullyHidden);
            }
            ImGui::EndPopup();
        }

        // 3. Lower Shelf Subsections Built Using Toolbar Machine
        if (displayMode != RibbonDisplayMode::Collapsed && animatedHeight > 58.5f && outViewMode == AppViewMode::CanvasWorkspace) {
            float shelfH = std::max(0.0f, animatedHeight - 58.0f);
            ImGui::SetCursorPosY(58.0f);
            ImGui::SetCursorPosX(0.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorShelf);
            if (ImGui::BeginChild("##ToolShelf", ImVec2(width, shelfH), false, 
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                RenderShelfContents(width, shelfH, displayMode == RibbonDisplayMode::MiniToolbar, canvas, inputSM, theme);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // Ribbon Baseline / Bottom Edge
        ImVec2 winPos = ImGui::GetWindowPos();
        if (displayMode == RibbonDisplayMode::Collapsed) {
            // Elegant rounded bottom edge in Collapsed mode
            float rounding = 10.0f;
            drawList->AddRect(
                ImVec2(winPos.x, winPos.y - 10.0f),
                ImVec2(winPos.x + width, winPos.y + animatedHeight),
                ImGui::ColorConvertFloat4ToU32(theme.colorBorder),
                rounding,
                ImDrawFlags_RoundCornersBottom,
                1.0f
            );
        } else {
            // Crisp 1px baseline where the ribbon shelf meets the canvas/sidebar
            drawList->AddLine(
                ImVec2(winPos.x, winPos.y + animatedHeight - 1.0f),
                ImVec2(winPos.x + width, winPos.y + animatedHeight - 1.0f),
                ImGui::ColorConvertFloat4ToU32(theme.colorBorder),
                1.0f
            );
        }
    }

    void RenderShelfContents(float width, float shelfH, bool isMini, CanvasEngine& canvas, InputStateMachine& inputSM, const ThemeManager& theme) {

            // HORIZONTAL SUBSECTION SCROLLING VIA MOUSE WHEEL:
            // When hovering the ribbon shelf, vertical or horizontal mouse wheel smoothly scrolls subsections!
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
                float wheel = ImGui::GetIO().MouseWheel != 0.0f ? ImGui::GetIO().MouseWheel : ImGui::GetIO().MouseWheelH;
                if (wheel != 0.0f) {
                    float targetScrollX = ImGui::GetScrollX() - (wheel * 48.0f);
                    ImGui::SetScrollX(targetScrollX);
                }
            }

            // Smooth tab fade transition
            float shelfAlpha = std::clamp(tabTransitionTimer, 0.0f, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shelfAlpha);

            ImGui::PushFont(FolioTheme::FontRibbonSection ? FolioTheme::FontRibbonSection : FolioTheme::FontRegular);
            ImGui::SetCursorPos(ImVec2(16.0f, isMini ? 3.0f : 8.0f));

            // Cached SVGs
            GLuint redoIcon   = g_IconManager.LoadOrGetSVG("ribbon_undo_redo", "assets/icons/Ribbon/undo-redo.svg", 128, true);
            GLuint selectIcon = g_IconManager.LoadOrGetSVG("ribbon_select", "assets/icons/Ribbon/select.svg", 128, true);
            GLuint deleteIcon = g_IconManager.LoadOrGetSVG("ribbon_delete", "assets/icons/Ribbon/delete.svg", 128, true);
            GLuint iconLasso  = g_IconManager.LoadOrGetSVG("lasso",  "assets/icons/lasso.svg", 128, true);
            GLuint iconEraser = g_IconManager.LoadOrGetSVG("eraser", "assets/icons/eraser.svg", 128, true);
            GLuint iconPen      = g_IconManager.LoadOrGetSVG("pen",        "assets/icons/pen.svg", 128, true);
            GLuint iconHigh     = g_IconManager.LoadOrGetSVG("high",       "assets/icons/highlighter.svg", 128, true);

            // Insert Tab SVGs
            GLuint iconPdf      = g_IconManager.LoadOrGetSVG("insert_pdf",     "assets/icons/Ribbon/pdf.svg", 128, true);
            GLuint iconAttach   = g_IconManager.LoadOrGetSVG("insert_attach",  "assets/icons/Ribbon/attach.svg", 128, true);
            GLuint iconTable    = g_IconManager.LoadOrGetSVG("insert_table",   "assets/icons/Ribbon/table.svg", 128, true);
            GLuint iconPicture  = g_IconManager.LoadOrGetSVG("insert_picture", "assets/icons/Ribbon/picture.svg", 128, true);
            GLuint iconAudio    = g_IconManager.LoadOrGetSVG("insert_audio",   "assets/icons/Ribbon/audio.svg", 128, true);
            GLuint iconVideo    = g_IconManager.LoadOrGetSVG("insert_video",   "assets/icons/Ribbon/video.svg", 128, true);
            GLuint iconLink     = g_IconManager.LoadOrGetSVG("insert_link",    "assets/icons/Ribbon/link.svg", 128, true);
            GLuint iconTag      = g_IconManager.LoadOrGetSVG("insert_tag",     "assets/icons/Ribbon/tag.svg", 128, true);
            GLuint iconMath     = g_IconManager.LoadOrGetSVG("insert_math",    "assets/icons/Ribbon/math.svg", 128, true);
            GLuint iconSymbol   = g_IconManager.LoadOrGetSVG("insert_symbol",  "assets/icons/Ribbon/symbol.svg", 128, true);

            if (activeTab == RibbonTab::Home) {
                // SUBSECTION: Clipboard
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_home_clipboard", "Clipboard", theme, isMini);
                    sec.AddSplitButton("paste_home", 0, "Paste", "Paste from clipboard (Ctrl+V)", false,
                        [&]() { /* Paste clipboard action */ },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("Paste", 0, "Ctrl+V", [&]() { /* Paste */ });
                            menu.AddItem("Paste Text Only", 0, "Ctrl+Shift+V", [&]() { /* Text only */ });
                        }
                    );
                    sec.BeginStack();
                    sec.AddSmallButton("cut_home", 0, "Cut", "Cut selection (Ctrl+X)", false, [&]() {});
                    sec.AddSmallButton("copy_home", 0, "Copy", "Copy selection (Ctrl+C)", false, [&]() {});
                    sec.EndStack();
                    sec.Render();
                }

                // SUBSECTION: History
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_home_history", "History", theme, isMini);
                    sec.BeginStack();
                    sec.AddSmallButton("undo_home", redoIcon, "Undo", "Undo last action (Ctrl+Z)", false,
                        [&]() { /* Undo */ }, false /* flipH = false: points LEFT */);
                    sec.AddSmallButton("redo_home", redoIcon, "Redo", "Redo last action (Ctrl+Y)", false,
                        [&]() { /* Redo */ }, true /* flipH = true: points RIGHT */);
                    sec.EndStack();
                    sec.Render();
                }
            }
            else if (activeTab == RibbonTab::Insert) {
                // -------------------------------------------------------------
                // SECTION 1: Files & Documents (Import PDF, File Attachment)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_insert_files", "Files", theme, isMini);

                    // 1. Import PDF
                    sec.AddSplitButton("btn_insert_pdf", iconPdf, "Import PDF", "Import PDF documents as printout pages or canvas background", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("Insert as Printout Pages...", iconPdf, nullptr, [&]() {});
                            menu.AddItem("Insert as Canvas Background...", iconPdf, nullptr, [&]() {});
                            menu.AddSeparator();
                            menu.AddItem("Insert First Page Only...", 0, nullptr, [&]() {});
                        }
                    );

                    // 2. File Attachment (storage options: Make a copy vs. System-wide path reference)
                    static int s_attachMode = 0; // 0 = Copy in document, 1 = System-wide path reference
                    static char s_attachFilePath[256] = "";
                    sec.AddSplitButton("btn_insert_attach", iconAttach, "Attachment", "Attach a file to this notebook page", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddCustom([&]() {
                                ImGui::TextUnformatted("Attachment Storage Mode:");
                                ImGui::Spacing();
                                ImGui::RadioButton("Make a copy in document", &s_attachMode, 0);
                                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                                ImGui::TextWrapped("Copies file into notebook bundle. Fully portable and self-contained.");
                                ImGui::PopStyleColor();

                                ImGui::Spacing();
                                ImGui::RadioButton("Use system-wide path reference", &s_attachMode, 1);
                                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                                ImGui::TextWrapped("Links to file on disk (e.g. C:\\...). Always accesses live file.");
                                ImGui::PopStyleColor();

                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::Spacing();
                                ImGui::PushItemWidth(220.0f);
                                ImGui::InputTextWithHint("##attach_path", "Selected file path...", s_attachFilePath, sizeof(s_attachFilePath));
                                ImGui::PopItemWidth();
                                ImGui::SameLine(0, 6.0f);
                                if (ImGui::Button("Browse...")) {
                                    // Open file browser
                                }
                                ImGui::Spacing();
                                if (ImGui::Button("Attach to Canvas Page", ImVec2(-1, 26.0f))) {
                                    ImGui::CloseCurrentPopup();
                                }
                            });
                        }
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 2: Tables
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_insert_tables", "Tables", theme, isMini);

                    static int s_hoverTableCols = 1;
                    static int s_hoverTableRows = 1;
                    static int s_customCols = 4;
                    static int s_customRows = 3;

                    sec.AddSplitButton("btn_insert_table", iconTable, "Table", "Insert a table grid onto the page", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddCustom([&]() {
                                ImGui::Text("Insert Table: %d x %d", s_hoverTableCols, s_hoverTableRows);
                                ImGui::Spacing();

                                // Interactive 8x8 Grid Hover Matrix
                                ImDrawList* gDl = ImGui::GetWindowDrawList();
                                ImVec2 startPos = ImGui::GetCursorScreenPos();
                                const float cellSize = 18.0f;
                                const float cellGap = 3.0f;
                                const int maxC = 8;
                                const int maxR = 8;

                                ImVec2 totalGridSz(maxC * (cellSize + cellGap), maxR * (cellSize + cellGap));
                                ImGui::InvisibleButton("##table_grid_matrix", totalGridSz);
                                bool gridHovered = ImGui::IsItemHovered();
                                ImVec2 mPos = ImGui::GetMousePos();

                                if (gridHovered) {
                                    int col = static_cast<int>((mPos.x - startPos.x) / (cellSize + cellGap)) + 1;
                                    int row = static_cast<int>((mPos.y - startPos.y) / (cellSize + cellGap)) + 1;
                                    s_hoverTableCols = std::clamp(col, 1, maxC);
                                    s_hoverTableRows = std::clamp(row, 1, maxR);
                                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                        ImGui::CloseCurrentPopup();
                                    }
                                }

                                for (int r = 1; r <= maxR; ++r) {
                                    for (int c = 1; c <= maxC; ++c) {
                                        float cx = startPos.x + (c - 1) * (cellSize + cellGap);
                                        float cy = startPos.y + (r - 1) * (cellSize + cellGap);
                                        bool isHighlighted = (c <= s_hoverTableCols && r <= s_hoverTableRows);
                                        ImU32 cellBg = isHighlighted 
                                            ? ImGui::ColorConvertFloat4ToU32(theme.colorPrimary) 
                                            : ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
                                        ImU32 cellBorder = isHighlighted
                                            ? ImGui::ColorConvertFloat4ToU32(theme.colorPrimary)
                                            : ImGui::ColorConvertFloat4ToU32(theme.colorBorder);
                                        gDl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cellSize, cy + cellSize), cellBg, 2.0f);
                                        gDl->AddRect(ImVec2(cx, cy), ImVec2(cx + cellSize, cy + cellSize), cellBorder, 2.0f);
                                    }
                                }

                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::Spacing();
                                ImGui::TextUnformatted("Custom Dimensions:");
                                ImGui::PushItemWidth(65.0f);
                                ImGui::InputInt("Cols##t_c", &s_customCols);
                                ImGui::SameLine(0, 8.0f);
                                ImGui::InputInt("Rows##t_r", &s_customRows);
                                ImGui::PopItemWidth();
                                s_customCols = std::clamp(s_customCols, 1, 30);
                                s_customRows = std::clamp(s_customRows, 1, 100);
                                if (ImGui::Button("Insert Custom Table", ImVec2(-1, 24.0f))) {
                                    ImGui::CloseCurrentPopup();
                                }
                            });
                        }
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 3: Media (Picture, Audio, Video)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_insert_media", "Media", theme, isMini);

                    // 4. Picture (From File or Online)
                    static char s_pictureUrl[256] = "";
                    sec.AddSplitButton("btn_insert_pic", iconPicture, "Picture", "Insert an image onto the page", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("From Local File...", iconPicture, nullptr, [&]() {});
                            menu.AddCustom([&]() {
                                ImGui::Separator();
                                ImGui::TextUnformatted("From Online Web URL:");
                                ImGui::PushItemWidth(220.0f);
                                ImGui::InputTextWithHint("##pic_url", "https://... image URL", s_pictureUrl, sizeof(s_pictureUrl));
                                ImGui::PopItemWidth();
                                if (ImGui::Button("Insert Image from URL", ImVec2(-1, 24.0f))) {
                                    ImGui::CloseCurrentPopup();
                                }
                            });
                            menu.AddSeparator();
                            menu.AddItem("Paste from Clipboard", 0, "Ctrl+V", [&]() {});
                        }
                    );

                    // 5. Audio ("vider")
                    sec.AddSplitButton("btn_insert_audio", iconAudio, "Audio", "Insert an audio note or audio track", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("Record Audio Note (Microphone)", iconAudio, nullptr, [&]() {});
                            menu.AddItem("From Audio File... (MP3, WAV, M4A)", iconAudio, nullptr, [&]() {});
                        }
                    );

                    // 6. Video
                    static char s_videoUrl[256] = "";
                    sec.AddSplitButton("btn_insert_video", iconVideo, "Video", "Insert a video clip or web video stream", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("From Video File... (MP4, MKV, WebM)", iconVideo, nullptr, [&]() {});
                            menu.AddCustom([&]() {
                                ImGui::Separator();
                                ImGui::TextUnformatted("Online Video Stream:");
                                ImGui::PushItemWidth(220.0f);
                                ImGui::InputTextWithHint("##vid_url", "YouTube, Vimeo, or Web URL...", s_videoUrl, sizeof(s_videoUrl));
                                ImGui::PopItemWidth();
                                if (ImGui::Button("Insert Web Video", ImVec2(-1, 24.0f))) {
                                    ImGui::CloseCurrentPopup();
                                }
                            });
                        }
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 4: Links & Tags
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_insert_links_tags", "Links & Tags", theme, isMini);

                    // 7. Link (can link to anything: web, page, file)
                    static int s_linkTargetType = 0; // 0=Web, 1=Canvas Page, 2=File/Folder
                    static char s_linkDisplayText[128] = "";
                    static char s_linkAddress[256] = "";

                    sec.AddSplitButton("btn_insert_link", iconLink, "Link", "Create a hyperlink to a web page, file, or canvas page", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddCustom([&]() {
                                ImGui::TextUnformatted("Hyperlink Target:");
                                ImGui::RadioButton("Web URL", &s_linkTargetType, 0);
                                ImGui::SameLine(0, 10.0f);
                                ImGui::RadioButton("Canvas Page", &s_linkTargetType, 1);
                                ImGui::SameLine(0, 10.0f);
                                ImGui::RadioButton("Local File", &s_linkTargetType, 2);

                                ImGui::Spacing();
                                ImGui::TextUnformatted("Text to Display:");
                                ImGui::PushItemWidth(220.0f);
                                ImGui::InputTextWithHint("##link_display", "Display label...", s_linkDisplayText, sizeof(s_linkDisplayText));

                                ImGui::Spacing();
                                ImGui::TextUnformatted("Target Address / Path:");
                                const char* hint = (s_linkTargetType == 0) ? "https://..." : (s_linkTargetType == 1 ? "Page Title or GUID..." : "C:\\Path\\to\\file...");
                                ImGui::InputTextWithHint("##link_addr", hint, s_linkAddress, sizeof(s_linkAddress));
                                ImGui::PopItemWidth();

                                ImGui::Spacing();
                                if (ImGui::Button("Insert Hyperlink", ImVec2(-1, 26.0f))) {
                                    ImGui::CloseCurrentPopup();
                                }
                            });
                        }
                    );

                    // 8. Tag
                    sec.AddSplitButton("btn_insert_tag", iconTag, "Tag", "Mark notes with searchable visual tags", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("[ ]  To-Do Checkbox", 0, nullptr, [&]() {});
                            menu.AddItem("[*]  Important (Star)", 0, nullptr, [&]() {});
                            menu.AddItem("[?]  Question Mark", 0, nullptr, [&]() {});
                            menu.AddItem("[!]  Remember / Idea", 0, nullptr, [&]() {});
                            menu.AddItem("[~]  Highlight Marker", 0, nullptr, [&]() {});
                            menu.AddItem("[#]  Definition / Term", 0, nullptr, [&]() {});
                        }
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 5: Math & Symbols
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_insert_symbols", "Symbols", theme, isMini);

                    // 9. Math
                    static char s_latexInput[256] = "E = mc^2";
                    sec.AddSplitButton("btn_insert_math", iconMath, "Math", "Insert mathematical equation or formula", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("Quadratic: x = (-b +- sqrt(b^2 - 4ac)) / 2a", 0, nullptr, [&]() {});
                            menu.AddItem("Pythagorean: a^2 + b^2 = c^2", 0, nullptr, [&]() {});
                            menu.AddItem("Circle Area: A = pi * r^2", 0, nullptr, [&]() {});
                            menu.AddItem("Fraction: a / b", 0, nullptr, [&]() {});
                            menu.AddItem("Summation: sum(x_i)", 0, nullptr, [&]() {});
                            menu.AddItem("Integral: int f(x) dx", 0, nullptr, [&]() {});
                            menu.AddSeparator();
                            menu.AddCustom([&]() {
                                ImGui::TextUnformatted("Custom LaTeX / Formula:");
                                ImGui::PushItemWidth(240.0f);
                                ImGui::InputText("##latex_box", s_latexInput, sizeof(s_latexInput));
                                ImGui::PopItemWidth();
                                if (ImGui::Button("Insert Equation", ImVec2(-1, 24.0f))) {
                                    ImGui::CloseCurrentPopup();
                                }
                            });
                        }
                    );

                    // 10. Symbols
                    sec.AddSplitButton("btn_insert_symbols", iconSymbol, "Symbols", "Insert special symbols and characters", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddCustom([&]() {
                                ImGui::TextUnformatted("Symbol Palette (Click to insert):");
                                ImGui::Spacing();
                                const char* syms[] = {
                                    "+-", "*", "/", "!=", "~=", "<=", ">=", "inf", "sqrt",
                                    "alpha", "beta", "gamma", "delta", "theta", "lambda", "mu", "pi", "sigma", "omega",
                                    "<-", "->", "^", "v", "<->", "=>",
                                    "$", "EUR", "GBP", "YEN", "(C)", "(R)", "(TM)", "deg", "*"
                                };
                                const int cols = 6;
                                for (int i = 0; i < (int)IM_ARRAYSIZE(syms); ++i) {
                                    if (i % cols != 0) ImGui::SameLine(0, 4.0f);
                                    char btnId[32]; snprintf(btnId, sizeof(btnId), "%s##sym_%d", syms[i], i);
                                    if (ImGui::Button(btnId, ImVec2(40.0f, 26.0f))) {
                                        ImGui::CloseCurrentPopup();
                                    }
                                }
                            });
                        }
                    );

                    sec.Render();
                }
            }
            else if (activeTab == RibbonTab::Draw) {
                auto& activePen = inputSM.palette.GetActivePen();

                // -------------------------------------------------------------
                // SECTION 1: Undo / History (Horizontal Side-by-Side: Left to Right)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_history", "History", theme, isMini);
                    sec.AddLargeButton("undo_draw", redoIcon, "Undo", "Undo last stroke (Ctrl+Z)", false,
                        [&]() { /* Undo action */ }, false /* flipH = false: points LEFT */, ImVec2(46.0f, 58.0f));
                    sec.AddLargeButton("redo_draw", redoIcon, "Redo", "Redo stroke (Ctrl+Y)", false,
                        [&]() { /* Redo action */ }, true /* flipH = true: points RIGHT */, ImVec2(46.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 2: Selection (Delete + Object Select & Lasso Select)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_selection", "Selection", theme, isMini);
                    bool isLasso = (inputSM.currentAction == InteractionState::Selecting);
                    bool isPointer = (!isLasso && inputSM.currentAction != InteractionState::Inking && inputSM.currentAction != InteractionState::Eraser);

                    // Square Delete button right before Select
                    sec.AddLargeButton("delete_draw", deleteIcon, "Delete", "Delete Selection: Delete selected strokes or objects (Del)", false,
                        [&]() {
                            if (currentSession) {
                                canvas.DeleteSelectedObjects(currentSession);
                            }
                        }, false, ImVec2(48.0f, 58.0f));

                    sec.AddLargeButton("select_draw", selectIcon, "Select", "Object Selection & Transform (Pointer)", isPointer,
                        [&]() {
                            inputSM.activeTool = InteractionState::Idle;
                            inputSM.currentAction = InteractionState::Idle;
                        }, false, ImVec2(48.0f, 58.0f));

                    sec.AddLargeButton("lasso_draw", iconLasso, "Lasso", "Freehand Lasso Selection", isLasso,
                        [&]() {
                            inputSM.activeTool = InteractionState::Selecting;
                            inputSM.currentAction = InteractionState::Selecting;
                        }, false, ImVec2(48.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 3: Drawing Tools (Eraser Split + Pen Presets + Add Tool)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_tools", "Drawing Tools", theme, isMini);

                    // 1. Eraser Split Dropdown
                    bool isEraser = (inputSM.currentAction == InteractionState::Eraser);
                    const char* eraserLabel = isStrokeEraser ? "Stroke Eraser" : "Point Eraser";
                    const char* eraserTooltip = isStrokeEraser ? "Vector Stroke Eraser (click to erase whole stroke)" : "Simple Eraser (erase by radius)";

                    sec.AddSplitButton("eraser", iconEraser, eraserLabel, eraserTooltip, isEraser,
                        [&]() { 
                            inputSM.activeTool = InteractionState::Eraser;
                            inputSM.currentAction = InteractionState::Eraser;
                            inputSM.isStrokeEraser = isStrokeEraser;
                            inputSM.eraserRadiusMm = eraserSizeMm * 0.5f;
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Eraser Mode");
                            menu.AddItem("Stroke Eraser", iconEraser, "", [&]() {
                                isStrokeEraser = true;
                                inputSM.activeTool = InteractionState::Eraser;
                                inputSM.currentAction = InteractionState::Eraser;
                                inputSM.isStrokeEraser = true;
                            }, isStrokeEraser);
                            menu.AddItem("Simple Eraser (Point)", 0, "", [&]() {
                                isStrokeEraser = false;
                                inputSM.activeTool = InteractionState::Eraser;
                                inputSM.currentAction = InteractionState::Eraser;
                                inputSM.isStrokeEraser = false;
                                inputSM.eraserRadiusMm = eraserSizeMm * 0.5f;
                            }, !isStrokeEraser);

                            if (!isStrokeEraser) {
                                menu.AddSeparator();
                                menu.AddHeader("Eraser Size");
                                menu.AddItem("Small (2.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 2.0f;
                                    inputSM.eraserRadiusMm = 1.0f;
                                }, std::abs(eraserSizeMm - 2.0f) < 0.5f);
                                menu.AddItem("Medium (6.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 6.0f;
                                    inputSM.eraserRadiusMm = 3.0f;
                                }, std::abs(eraserSizeMm - 6.0f) < 0.5f);
                                menu.AddItem("Large (12.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 12.0f;
                                    inputSM.eraserRadiusMm = 6.0f;
                                }, std::abs(eraserSizeMm - 12.0f) < 0.5f);
                                menu.AddItem("Extra Large (20.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 20.0f;
                                    inputSM.eraserRadiusMm = 10.0f;
                                }, std::abs(eraserSizeMm - 20.0f) < 0.5f);
                            }
                        }, false, ImVec2(80.0f, 58.0f)
                    );

                    // 2. Preset Nib Carousel (with drag & drop reordering)
                    for (int pIdx = 0; pIdx < static_cast<int>(presetManager.presets.size()); pIdx++) {
                        auto& preset = presetManager.presets[pIdx];
                        bool isActive = (inputSM.currentAction == InteractionState::Inking &&
                                         presetManager.activePresetId == preset.id);

                        sec.AddPenNibControl(
                            preset.id.c_str(),
                            preset,
                            isActive,
                            // On Select
                            [&, id = preset.id]() {
                                auto* p = presetManager.FindPreset(id);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.activeTool = InteractionState::Inking;
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            },
                            // On Custom Change
                            [&](PenPreset& p) {
                                if (presetManager.activePresetId == p.id) {
                                    presetManager.ApplyPreset(p, activePen);
                                }
                            },
                            // On Delete
                            [&](const std::string& id) {
                                presetManager.DeletePreset(id);
                                auto* activeP = presetManager.GetActivePreset();
                                if (activeP) {
                                    presetManager.ApplyPreset(*activeP, activePen);
                                }
                            },
                            ImVec2(42.0f, 58.0f),
                            pIdx,
                            static_cast<int>(presetManager.presets.size()),
                            [&](int fromIdx, int toIdx) {
                                presetManager.ReorderPreset(fromIdx, toIdx);
                            }
                        );
                    }

                    // 3. "+ Add" Split Button (Quick Add / Menu to pick tool type)
                    sec.AddSplitButton("add_tool", 0, "+ Add", "Add a new pen, highlighter, or pencil preset", false,
                        [&]() {
                            std::string newId = presetManager.AddPreset(PenType::Pen);
                            auto* p = presetManager.FindPreset(newId);
                            if (p) {
                                presetManager.ApplyPreset(*p, activePen);
                                inputSM.activeTool = InteractionState::Inking;
                                inputSM.currentAction = InteractionState::Inking;
                            }
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Add Drawing Tool");
                            menu.AddItem("Add Pen", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Pen);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.activeTool = InteractionState::Inking;
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Fountain Pen", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Fountain);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.activeTool = InteractionState::Inking;
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Pencil", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Pencil);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.activeTool = InteractionState::Inking;
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Brush", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Brush);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.activeTool = InteractionState::Inking;
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Highlighter", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Highlighter);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.activeTool = InteractionState::Inking;
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Laser Pointer", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::LaserPointer);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.activeTool = InteractionState::Inking;
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                        }, false, ImVec2(60.0f, 58.0f)
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION: Customize (Color Changer on left + Longer Thickness & Opacity Sliders)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_customize", "Customize", theme, isMini);

                    auto* activePreset = presetManager.GetActivePreset();
                    if (activePreset) {
                        sec.AddWidget([&]() {
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            ImVec2 startPos = ImGui::GetCursorScreenPos();

                            // 1. Color Changer Button (Left side) - Modern centered circular disc
                            float colorBtnSize = isMini ? 28.0f : 38.0f;
                            float colorBtnY = startPos.y + (isMini ? 2.0f : (58.0f - colorBtnSize) * 0.5f);
                            ImVec2 colorBtnPos(startPos.x, colorBtnY);

                            ImGui::SetCursorScreenPos(colorBtnPos);
                            std::string colorPopupId = "##pen_custom_color_popup";

                            // Invisible button for interaction
                            bool colorBtnClicked = ImGui::InvisibleButton("##pen_color_btn", ImVec2(colorBtnSize, colorBtnSize));
                            bool colorBtnHovered = ImGui::IsItemHovered();
                            bool colorBtnPressed = ImGui::IsItemActive();

                            // Modern centered swatch disc
                            ImVec2 swatchCenter(colorBtnPos.x + colorBtnSize * 0.5f, colorBtnPos.y + colorBtnSize * 0.5f);
                            float outerRadius = (colorBtnSize * 0.5f) - 1.0f;
                            float innerRadius = outerRadius - 3.0f;

                            // Interactive subtle background halo
                            if (colorBtnPressed) {
                                ImU32 darkOutline = (theme.colorBg.x < 0.5f) ? IM_COL32(10, 11, 14, 255) : IM_COL32(38, 42, 50, 240);
                                drawList->AddCircleFilled(swatchCenter, outerRadius + 1.0f, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover));
                                drawList->AddCircle(swatchCenter, outerRadius + 1.0f, darkOutline, 0, 1.8f);
                            } else if (colorBtnHovered) {
                                drawList->AddCircleFilled(swatchCenter, outerRadius + 1.0f, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover));
                                drawList->AddCircle(swatchCenter, outerRadius + 1.0f, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 0, 1.0f);
                            } else {
                                drawList->AddCircle(swatchCenter, outerRadius, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 0, 1.0f);
                            }

                            // Active ink color circle
                            drawList->AddCircleFilled(swatchCenter, innerRadius, ImGui::ColorConvertFloat4ToU32(activePreset->color));
                            // Subtle inner specular sheen on the upper rim
                            drawList->AddCircle(swatchCenter, innerRadius, IM_COL32(255, 255, 255, 50), 0, 1.0f);

                            if (colorBtnHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                                ImGui::BeginTooltip();
                                ImGui::TextUnformatted("Custom Color Picker: Choose any color");
                                ImGui::EndTooltip();
                            }

                            if (colorBtnClicked) {
                                ImGui::OpenPopup(colorPopupId.c_str());
                            }

                            // Popup with full custom color picker
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
                            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
                            ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.colorPanel);
                            ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

                            if (ImGui::BeginPopup(colorPopupId.c_str())) {
                                ImGui::TextUnformatted("Choose Custom Color");
                                ImGui::Separator();
                                ImGui::Spacing();

                                bool colorChanged = false;
                                if (ImGui::ColorPicker4("##full_pen_color_picker", (float*)&activePreset->color,
                                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueBar)) {
                                    colorChanged = true;
                                }

                                if (colorChanged) {
                                    presetManager.ApplyPreset(*activePreset, activePen);
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::PopStyleColor(2);
                            ImGui::PopStyleVar(2);

                            // 2. Sliders (Thickness & Opacity cleanly stacked without clashing)
                            float sliderX = colorBtnPos.x + colorBtnSize + 8.0f;
                            float sliderW = 140.0f;
                            float topSliderY = startPos.y + (isMini ? 4.0f : 7.0f);
                            float bottomSliderY = startPos.y + 32.0f;

                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                            ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f)); // Compact 20px frame height
                            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.colorItemHover);
                            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme.colorItemSelected);
                            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme.colorItemSelected);
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme.colorTextMuted);
                            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, theme.colorText);

                            // Top: Thickness Slider
                            ImGui::SetCursorScreenPos(ImVec2(sliderX, topSliderY));
                            ImGui::PushItemWidth(sliderW);
                            bool thicknessChanged = false;
                            if (ImGui::SliderFloat("##slider_thick", &activePreset->thicknessMm, 0.2f, 25.0f, "Thick: %.1f mm")) {
                                thicknessChanged = true;
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                                ImGui::BeginTooltip();
                                ImGui::Text("Stroke Thickness: %.1f mm", activePreset->thicknessMm);
                                ImGui::EndTooltip();
                            }
                            ImGui::PopItemWidth();

                            // Bottom: Opacity Slider (directly under thickness with clean 5px gap, no clashing)
                            if (!isMini) {
                                ImGui::SetCursorScreenPos(ImVec2(sliderX, bottomSliderY));
                                ImGui::PushItemWidth(sliderW);
                                float opacityPct = activePreset->opacity * 100.0f;
                                if (ImGui::SliderFloat("##slider_opacity", &opacityPct, 10.0f, 100.0f, "Opacity: %.0f%%")) {
                                    activePreset->opacity = opacityPct / 100.0f;
                                    presetManager.ApplyPreset(*activePreset, activePen);
                                }
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Ink Opacity: %.0f%%", activePreset->opacity * 100.0f);
                                    ImGui::EndTooltip();
                                }
                                ImGui::PopItemWidth();
                            }

                            if (thicknessChanged) {
                                presetManager.ApplyPreset(*activePreset, activePen);
                            }

                            ImGui::PopStyleColor(5);
                            ImGui::PopStyleVar(3);

                            // Advance cursor so section sizing accurately wraps these controls
                            ImGui::SetCursorScreenPos(ImVec2(sliderX + sliderW, startPos.y));
                        }, 6.0f);
                    }

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 4: Input Mode (Draw with Touch)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_input", "Input", theme, isMini);
                    sec.AddLargeButton("draw_touch", 0, "Touch", "Draw with Touch: Toggle finger inking vs canvas pan/zoom (palm rejection)", drawWithTouch,
                        [&]() { drawWithTouch = !drawWithTouch; }, false, ImVec2(50.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 5: Stencils (Ruler)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_stencils", "Stencils", theme, isMini);
                    sec.AddLargeButton("stencil_ruler", 0, "Ruler", "Ruler: Toggle digital straightedge ruler overlay", rulerEnabled,
                        [&]() { rulerEnabled = !rulerEnabled; }, false, ImVec2(48.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 6: Edit (Insert Space)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_edit", "Edit", theme, isMini);

                    sec.AddLargeButton("insert_space", 0, "Insert Space", "Insert Space: Insert vertical space between notes", isInsertSpaceActive,
                        [&]() { isInsertSpaceActive = !isInsertSpaceActive; }, false, ImVec2(74.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 7: Shapes (Shapes Split Dropdown + Automatic Shapes Toggle)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_shapes", "Shapes", theme, isMini);

                    sec.AddSplitButton("shapes_picker", 0, "Shapes", "Insert geometric vector shape", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Insert 2D Shape");
                            menu.AddItem("Line", 0, "", [&]() {});
                            menu.AddItem("Arrow", 0, "", [&]() {});
                            menu.AddItem("Rectangle", 0, "", [&]() {});
                            menu.AddItem("Ellipse", 0, "", [&]() {});
                            menu.AddItem("Triangle", 0, "", [&]() {});
                            menu.AddItem("Star", 0, "", [&]() {});
                            menu.AddItem("Coordinate Axes", 0, "", [&]() {});
                        }, false, ImVec2(64.0f, 58.0f)
                    );

                    sec.AddLargeButton("auto_shapes", 0, "Auto", "Automatic Shapes: Snaps freehand geometric sketches into clean vector shapes", autoShapesEnabled,
                        [&]() { autoShapesEnabled = !autoShapesEnabled; }, false, ImVec2(48.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 8: Math (Ink to Math)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_math", "Math", theme, isMini);
                    sec.AddLargeButton("ink_to_math", 0, "Math", "Ink to Math: Convert handwritten mathematical expressions to LaTeX / MathML", false,
                        [&]() {}, false, ImVec2(48.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 9: Mode (Full Page View)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_mode", "Mode", theme, isMini);
                    bool isFullPage = (displayMode == RibbonDisplayMode::FullyHidden);
                    sec.AddLargeButton("full_page_view", 0, "Full Page", "Full Page View: Toggle distraction-free canvas mode", isFullPage,
                        [&]() {
                            SetDisplayMode(displayMode == RibbonDisplayMode::FullyHidden ? RibbonDisplayMode::FullRibbon : RibbonDisplayMode::FullyHidden);
                        }, false, ImVec2(64.0f, 58.0f));
                    sec.Render();
                }
            }
            else if (activeTab == RibbonTab::View) {
                // SUBSECTION 1: Zoom (Stacked In/Out, Home 100%, Fit Width)
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_view_zoom", "Zoom", theme, isMini);

                    // Unified Zoom Compound Widget (+ / - stacked on left, 100% Home + Dropdown on right)
                    sec.AddZoomCompound("zoom_compound",
                        [&]() {
                            double newZoom = canvas.transform.zoom * 1.25;
                            if (newZoom > 32.0) newZoom = 32.0;
                            canvas.transform.zoom = newZoom;
                            canvas.isDirty = true;
                            canvas.needsFullRebake = true;
                        },
                        [&]() {
                            double newZoom = canvas.transform.zoom * 0.8;
                            if (newZoom < 0.05) newZoom = 0.05;
                            canvas.transform.zoom = newZoom;
                            canvas.isDirty = true;
                            canvas.needsFullRebake = true;
                        },
                        [&]() {
                            canvas.transform.panXMm = 0.0;
                            canvas.transform.panYMm = 0.0;
                            canvas.transform.zoom = 1.0;
                            canvas.isDirty = true;
                            canvas.needsFullRebake = true;
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Zoom Presets");
                            menu.AddItem("25%", 0, "", [&]() {
                                canvas.transform.zoom = 0.25;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            });
                            menu.AddItem("50%", 0, "", [&]() {
                                canvas.transform.zoom = 0.5;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            });
                            menu.AddItem("75%", 0, "", [&]() {
                                canvas.transform.zoom = 0.75;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            });
                            menu.AddItem("100% — Home", 0, "Ctrl+1", [&]() {
                                canvas.transform.panXMm = 0.0;
                                canvas.transform.panYMm = 0.0;
                                canvas.transform.zoom = 1.0;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            });
                            menu.AddItem("150%", 0, "", [&]() {
                                canvas.transform.zoom = 1.5;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            });
                            menu.AddItem("200%", 0, "", [&]() {
                                canvas.transform.zoom = 2.0;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            });
                            menu.AddItem("300%", 0, "", [&]() {
                                canvas.transform.zoom = 3.0;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            });
                        },
                        "100%",
                        ImVec2(118.0f, 58.0f)
                    );

                    // Fit Width button (dormant — feature placeholder)
                    sec.AddLargeButton("zoom_fit_width", 0, "Fit Width", "Fit Page Width: Zoom to fill canvas width (coming soon)", false,
                        [&]() { /* placeholder */ }, false, ImVec2(68.0f, 58.0f));

                    sec.Render();
                }

                // SUBSECTION 2: Page Setup (Background type menu + Invert Canvas toggle)
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_view_paper", "Page Setup", theme, isMini);

                    // Dynamic label reflecting current paper style
                    const char* bgLabel = "Background";
                    if (canvas.currentPaperStyle == PaperStyle::Grid) bgLabel = "Grid";
                    else if (canvas.currentPaperStyle == PaperStyle::Lined) bgLabel = "Ruled";
                    else if (canvas.currentPaperStyle == PaperStyle::Dotted) bgLabel = "Dotted";
                    else if (canvas.currentPaperStyle == PaperStyle::Blank) bgLabel = "Empty";

                    sec.AddSplitButton("page_bg_style", 0, bgLabel, "Page Background: Change paper pattern, paper color, and line color", false,
                        [&]() {
                            // Cycle through templates on direct click: Blank -> Ruled -> Grid -> Dotted -> Blank
                            if (canvas.currentPaperStyle == PaperStyle::Blank) canvas.currentPaperStyle = PaperStyle::Lined;
                            else if (canvas.currentPaperStyle == PaperStyle::Lined) canvas.currentPaperStyle = PaperStyle::Grid;
                            else if (canvas.currentPaperStyle == PaperStyle::Grid) canvas.currentPaperStyle = PaperStyle::Dotted;
                            else canvas.currentPaperStyle = PaperStyle::Blank;
                            canvas.isDirty = true;
                            canvas.needsFullRebake = true;
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Paper Pattern");
                            menu.AddItem("Empty (Blank)", 0, "", [&]() {
                                canvas.currentPaperStyle = PaperStyle::Blank;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            }, canvas.currentPaperStyle == PaperStyle::Blank);
                            menu.AddItem("Ruled (Lines)", 0, "", [&]() {
                                canvas.currentPaperStyle = PaperStyle::Lined;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            }, canvas.currentPaperStyle == PaperStyle::Lined);
                            menu.AddItem("Grid", 0, "", [&]() {
                                canvas.currentPaperStyle = PaperStyle::Grid;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            }, canvas.currentPaperStyle == PaperStyle::Grid);
                            menu.AddItem("Dotted", 0, "", [&]() {
                                canvas.currentPaperStyle = PaperStyle::Dotted;
                                canvas.isDirty = true;
                                canvas.needsFullRebake = true;
                            }, canvas.currentPaperStyle == PaperStyle::Dotted);

                            menu.AddSeparator();
                            menu.AddHeader(isCanvasInverted ? "Page Background (Dark Shades)" : "Page Background (Light Shades)");

                            // Background color swatches
                            menu.AddCustom([&]() {
                                struct BgPreset { uint8_t r, g, b; const char* tip; };
                                static const BgPreset lightShades[] = {
                                    { 0xFF, 0xFF, 0xFF, "White" },
                                    { 0xFA, 0xF8, 0xF4, "Ivory" },
                                    { 0xF6, 0xEE, 0xDE, "Sepia Cream" },
                                    { 0xF0, 0xF4, 0xFA, "Pale Blue" },
                                    { 0xEE, 0xF6, 0xF0, "Mint Tint" },
                                    { 0xF2, 0xF2, 0xF5, "Soft Grey" }
                                };
                                static const BgPreset darkShades[] = {
                                    { 0x1E, 0x20, 0x26, "Deep Charcoal" },
                                    { 0x16, 0x17, 0x1C, "Night Black" },
                                    { 0x1C, 0x22, 0x2E, "Midnight Navy" },
                                    { 0x1B, 0x24, 0x20, "Dark Spruce" },
                                    { 0x25, 0x20, 0x26, "Dark Plum" },
                                    { 0x28, 0x2A, 0x30, "Slate Grey" }
                                };

                                const BgPreset* shades = isCanvasInverted ? darkShades : lightShades;
                                for (int i = 0; i < 6; i++) {
                                    if (i > 0) ImGui::SameLine(0, 6.0f);
                                    ImVec4 col(shades[i].r / 255.0f, shades[i].g / 255.0f, shades[i].b / 255.0f, 1.0f);
                                    ImGui::PushID(i + 100);
                                    if (FolioUI::ToolbarControls::RenderCircleButton("##bgcol", col, shades[i].tip, theme, false, 24.0f)) {
                                        canvas.canvasBgColor = BLRgba32(shades[i].r, shades[i].g, shades[i].b);
                                        canvas.isDirty = true;
                                        canvas.needsFullRebake = true;
                                    }
                                    ImGui::PopID();
                                }
                            });

                            menu.AddSeparator();
                            menu.AddHeader("Line & Grid Color");

                            // Line / Grid color swatches
                            menu.AddCustom([&]() {
                                struct LinePreset { uint8_t r, g, b; const char* tip; };
                                static const LinePreset lightLines[] = {
                                    { 0xEB, 0xEE, 0xF2, "Subtle Grey" },
                                    { 0xD5, 0xDC, 0xE6, "Soft Slate" },
                                    { 0xC4, 0xD4, 0xE8, "Pale Blue-Grey" },
                                    { 0xDE, 0xD4, 0xC4, "Warm Tan" },
                                    { 0xCA, 0xDE, 0xCE, "Soft Sage" },
                                    { 0x94, 0x9B, 0xA8, "Medium Slate" }
                                };
                                static const LinePreset darkLines[] = {
                                    { 0x34, 0x38, 0x44, "Dim Charcoal" },
                                    { 0x42, 0x48, 0x56, "Medium Slate" },
                                    { 0x2E, 0x3C, 0x4E, "Dark Blue-Grey" },
                                    { 0x3E, 0x38, 0x2E, "Dark Warm Tan" },
                                    { 0x2E, 0x40, 0x34, "Dark Sage" },
                                    { 0x56, 0x5C, 0x6E, "Light Slate" }
                                };

                                const LinePreset* lines = isCanvasInverted ? darkLines : lightLines;
                                for (int i = 0; i < 6; i++) {
                                    if (i > 0) ImGui::SameLine(0, 6.0f);
                                    ImVec4 col(lines[i].r / 255.0f, lines[i].g / 255.0f, lines[i].b / 255.0f, 1.0f);
                                    ImGui::PushID(i + 200);
                                    if (FolioUI::ToolbarControls::RenderCircleButton("##linecol", col, lines[i].tip, theme, false, 24.0f)) {
                                        canvas.gridLineColor = BLRgba32(lines[i].r, lines[i].g, lines[i].b);
                                        canvas.isDirty = true;
                                        canvas.needsFullRebake = true;
                                    }
                                    ImGui::PopID();
                                }
                            });
                        }, false, ImVec2(76.0f, 58.0f)
                    );

                    // Invert Canvas Button (inverts background, lines, and ink colors together)
                    sec.AddLargeButton("invert_canvas", 0, "Invert Canvas",
                        "Invert Canvas Color: Switch canvas to dark mode and invert ink colors for maximum readability",
                        isCanvasInverted,
                        [&]() {
                            isCanvasInverted = !isCanvasInverted;
                            if (isCanvasInverted) {
                                canvas.canvasBgColor = BLRgba32(0x1E, 0x20, 0x26);
                                canvas.gridLineColor = BLRgba32(0x34, 0x38, 0x44);
                                canvas.inkColorInverted = true;
                            } else {
                                canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
                                canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
                                canvas.inkColorInverted = false;
                            }
                            canvas.isDirty = true;
                            canvas.needsFullRebake = true;
                        }, false, ImVec2(78.0f, 58.0f)
                    );

                    sec.Render();
                }

                // SUBSECTION 3: Advanced Document Options (opens sliding side panel)
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_view_adv", "Document", theme, isMini);
                    sec.AddLargeButton("adv_doc_options", 0, "Adv. Options",
                        "Advanced Document Options: Page format, canvas mode, backgrounds, borders, scroll mode and more",
                        advancedOptionsOpen,
                        [&]() {
                            advancedOptionsOpen = !advancedOptionsOpen;
                        },
                        false, ImVec2(80.0f, 58.0f));
                    sec.Render();
                }
            }
            else {
                // Generic tab placeholder section using ToolbarSectionBuilder
                FolioUI::ToolbarSectionBuilder sec("grp_general", "Tools", theme, isMini);
                sec.AddLargeButton("gen_tool", 0, "Ready", "Feature coming soon", false, [&]() {}, false, ImVec2(68.0f, 58.0f));
                sec.Render();
            }

            ImGui::PopFont();
            ImGui::PopStyleVar(); // Pop shelfAlpha
    }

    // ============================================================
    // ADVANCED DOCUMENT OPTIONS PANEL
    // Slides in from the right, covers canvas & sidebar, sits under ribbon.
    // Call this after all other ImGui windows in the frame loop.
    // ============================================================
    void RenderAdvancedOptionsPanel(float screenW, float screenH, float ribbonTopY, float ribbonH,
                                    CanvasEngine& canvas, const ThemeManager& theme) {
        if (!advancedOptionsOpen) {
            advPanelAnimX = std::max(0.0f, advPanelAnimX - ImGui::GetIO().DeltaTime * 8.0f);
        } else {
            advPanelAnimX = std::min(1.0f, advPanelAnimX + ImGui::GetIO().DeltaTime * 8.0f);
        }

        if (advPanelAnimX < 0.005f) return; // Fully hidden: skip rendering

        float panelW = screenW * 0.33f;
        float panelH = screenH - (ribbonTopY + ribbonH);
        float panelY = ribbonTopY + ribbonH;
        float slideOffset = panelW * (1.0f - advPanelAnimX);
        float panelX = screenW - panelW + slideOffset;

        // --- Dim scrim covering left portion ---
        if (advancedOptionsOpen && advPanelAnimX > 0.01f) {
            ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
            ImVec2 scrimMin(0.0f, panelY);
            ImVec2 scrimMax(panelX, panelY + panelH);
            ImU32 scrimColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.28f * advPanelAnimX));
            bgDrawList->AddRectFilled(scrimMin, scrimMax, scrimColor);

            // Dismiss when user clicks outside the panel
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 mousePos = io.MousePos;
                if (mousePos.y >= panelY && mousePos.x < panelX) {
                    advancedOptionsOpen = false;
                }
            }
        }

        // --- Side Panel ---
        ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ImVec2(panelX, panelY));
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.colorPanel);
        ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);

        ImGui::Begin("##AdvDocOptionsPanel", nullptr, panelFlags);

        // Panel title row
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wPos = ImGui::GetWindowPos();
        // Left edge accent bar
        dl->AddRectFilled(ImVec2(wPos.x, wPos.y), ImVec2(wPos.x + 3.0f, wPos.y + panelH),
            IM_COL32(80, 130, 220, 200));

        ImGui::Dummy(ImVec2(0, 2));
        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge ? FolioTheme::FontRibbonBoldLarge : FolioTheme::FontBold);
        ImGui::TextUnformatted("Advanced Document Options");
        ImGui::PopFont();

        // Dismiss X button top-right
        float closeX = panelX + panelW - 36.0f;
        float closeY = panelY + 10.0f;
        ImGui::SameLine(panelW - 38.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorItemHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
        if (ImGui::Button("X##adv_close", ImVec2(28, 28))) {
            advancedOptionsOpen = false;
        }
        ImGui::PopStyleColor(4);
        ImGui::Separator();
        ImGui::Spacing();

        // Helper macro for section headers
        auto SectionHeader = [&](const char* label) -> bool {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Header, theme.colorItemSelected);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme.colorItemHover);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme.colorItemSelected);
            bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor(3);
            return open;
        };

        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorItemText);

        // Keep local invert state synced with canvas
        isCanvasInverted = canvas.inkColorInverted;

        // ── SECTION 1: Canvas View ─────────────────────────────────────
        if (SectionHeader("  Canvas View")) {
            ImGui::Indent(8.0f);

            bool invCanvas = isCanvasInverted;
            if (ImGui::Checkbox("Invert Canvas Mode", &invCanvas)) {
                isCanvasInverted = invCanvas;
                if (isCanvasInverted) {
                    canvas.canvasBgColor = BLRgba32(0x1E, 0x20, 0x26);
                    canvas.gridLineColor = BLRgba32(0x34, 0x38, 0x44);
                    canvas.inkColorInverted = true;
                } else {
                    canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
                    canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
                    canvas.inkColorInverted = false;
                }
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
            ImGui::TextUnformatted("(Dark canvas + inverted ink)");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Text("Current Zoom: %.0f%%", canvas.transform.zoom * 100.0);

            ImGui::Unindent(8.0f);
        }

        // ── SECTION 2: Calibrate Canvas to Real World Scale ────────────
        if (SectionHeader("  Calibrate Canvas (Real World Scale)")) {
            ImGui::Indent(8.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
            ImGui::TextWrapped("Hold a physical millimeter ruler against your screen. Adjust the DPI slider until the on-screen markings match your real ruler. This guarantees 1:1 true physical scale for drafting and 1:1 export.");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Active calibration metrics
            float currentDpi = static_cast<float>(canvas.transform.pixelsPerMm * 25.4);
            float pxPerMm = static_cast<float>(canvas.transform.pixelsPerMm);
            float rulerMmTotal = 50.0f;
            float rulerWidthPx = rulerMmTotal * pxPerMm;

            ImVec2 rPos = ImGui::GetCursorScreenPos();
            float rH = 34.0f;
            ImDrawList* rDl = ImGui::GetWindowDrawList();

            // On-screen calibration ruler (0 to 50 mm)
            rDl->AddRectFilled(rPos, ImVec2(rPos.x + rulerWidthPx, rPos.y + rH),
                ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 4.0f);
            rDl->AddRect(rPos, ImVec2(rPos.x + rulerWidthPx, rPos.y + rH),
                ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 4.0f, 0, 1.0f);

            ImU32 tickCol = ImGui::ColorConvertFloat4ToU32(theme.colorText);
            ImU32 numCol = ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted);

            for (int mm = 0; mm <= 50; mm++) {
                float tickX = rPos.x + mm * pxPerMm;
                float tickH = 6.0f;
                if (mm % 10 == 0) {
                    tickH = 14.0f;
                    char mmStr[8];
                    snprintf(mmStr, sizeof(mmStr), "%d", mm);
                    rDl->AddText(ImVec2(tickX - 3.0f, rPos.y + 16.0f), numCol, mmStr);
                } else if (mm % 5 == 0) {
                    tickH = 10.0f;
                }
                rDl->AddLine(ImVec2(tickX, rPos.y), ImVec2(tickX, rPos.y + tickH), tickCol, (mm % 10 == 0) ? 1.5f : 1.0f);
            }

            ImGui::Dummy(ImVec2(rulerWidthPx, rH + 4.0f));
            ImGui::Spacing();

            // DPI adjustment controls
            ImGui::Text("Screen Calibration: %.1f DPI (%.2f px/mm)", currentDpi, pxPerMm);
            ImGui::PushItemWidth(panelW - 130.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.colorItemHover);
            if (ImGui::SliderFloat("##dpi_slider", &currentDpi, 60.0f, 320.0f, "%.1f DPI")) {
                canvas.transform.SetDPI(currentDpi);
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
            ImGui::PopStyleColor();
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("-##dpi_dn", ImVec2(24, 22))) {
                currentDpi = std::max(60.0f, currentDpi - 1.0f);
                canvas.transform.SetDPI(currentDpi);
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+##dpi_up", ImVec2(24, 22))) {
                currentDpi = std::min(320.0f, currentDpi + 1.0f);
                canvas.transform.SetDPI(currentDpi);
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }

            if (ImGui::Button("Reset to Standard 96.0 DPI", ImVec2(panelW - 52.0f, 24.0f))) {
                canvas.transform.SetDPI(96.0f);
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }

            ImGui::Unindent(8.0f);
        }

        // ── SECTION 3: Page Background (Two Palettes: Normal & Inverted) ─
        if (SectionHeader("  Page Background")) {
            ImGui::Indent(8.0f);

            // Table 1: Normal Mode Palette (Light Paper)
            bool isNormalActive = !isCanvasInverted;
            ImGui::PushStyleColor(ImGuiCol_Text, isNormalActive ? theme.colorText : theme.colorTextMuted);
            ImGui::TextUnformatted("Table 1: Normal Palette (Light Paper)");
            ImGui::PopStyleColor();
            if (!isNormalActive) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.45f, 0.20f, 1.0f));
                ImGui::TextUnformatted("  [Locked: Canvas is inverted; light colors prohibited]");
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();

            struct BgColorDef { const char* label; uint8_t r, g, b; };
            static const BgColorDef s_LightBgPresets[] = {
                { "Pure White",  0xFF, 0xFF, 0xFF },
                { "Warm Ivory",  0xFA, 0xF8, 0xF5 },
                { "Sepia Cream", 0xF4, 0xEC, 0xD8 },
                { "Sky Mist",    0xF0, 0xF4, 0xFF },
                { "Pale Mint",   0xED, 0xF7, 0xF2 },
                { "Soft Rose",   0xFA, 0xF0, 0xF2 },
                { "Light Sand",  0xF6, 0xF0, 0xE6 },
                { "Clean Slate", 0xEC, 0xEF, 0xF3 },
            };

            if (!isNormalActive) ImGui::BeginDisabled(true);
            for (int i = 0; i < 8; ++i) {
                const auto& p = s_LightBgPresets[i];
                ImVec4 col(p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, 1.0f);
                bool isCurrent = isNormalActive && (canvas.canvasBgColor.r() == p.r && canvas.canvasBgColor.g() == p.g && canvas.canvasBgColor.b() == p.b);

                ImGui::PushStyleColor(ImGuiCol_Button, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.92f, col.y * 0.92f, col.z * 0.92f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Border, theme.colorPrimary);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                char btnId[32]; snprintf(btnId, sizeof(btnId), "##lbg_%d", i);
                if (ImGui::Button(btnId, ImVec2(24.0f, 24.0f))) {
                    canvas.canvasBgColor = BLRgba32(p.r, p.g, p.b);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                if (isCurrent) {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor(3);
                if (i % 4 != 3) ImGui::SameLine(0, 8.0f);
            }
            if (!isNormalActive) ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Table 2: Inverted Mode Palette (Dark Paper)
            bool isInvertedActive = isCanvasInverted;
            ImGui::PushStyleColor(ImGuiCol_Text, isInvertedActive ? theme.colorText : theme.colorTextMuted);
            ImGui::TextUnformatted("Table 2: Inverted Palette (Dark Paper)");
            ImGui::PopStyleColor();
            if (!isInvertedActive) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                ImGui::TextUnformatted("  [Inactive: Toggle Invert Canvas Mode to use dark palette]");
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();

            static const BgColorDef s_DarkBgPresets[] = {
                { "Dark Slate",      0x1E, 0x20, 0x26 },
                { "Midnight Blue",   0x14, 0x17, 0x24 },
                { "Obsidian Night",  0x10, 0x11, 0x14 },
                { "Dark Navy",       0x16, 0x20, 0x2E },
                { "Deep Forest",     0x14, 0x22, 0x1A },
                { "Dark Burgundy",   0x26, 0x16, 0x1E },
                { "Dark Espresso",   0x20, 0x1A, 0x16 },
                { "Steel Graphite",  0x24, 0x28, 0x32 },
            };

            if (!isInvertedActive) ImGui::BeginDisabled(true);
            for (int i = 0; i < 8; ++i) {
                const auto& p = s_DarkBgPresets[i];
                ImVec4 col(p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, 1.0f);
                bool isCurrent = isInvertedActive && (canvas.canvasBgColor.r() == p.r && canvas.canvasBgColor.g() == p.g && canvas.canvasBgColor.b() == p.b);

                ImGui::PushStyleColor(ImGuiCol_Button, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 1.2f, col.y * 1.2f, col.z * 1.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Border, theme.colorPrimary);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                char btnId[32]; snprintf(btnId, sizeof(btnId), "##dbg_%d", i);
                if (ImGui::Button(btnId, ImVec2(24.0f, 24.0f))) {
                    canvas.canvasBgColor = BLRgba32(p.r, p.g, p.b);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                if (isCurrent) {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor(3);
                if (i % 4 != 3) ImGui::SameLine(0, 8.0f);
            }
            if (!isInvertedActive) ImGui::EndDisabled();

            ImGui::Unindent(8.0f);
        }

        // ── SECTION 4: Rule Lines & Line Colors (Two Palettes) ─────────
        if (SectionHeader("  Rule Lines & Pattern")) {
            ImGui::Indent(8.0f);

            // Pattern buttons
            auto PaperBtn = [&](const char* label, PaperStyle style) {
                bool isCur = (canvas.currentPaperStyle == style);
                if (isCur) {
                    ImGui::PushStyleColor(ImGuiCol_Button, theme.colorItemSelected);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorItemHover);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, theme.colorItemHover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorItemSelected);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorItemText);
                if (ImGui::Button(label, ImVec2((panelW - 52.0f - 18.0f) * 0.25f, 28.0f))) {
                    canvas.currentPaperStyle = style;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine(0, 6.0f);
            };

            PaperBtn("Empty",  PaperStyle::Blank);
            PaperBtn("Ruled",  PaperStyle::Lined);
            PaperBtn("Grid",   PaperStyle::Grid);
            PaperBtn("Dotted", PaperStyle::Dotted);
            ImGui::NewLine();

            // Grid spacing slider
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
            ImGui::TextUnformatted("Grid / Line Spacing:");
            ImGui::PopStyleColor();
            float spacing = static_cast<float>(canvas.gridSpacingMm);
            ImGui::PushItemWidth(panelW - 52.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.colorItemHover);
            if (ImGui::SliderFloat("##grid_spacing", &spacing, 2.0f, 20.0f, "%.1f mm")) {
                canvas.gridSpacingMm = static_cast<double>(spacing);
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
            ImGui::PopStyleColor();
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Two Tables for Line Colors:
            // Table 1: Normal Line Colors
            bool isNormalLineActive = !isCanvasInverted;
            ImGui::PushStyleColor(ImGuiCol_Text, isNormalLineActive ? theme.colorText : theme.colorTextMuted);
            ImGui::TextUnformatted("Table 1: Normal Line Colors");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            struct LineColorDef { const char* label; uint8_t r, g, b; };
            static const LineColorDef s_LightLinePresets[] = {
                { "Default Slate",  0xEB, 0xEE, 0xF2 },
                { "Subtle Blue",    0xD8, 0xE4, 0xF8 },
                { "Mint Tint",      0xD8, 0xEF, 0xE4 },
                { "Pale Mauve",     0xEA, 0xDE, 0xE4 },
                { "Warm Amber",     0xEF, 0xE8, 0xD6 },
                { "Medium Slate",   0xCB, 0xD2, 0xDC },
            };

            if (!isNormalLineActive) ImGui::BeginDisabled(true);
            for (int i = 0; i < 6; ++i) {
                const auto& p = s_LightLinePresets[i];
                ImVec4 col(p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, 1.0f);
                bool isCurrent = isNormalLineActive && (canvas.gridLineColor.r() == p.r && canvas.gridLineColor.g() == p.g && canvas.gridLineColor.b() == p.b);

                ImGui::PushStyleColor(ImGuiCol_Button, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.9f, col.y * 0.9f, col.z * 0.9f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Border, theme.colorPrimary);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                char btnId[32]; snprintf(btnId, sizeof(btnId), "##lnl_%d", i);
                if (ImGui::Button(btnId, ImVec2(24.0f, 24.0f))) {
                    canvas.gridLineColor = BLRgba32(p.r, p.g, p.b);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                if (isCurrent) {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor(3);
                if (i < 5) ImGui::SameLine(0, 8.0f);
            }
            if (!isNormalLineActive) ImGui::EndDisabled();

            ImGui::Spacing();

            // Table 2: Inverted Line Colors
            bool isInvertedLineActive = isCanvasInverted;
            ImGui::PushStyleColor(ImGuiCol_Text, isInvertedLineActive ? theme.colorText : theme.colorTextMuted);
            ImGui::TextUnformatted("Table 2: Inverted Line Colors (Dark Canvas)");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            static const LineColorDef s_DarkLinePresets[] = {
                { "Muted Dark Slate", 0x34, 0x38, 0x44 },
                { "Luminous Steel",   0x44, 0x4C, 0x5C },
                { "Deep Cyan",        0x2C, 0x3E, 0x4C },
                { "Soft Teal",        0x26, 0x3E, 0x36 },
                { "Muted Violet",     0x3E, 0x2E, 0x3E },
                { "Amber Dusk",       0x3E, 0x37, 0x2A },
            };

            if (!isInvertedLineActive) ImGui::BeginDisabled(true);
            for (int i = 0; i < 6; ++i) {
                const auto& p = s_DarkLinePresets[i];
                ImVec4 col(p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, 1.0f);
                bool isCurrent = isInvertedLineActive && (canvas.gridLineColor.r() == p.r && canvas.gridLineColor.g() == p.g && canvas.gridLineColor.b() == p.b);

                ImGui::PushStyleColor(ImGuiCol_Button, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 1.2f, col.y * 1.2f, col.z * 1.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Border, theme.colorPrimary);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                char btnId[32]; snprintf(btnId, sizeof(btnId), "##lnd_%d", i);
                if (ImGui::Button(btnId, ImVec2(24.0f, 24.0f))) {
                    canvas.gridLineColor = BLRgba32(p.r, p.g, p.b);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                if (isCurrent) {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor(3);
                if (i < 5) ImGui::SameLine(0, 8.0f);
            }
            if (!isInvertedLineActive) ImGui::EndDisabled();

            ImGui::Unindent(8.0f);
        }

        // ── SECTION 5: Page Border ────────────────────────────────────
        if (SectionHeader("  Page Border")) {
            ImGui::Indent(8.0f);

            bool showBorder = canvas.showPageBorder;
            if (ImGui::Checkbox("Show Page Border", &showBorder)) {
                canvas.showPageBorder = showBorder;
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }

            if (canvas.showPageBorder) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                ImGui::TextUnformatted("Border Type Calculation:");
                ImGui::PopStyleColor();

                int bType = (canvas.pageBorderType == PageBorderType::Automatic) ? 0 : 1;
                if (ImGui::RadioButton("Automatic (Fit to Widest Space)", &bType, 0)) {
                    canvas.pageBorderType = PageBorderType::Automatic;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                ImGui::TextWrapped("  Calculates bottom boundary to match selected page ratio.");
                ImGui::PopStyleColor();

                if (ImGui::RadioButton("Fixed (Selected Page Format)", &bType, 1)) {
                    canvas.pageBorderType = PageBorderType::Fixed;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                ImGui::TextWrapped("  Maintains exact 1:1 physical sheet dimensions.");
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                ImGui::TextUnformatted("Border Line Style:");
                ImGui::PopStyleColor();

                int bStyle = static_cast<int>(canvas.pageBorderStyle);
                if (ImGui::RadioButton("Continuous", &bStyle, 0)) {
                    canvas.pageBorderStyle = PageBorderStyle::Continuous;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::SameLine(0, 12.0f);
                if (ImGui::RadioButton("Dashed", &bStyle, 1)) {
                    canvas.pageBorderStyle = PageBorderStyle::Dashed;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::SameLine(0, 12.0f);
                if (ImGui::RadioButton("Corners", &bStyle, 2)) {
                    canvas.pageBorderStyle = PageBorderStyle::Corners;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                ImGui::TextUnformatted("Border Color:");
                ImGui::PopStyleColor();
                ImGui::Spacing();

                struct BorderColDef { const char* label; uint8_t r, g, b; };
                static const BorderColDef s_BorderColors[] = {
                    { "Subtle Slate",    0xD0, 0xD4, 0xDC },
                    { "Accent Blue",     0x4A, 0x90, 0xE2 },
                    { "Folio Orange",    0xE6, 0x5C, 0x14 },
                    { "Medium Charcoal", 0x50, 0x54, 0x60 },
                    { "Emerald Green",   0x2E, 0x7D, 0x32 },
                    { "Ruby Crimson",    0xC2, 0x18, 0x5B }
                };

                for (int i = 0; i < 6; ++i) {
                    const auto& bc = s_BorderColors[i];
                    ImVec4 col(bc.r / 255.0f, bc.g / 255.0f, bc.b / 255.0f, 1.0f);
                    bool isCurrent = (canvas.pageBorderColor.r() == bc.r && canvas.pageBorderColor.g() == bc.g && canvas.pageBorderColor.b() == bc.b);

                    ImGui::PushStyleColor(ImGuiCol_Button, col);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.9f, col.y * 0.9f, col.z * 0.9f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
                    if (isCurrent) {
                        ImGui::PushStyleColor(ImGuiCol_Border, theme.colorPrimary);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                    }
                    char btnId[32]; snprintf(btnId, sizeof(btnId), "##bcol_%d", i);
                    if (ImGui::Button(btnId, ImVec2(24.0f, 24.0f))) {
                        canvas.pageBorderColor = BLRgba32(bc.r, bc.g, bc.b);
                        canvas.isDirty = true;
                        canvas.needsFullRebake = true;
                    }
                    if (isCurrent) {
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopStyleColor(3);
                    if (i < 5) ImGui::SameLine(0, 8.0f);
                }

                // Custom border color edit
                ImGui::Spacing();
                float bColArr[4] = {
                    canvas.pageBorderColor.r() / 255.0f,
                    canvas.pageBorderColor.g() / 255.0f,
                    canvas.pageBorderColor.b() / 255.0f,
                    1.0f
                };
                if (ImGui::ColorEdit4("Custom Border Color", bColArr, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                    canvas.pageBorderColor = BLRgba32(
                        static_cast<uint8_t>(bColArr[0] * 255.0f),
                        static_cast<uint8_t>(bColArr[1] * 255.0f),
                        static_cast<uint8_t>(bColArr[2] * 255.0f)
                    );
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }

                ImGui::Spacing();
                float bWidth = static_cast<float>(canvas.pageBorderWidth);
                ImGui::PushItemWidth(panelW - 52.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.colorItemHover);
                if (ImGui::SliderFloat("##bwidth", &bWidth, 0.5f, 5.0f, "Width: %.1f mm")) {
                    canvas.pageBorderWidth = static_cast<double>(bWidth);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::PopStyleColor();
                ImGui::PopItemWidth();
            }

            ImGui::Unindent(8.0f);
        }

        // ── SECTION 6: Canvas Infinity Modes ───────────────────────────
        if (SectionHeader("  Canvas Mode")) {
            ImGui::Indent(8.0f);

            int modeIdx = 0;
            if (canvas.infinityMode == CanvasInfinityMode::SemiInfinity) modeIdx = 0;
            else if (canvas.infinityMode == CanvasInfinityMode::FullInfinity) modeIdx = 1;
            else if (canvas.infinityMode == CanvasInfinityMode::VerticalScroll) modeIdx = 2;
            else if (canvas.infinityMode == CanvasInfinityMode::HorizontalScroll) modeIdx = 3;

            if (ImGui::RadioButton("Semi Infinity (OneNote-Style)", &modeIdx, 0)) {
                canvas.SetInfinityMode(CanvasInfinityMode::SemiInfinity);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
            ImGui::TextWrapped("  Anchored at origin (0, 0). Expands infinitely downwards & rightwards.");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            if (ImGui::RadioButton("Full Infinity (Unbounded 2D)", &modeIdx, 1)) {
                canvas.SetInfinityMode(CanvasInfinityMode::FullInfinity);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
            ImGui::TextWrapped("  Unbounded 2D canvas in all directions (-inf to +inf).");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            if (ImGui::RadioButton("Vertical Infinite Scroll", &modeIdx, 2)) {
                canvas.SetInfinityMode(CanvasInfinityMode::VerticalScroll);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
            ImGui::TextWrapped("  Fixed sheet width with continuous downward document roll.");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            if (ImGui::RadioButton("Horizontal Infinite Scroll", &modeIdx, 3)) {
                canvas.SetInfinityMode(CanvasInfinityMode::HorizontalScroll);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
            ImGui::TextWrapped("  Fixed sheet height with continuous rightward drafting roll.");
            ImGui::PopStyleColor();

            ImGui::Unindent(8.0f);
        }

        // ── SECTION 7: Page Format & Dimensions ────────────────────────
        if (SectionHeader("  Page Format")) {
            ImGui::Indent(8.0f);

            int pageSize = static_cast<int>(canvas.pageSizeFormat);
            const char* pageSizes[] = {
                "Letter (216 x 279 mm)",
                "A4 (210 x 297 mm)",
                "A3 (297 x 420 mm)",
                "A5 (148 x 210 mm)",
                "Custom..."
            };
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.colorItemHover);
            ImGui::PushItemWidth(panelW - 52.0f);
            if (ImGui::Combo("##page_size", &pageSize, pageSizes, IM_ARRAYSIZE(pageSizes))) {
                canvas.pageSizeFormat = static_cast<PageSizeFormat>(pageSize);
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleColor();

            int orient = canvas.pageIsLandscape ? 1 : 0;
            if (ImGui::RadioButton("Portrait", &orient, 0)) {
                canvas.pageIsLandscape = false;
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
            ImGui::SameLine(0, 16);
            if (ImGui::RadioButton("Landscape", &orient, 1)) {
                canvas.pageIsLandscape = true;
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }

            if (canvas.pageSizeFormat == PageSizeFormat::Custom) {
                ImGui::Spacing();
                float cW = static_cast<float>(canvas.customPageWidthMm);
                float cH = static_cast<float>(canvas.customPageHeightMm);
                ImGui::PushItemWidth((panelW - 52.0f - 8.0f) * 0.5f);
                if (ImGui::DragFloat("W (mm)##custom_w", &cW, 1.0f, 50.0f, 1000.0f, "%.0f mm")) {
                    canvas.customPageWidthMm = static_cast<double>(cW);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::SameLine(0, 8.0f);
                if (ImGui::DragFloat("H (mm)##custom_h", &cH, 1.0f, 50.0f, 1000.0f, "%.0f mm")) {
                    canvas.customPageHeightMm = static_cast<double>(cH);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::PopItemWidth();
            }

            ImGui::Unindent(8.0f);
        }

        // ── BOTTOM ACTION ROW: Set as Default & Reset ─────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static float s_defaultSavedFeedbackTimer = 0.0f;
        if (s_defaultSavedFeedbackTimer > 0.0f) {
            s_defaultSavedFeedbackTimer -= ImGui::GetIO().DeltaTime;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.35f, 1.0f));
            ImGui::TextUnformatted("✓ Settings saved as default template for all new pages!");
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        float btnW = (panelW - 52.0f - 10.0f) * 0.5f;

        // Set as Default button
        ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorPrimary);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("Set as Default", ImVec2(btnW, 32.0f))) {
            canvas.defaultTemplate.paperStyle = canvas.currentPaperStyle;
            canvas.defaultTemplate.gridSpacingMm = canvas.gridSpacingMm;
            if (isCanvasInverted) {
                canvas.defaultTemplate.invertedBgColor = canvas.canvasBgColor;
                canvas.defaultTemplate.invertedLineColor = canvas.gridLineColor;
            } else {
                canvas.defaultTemplate.normalBgColor = canvas.canvasBgColor;
                canvas.defaultTemplate.normalLineColor = canvas.gridLineColor;
            }
            canvas.defaultTemplate.showBorder = canvas.showPageBorder;
            canvas.defaultTemplate.borderColor = canvas.pageBorderColor;
            canvas.defaultTemplate.borderWidth = canvas.pageBorderWidth;
            canvas.defaultTemplate.borderType = canvas.pageBorderType;
            canvas.defaultTemplate.borderStyle = canvas.pageBorderStyle;
            canvas.defaultTemplate.pageSizeFormat = canvas.pageSizeFormat;
            canvas.defaultTemplate.pageIsLandscape = canvas.pageIsLandscape;
            canvas.defaultTemplate.infinityMode = canvas.infinityMode;
            canvas.defaultTemplate.calibrationDpi = canvas.transform.pixelsPerMm * 25.4;
            s_defaultSavedFeedbackTimer = 3.0f;
        }
        ImGui::PopStyleColor(4);

        ImGui::SameLine(0, 10.0f);

        // Reset button
        ImGui::PushStyleColor(ImGuiCol_Button, theme.colorItemHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorItemSelected);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
        if (ImGui::Button("Reset", ImVec2(btnW, 32.0f))) {
            canvas.currentPaperStyle = PaperStyle::Grid;
            canvas.gridSpacingMm = 5.0;
            canvas.canvasBgColor = isCanvasInverted ? BLRgba32(0x1E, 0x20, 0x26) : BLRgba32(0xFF, 0xFF, 0xFF);
            canvas.gridLineColor = isCanvasInverted ? BLRgba32(0x34, 0x38, 0x44) : BLRgba32(0xEB, 0xEE, 0xF2);
            canvas.showPageBorder = false;
            canvas.pageBorderType = PageBorderType::Automatic;
            canvas.pageBorderStyle = PageBorderStyle::Continuous;
            canvas.pageSizeFormat = PageSizeFormat::Letter;
            canvas.pageIsLandscape = false;
            canvas.SetInfinityMode(CanvasInfinityMode::SemiInfinity);
            canvas.transform.SetDPI(96.0f);
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
        }
        ImGui::PopStyleColor(4);

        ImGui::PopStyleColor(); // colorItemText

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    void RenderCollapsedPopup(float width, float topOffset, CanvasEngine& canvas, InputStateMachine& inputSM, const ThemeManager& theme, DocumentSession* session = nullptr) {
        if (session) currentSession = session;
        if (!isCollapsedPopupOpen || displayMode != RibbonDisplayMode::Collapsed) {
            return;
        }

        float popupY = topOffset + animatedHeight + 2.0f;
        float popupH = 96.0f;                 // Full shelf height
        float marginX = 8.0f;
        float popupX = marginX;
        float popupW = width - (2.0f * marginX);
        float popupRounding = 12.0f;

        // 1. Outside input / canvas dismiss check:
        // Any outside input on canvas (drawing, clicking, scrolling) or pressing Escape hides it immediately
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            isCollapsedPopupOpen = false;
            return;
        }

        // Hardware digitizer engagement (stylus pen down, touch gesture) immediately dismisses popup
        if (inputSM.currentStylusState == StylusState::Engaged ||
            inputSM.pen.isDown ||
            inputSM.wasMouseDown ||
            inputSM.wasTouchDown ||
            (inputSM.isCanvasHovered && (io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2] || io.MouseWheel != 0.0f))) {
            isCollapsedPopupOpen = false;
            return;
        }

        // Mouse click or scroll anywhere outside the popup shelf and tabs row dismisses popup immediately
        bool anySubmenuOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        if (!anySubmenuOpen && (io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2] || io.MouseWheel != 0.0f)) {
            ImVec2 mousePos = io.MousePos;
            bool insidePopup = (mousePos.x >= popupX && mousePos.x <= (popupX + popupW) &&
                                mousePos.y >= popupY && mousePos.y <= (popupY + popupH));
            bool insideTabs = (mousePos.x >= 0.0f && mousePos.x <= width &&
                               mousePos.y >= topOffset && mousePos.y < (topOffset + animatedHeight));
            if (!insidePopup && !insideTabs) {
                isCollapsedPopupOpen = false;
                return;
            }
        }

        // 2. Render Floating Overlay Window Directly on Top of Canvas
        // Canvas does not move or resize (contentY remains 58px); popup floats above it.
        // Pure shelf background (theme.colorShelf) with NO orange background!
        // Rounded corners on all edges with smooth border and multi-layer soft drop shadow.
        ImGui::SetNextWindowPos(ImVec2(popupX, popupY));
        ImGui::SetNextWindowSize(ImVec2(popupW, popupH));
        ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, popupRounding);
        ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.colorShelf);

        if (ImGui::Begin("##CollapsedRibbonPopup", &isCollapsedPopupOpen, popupFlags)) {
            RenderShelfContents(popupW, popupH, false /* isMini = false */, canvas, inputSM, theme);

            // Floating Aesthetics: Multi-layer soft drop shadow with matching corner rounding
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 pMin(winPos.x, winPos.y);
            ImVec2 pMax(winPos.x + popupW, winPos.y + popupH);

            ImU32 shadowCol1 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.12f));
            ImU32 shadowCol2 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.06f));
            ImU32 shadowCol3 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.02f));

            drawList->AddRect(ImVec2(pMin.x - 1.0f, pMin.y - 1.0f), ImVec2(pMax.x + 1.0f, pMax.y + 2.0f), shadowCol1, popupRounding, 0, 1.5f);
            drawList->AddRect(ImVec2(pMin.x - 2.0f, pMin.y - 1.0f), ImVec2(pMax.x + 2.0f, pMax.y + 4.0f), shadowCol2, popupRounding, 0, 3.0f);
            drawList->AddRect(ImVec2(pMin.x - 4.0f, pMin.y - 1.0f), ImVec2(pMax.x + 4.0f, pMax.y + 7.0f), shadowCol3, popupRounding, 0, 5.0f);
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
};