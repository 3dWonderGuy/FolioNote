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
#include "ui/views/pdf_viewer_page.hpp"
enum class RibbonTab { Home, Insert, Draw, History, Review, View, Help, ShapeFormat, PdfTools };

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

        // Check for contextual selected shape and handle auto-tab navigation
        auto selectedShape = canvas.GetSelectedShape(session ? session : currentSession);
        static uint32_t s_lastSelectedShapeUid = 0;
        uint32_t currentShapeUid = selectedShape ? selectedShape->uid : 0;

        if (selectedShape) {
            if (s_lastSelectedShapeUid != currentShapeUid && activeTab != RibbonTab::ShapeFormat) {
                previousTab = activeTab;
                activeTab = RibbonTab::ShapeFormat;
                tabTransitionTimer = 0.0f;
            }
        } else {
            if (activeTab == RibbonTab::ShapeFormat) {
                activeTab = (previousTab != RibbonTab::ShapeFormat) ? previousTab : RibbonTab::Draw;
                tabTransitionTimer = 0.0f;
            }
        }
        s_lastSelectedShapeUid = currentShapeUid;

        struct TabDef {
            const char* name;
            bool isFile;
            RibbonTab tabEnum;
            bool isContextual = false;
        };

        std::vector<TabDef> tabs = {
            { "File",    true,  RibbonTab::Home, false },
            { "Home",    false, RibbonTab::Home, false },
            { "Insert",  false, RibbonTab::Insert, false },
            { "Draw",    false, RibbonTab::Draw, false },
            { "View",    false, RibbonTab::View, false }
        };

        auto currentDocSession = session ? session : currentSession;
        auto activePg = currentDocSession ? currentDocSession->GetActivePage() : nullptr;
        bool isDedicatedPdf = activePg && activePg->isDedicatedPdf;

        if (selectedShape != nullptr || activeTab == RibbonTab::ShapeFormat) {
            tabs.push_back({ "Shape Format", false, RibbonTab::ShapeFormat, true });
        }
        if (isDedicatedPdf || activeTab == RibbonTab::PdfTools) {
            tabs.push_back({ "PDF Tools", false, RibbonTab::PdfTools, true });
        }
        if (!isDedicatedPdf && activeTab == RibbonTab::PdfTools) {
            activeTab = (previousTab != RibbonTab::PdfTools) ? previousTab : RibbonTab::Draw;
        }
        const int numTabs = static_cast<int>(tabs.size());

        std::vector<float> tabWidths(numTabs);
        std::vector<float> boldTextWidths(numTabs);
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
                ? (tab.isContextual ? ImGui::ColorConvertFloat4ToU32(theme.colorPrimary) : ImGui::ColorConvertFloat4ToU32(theme.colorHeaderText))
                : (tab.isContextual ? ImGui::ColorConvertFloat4ToU32(theme.colorPrimary) : ImGui::ColorConvertFloat4ToU32(isHovered ? theme.colorTabHoverText : theme.colorHeaderTextMuted));

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
                    sec.AddLargeButton("btn_insert_pdf", iconPdf, "Import PDF", "Import a PDF document into notebook sections or dedicated viewer", false,
                        [&]() {
                            canvas.OpenPdfFileDialog(nullptr, currentSession);
                        },
                        false, ImVec2(64.0f, 58.0f)
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

                    // 4. Picture (From Files/Pictures or Clipboard)
                    static char s_pictureUrl[256] = "";
                    sec.AddSplitButton("btn_insert_pic", iconPicture, "Picture", "Insert an image from files or pictures", false,
                        [&]() {
                            canvas.OpenImageFileDialog(canvas.sdlWindow, currentSession);
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddItem("From Files / Pictures...", iconPicture, nullptr, [&]() {
                                canvas.OpenImageFileDialog(canvas.sdlWindow, currentSession);
                            });
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
                            menu.AddItem("Paste from Clipboard", 0, "Ctrl+V", [&]() {
                                canvas.InsertImageFromClipboard(currentSession);
                            });
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
                // SECTION 3.5: Shapes & Drawings
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_insert_shapes_tab", "Shapes", theme, isMini);
                    sec.AddSplitButton("btn_insert_shapes_tab", 0, "Shapes", "Insert geometric vector shape", false,
                        [&]() {
                            canvas.StartShapeCreation(Folio::ShapeType::Rectangle, canvas.shapeCreation.lockDrawingMode);
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::DrawingShape);
                            inputSM.currentAction = InteractionState::DrawingShape;
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Insert 2D Vector Shape");
                            menu.AddCustom([&]() {
                                struct ShapeItem {
                                    Folio::ShapeType type;
                                    const char* name;
                                };
                                static const ShapeItem s_items[] = {
                                    { Folio::ShapeType::Rectangle, "Rectangle" },
                                    { Folio::ShapeType::RoundedRectangle, "Rounded Rect" },
                                    { Folio::ShapeType::Ellipse, "Ellipse / Circle" },
                                    { Folio::ShapeType::Triangle, "Triangle" },
                                    { Folio::ShapeType::Diamond, "Diamond" },
                                    { Folio::ShapeType::Star, "Star (5-Point)" },
                                    { Folio::ShapeType::Arrow, "Arrow" },
                                    { Folio::ShapeType::DoubleArrow, "Double Arrow" },
                                    { Folio::ShapeType::Hexagon, "Hexagon" },
                                    { Folio::ShapeType::Line, "Line" }
                                };

                                for (const auto& item : s_items) {
                                    ImGui::PushID(static_cast<int>(item.type) + 500);
                                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                                    float rowHeight = 32.0f;
                                    float menuW = 210.0f;
                                    if (ImGui::Selectable("##shape_select_tab", false, 0, ImVec2(menuW, rowHeight))) {
                                        canvas.StartShapeCreation(item.type, canvas.shapeCreation.lockDrawingMode);
                                        inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::DrawingShape);
                                        inputSM.currentAction = InteractionState::DrawingShape;
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImDrawList* dl = ImGui::GetWindowDrawList();
                                    ImVec2 iconMin(p0.x + 8.0f, p0.y + 5.0f);
                                    ImVec2 iconMax(p0.x + 30.0f, p0.y + 27.0f);
                                    Folio::ShapeObject::DrawShapeIconImGui(dl, item.type, iconMin, iconMax,
                                        ImGui::GetColorU32(theme.colorText),
                                        ImGui::GetColorU32(ImVec4(0.0f, 0.47f, 0.83f, 0.25f)));

                                    dl->AddText(ImVec2(p0.x + 38.0f, p0.y + 7.0f), ImGui::GetColorU32(theme.colorText), item.name);
                                    ImGui::PopID();
                                }
                            });
                        }, false, ImVec2(64.0f, 58.0f)
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
                // -------------------------------------------------------------
                // SECTION 1: Undo / History (Horizontal Side-by-Side: Left to Right)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_history")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_history", "History", theme, isMini);
                    sec.AddLargeButton("undo_draw", redoIcon, "Undo", "Undo last stroke (Ctrl+Z)", false,
                        [&]() { /* Undo action */ }, false /* flipH = false: points LEFT */, ImVec2(46.0f, 58.0f));
                    sec.AddLargeButton("redo_draw", redoIcon, "Redo", "Redo stroke (Ctrl+Y)", false,
                        [&]() { /* Redo action */ }, true /* flipH = true: points RIGHT */, ImVec2(46.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 2: Selection + Navigation (Delete / Select / Lasso / Pan)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_selection")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_selection", "Selection", theme, isMini);
                    bool isSelecting = (inputSM.currentAction == InteractionState::Selecting);
                    bool isBoxSelect = (isSelecting && canvas.selectionMode == CanvasEngine::SelectionMode::Box);
                    bool isLasso     = (isSelecting && canvas.selectionMode == CanvasEngine::SelectionMode::Lasso);
                    bool isPanning   = (inputSM.currentAction == InteractionState::Panning);

                    // Square Delete button right before Select
                    sec.AddLargeButton("delete_draw", deleteIcon, "Delete", "Delete Selection: Delete selected strokes or objects (Del)", false,
                        [&]() {
                            if (currentSession) {
                                canvas.DeleteSelectedObjects(currentSession);
                            }
                        }, false, ImVec2(48.0f, 58.0f));

                    sec.AddLargeButton("select_draw", selectIcon, "Select", "Box Marquee Selection & Direct Click Object Select (Default)", isBoxSelect,
                        [&]() {
                            canvas.selectionMode = CanvasEngine::SelectionMode::Box;
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Selecting);
                            inputSM.currentAction = InteractionState::Selecting;
                        }, false, ImVec2(48.0f, 58.0f));

                    sec.AddLargeButton("lasso_draw", iconLasso, "Lasso", "Freehand Lasso Selection", isLasso,
                        [&]() {
                            canvas.selectionMode = CanvasEngine::SelectionMode::Lasso;
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Selecting);
                            inputSM.currentAction = InteractionState::Selecting;
                        }, false, ImVec2(48.0f, 58.0f));

                    // Pan sits here alongside the other navigation tools.
                    // For touch this is the default mode (finger gestures pan/zoom).
                    // For stylus/mouse it can also be used as an explicit pan tool.
                    sec.AddLargeButton("pan_draw", 0, "Pan",
                        "Pan: Navigate the canvas by dragging. Default touch mode.",
                        isPanning,
                        [&]() {
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Panning);
                            inputSM.currentAction = InteractionState::Panning;
                            // If the active device is touch, sync the drawWithTouch toggle off
                            if (inputSM.ActiveDevice == DeviceType::Touch) {
                                drawWithTouch = false;
                                SettingsManager::Instance().drawWithTouch = false;
                                SettingsManager::Instance().Save();
                            }
                        }, false, ImVec2(48.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 3: Drawing Tools (Eraser Split + Pen Presets + Add Tool)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_tools")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_tools", "Drawing Tools", theme, isMini);

                    // 1. Eraser Split Dropdown
                    bool isEraser = (inputSM.currentAction == InteractionState::Eraser);
                    const char* eraserLabel = isStrokeEraser ? "Stroke Eraser" : "Point Eraser";
                    const char* eraserTooltip = isStrokeEraser ? "Vector Stroke Eraser (click to erase whole stroke)" : "Simple Eraser (erase by radius)";

                    sec.AddSplitButton("eraser", iconEraser, eraserLabel, eraserTooltip, isEraser,
                        [&]() { 
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Eraser);
                            inputSM.currentAction = InteractionState::Eraser;
                            inputSM.isStrokeEraser = isStrokeEraser;
                            inputSM.eraserRadiusMm = eraserSizeMm * 0.5f;
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Eraser Mode");
                            menu.AddItem("Stroke Eraser", iconEraser, "", [&]() {
                                isStrokeEraser = true;
                                inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Eraser);
                                inputSM.currentAction = InteractionState::Eraser;
                                inputSM.isStrokeEraser = true;
                                SettingsManager::Instance().isStrokeEraser = true;
                                SettingsManager::Instance().Save();
                            }, isStrokeEraser);
                            menu.AddItem("Simple Eraser (Point)", 0, "", [&]() {
                                isStrokeEraser = false;
                                inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Eraser);
                                inputSM.currentAction = InteractionState::Eraser;
                                inputSM.isStrokeEraser = false;
                                inputSM.eraserRadiusMm = eraserSizeMm * 0.5f;
                                SettingsManager::Instance().isStrokeEraser = false;
                                SettingsManager::Instance().Save();
                            }, !isStrokeEraser);

                            if (!isStrokeEraser) {
                                menu.AddSeparator();
                                menu.AddHeader("Eraser Size");
                                menu.AddItem("Small (2.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 2.0f;
                                    inputSM.eraserRadiusMm = 1.0f;
                                    SettingsManager::Instance().eraserSizeMm = 2.0f;
                                    SettingsManager::Instance().Save();
                                }, std::abs(eraserSizeMm - 2.0f) < 0.5f);
                                menu.AddItem("Medium (6.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 6.0f;
                                    inputSM.eraserRadiusMm = 3.0f;
                                    SettingsManager::Instance().eraserSizeMm = 6.0f;
                                    SettingsManager::Instance().Save();
                                }, std::abs(eraserSizeMm - 6.0f) < 0.5f);
                                menu.AddItem("Large (12.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 12.0f;
                                    inputSM.eraserRadiusMm = 6.0f;
                                    SettingsManager::Instance().eraserSizeMm = 12.0f;
                                    SettingsManager::Instance().Save();
                                }, std::abs(eraserSizeMm - 12.0f) < 0.5f);
                                menu.AddItem("Extra Large (20.0 mm)", 0, "", [&]() {
                                    eraserSizeMm = 20.0f;
                                    inputSM.eraserRadiusMm = 10.0f;
                                    SettingsManager::Instance().eraserSizeMm = 20.0f;
                                    SettingsManager::Instance().Save();
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
                                    inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
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
                                inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
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
                                    inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Fountain Pen", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Fountain);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Pencil", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Pencil);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Brush", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Brush);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Highlighter", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::Highlighter);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
                                    inputSM.currentAction = InteractionState::Inking;
                                }
                            });
                            menu.AddItem("Add Laser Pointer", 0, "", [&]() {
                                std::string newId = presetManager.AddPreset(PenType::LaserPointer);
                                auto* p = presetManager.FindPreset(newId);
                                if (p) {
                                    presetManager.ApplyPreset(*p, activePen);
                                    inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::Inking);
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

                            // 3. Line Style Selector Button (Right of sliders)
                            float styleBtnX = sliderX + sliderW + 8.0f;
                            float styleBtnW = isMini ? 36.0f : 50.0f;
                            float styleBtnH = isMini ? 28.0f : 52.0f;
                            float styleBtnY = startPos.y + (isMini ? 2.0f : (58.0f - styleBtnH) * 0.5f);
                            ImVec2 styleBtnPos(styleBtnX, styleBtnY);

                            ImGui::SetCursorScreenPos(styleBtnPos);
                            std::string lineStylePopupId = "##line_style_picker_popup";
                            bool styleBtnClicked = ImGui::InvisibleButton("##line_style_btn", ImVec2(styleBtnW, styleBtnH));
                            bool styleBtnHovered = ImGui::IsItemHovered();
                            bool styleBtnPressed = ImGui::IsItemActive();

                            // Button background pill
                            ImVec2 sbMin = styleBtnPos;
                            ImVec2 sbMax(styleBtnPos.x + styleBtnW, styleBtnPos.y + styleBtnH);
                            float sbRound = 6.0f;

                            if (styleBtnPressed) {
                                drawList->AddRectFilled(sbMin, sbMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected), sbRound);
                                drawList->AddRect(sbMin, sbMax, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), sbRound, 0, 1.5f);
                            } else if (styleBtnHovered) {
                                drawList->AddRectFilled(sbMin, sbMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), sbRound);
                                drawList->AddRect(sbMin, sbMax, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), sbRound, 0, 1.0f);
                            } else {
                                drawList->AddRectFilled(sbMin, sbMax, ImGui::ColorConvertFloat4ToU32(theme.colorPanel), sbRound);
                                drawList->AddRect(sbMin, sbMax, ImGui::ColorConvertFloat4ToU32(theme.colorBorder), sbRound, 0, 1.0f);
                            }

                            // Graphic preview of current line style
                            ImU32 inkU32 = ImGui::ColorConvertFloat4ToU32(activePreset->color);
                            float previewY = isMini ? (styleBtnPos.y + styleBtnH * 0.5f) : (styleBtnPos.y + 16.0f);
                            float px1 = styleBtnPos.x + 8.0f;
                            float px2 = styleBtnPos.x + styleBtnW - 8.0f;
                            float pThick = std::clamp(activePreset->thicknessMm * 1.5f, 2.0f, 4.0f);

                            if (activePreset->strokePattern == StrokePattern::Solid) {
                                drawList->AddLine(ImVec2(px1, previewY), ImVec2(px2, previewY), inkU32, pThick);
                            } else if (activePreset->strokePattern == StrokePattern::Dashed) {
                                float seg = 10.0f, gap = 5.0f;
                                drawList->AddLine(ImVec2(px1, previewY), ImVec2(px1 + seg, previewY), inkU32, pThick);
                                drawList->AddLine(ImVec2(px1 + seg + gap, previewY), ImVec2(px2, previewY), inkU32, pThick);
                            } else if (activePreset->strokePattern == StrokePattern::Dotted) {
                                float dotR = std::clamp(pThick * 0.55f, 1.6f, 2.8f);
                                float span = px2 - px1;
                                for (int d = 0; d <= 3; d++) {
                                    drawList->AddCircleFilled(ImVec2(px1 + d * (span / 3.0f), previewY), dotR, inkU32);
                                }
                            } else {
                                // Textured
                                float dotR = 1.6f;
                                float span = px2 - px1;
                                for (int d = 0; d <= 5; d++) {
                                    float jy = ((d % 2 == 0) ? -0.8f : 0.8f);
                                    drawList->AddCircleFilled(ImVec2(px1 + d * (span / 5.0f), previewY + jy), dotR, inkU32);
                                }
                            }

                            // Label text below preview (in full mode)
                            if (!isMini) {
                                const char* sLabel = "Solid";
                                if (activePreset->strokePattern == StrokePattern::Solid) sLabel = "Solid";
                                else if (activePreset->strokePattern == StrokePattern::Dashed) sLabel = "Dashed";
                                else if (activePreset->strokePattern == StrokePattern::Dotted) sLabel = "Dotted";
                                else if (activePreset->strokePattern == StrokePattern::TexturedPencil) sLabel = "Texture";

                                ImGui::PushFont(FolioTheme::FontRibbonSection ? FolioTheme::FontRibbonSection : FolioTheme::FontRegular);
                                ImVec2 txtSz = ImGui::CalcTextSize(sLabel);
                                float txtX = styleBtnPos.x + (styleBtnW - txtSz.x) * 0.5f;
                                float txtY = styleBtnPos.y + styleBtnH - txtSz.y - 4.0f;
                                drawList->AddText(ImVec2(txtX, txtY), ImGui::ColorConvertFloat4ToU32(theme.colorText), sLabel);
                                ImGui::PopFont();
                            }

                            if (styleBtnHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                                ImGui::BeginTooltip();
                                const char* patDesc = (activePreset->strokePattern == StrokePattern::Dotted) ? "Dotted Line" :
                                                      (activePreset->strokePattern == StrokePattern::Dashed) ? "Dashed Line" :
                                                      (activePreset->strokePattern == StrokePattern::TexturedPencil) ? "Textured Line" : "Continuous (Solid) Line";
                                ImGui::Text("Line Style: %s", patDesc);
                                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                                ImGui::TextUnformatted("Click to choose Continuous, Dashed, or Dotted style");
                                ImGui::PopStyleColor();
                                ImGui::EndTooltip();
                            }

                            if (styleBtnClicked) {
                                ImGui::OpenPopup(lineStylePopupId.c_str());
                            }

                            // Popup Menu for Line Style Selection
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
                            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
                            ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.colorPanel);
                            ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

                            if (ImGui::BeginPopup(lineStylePopupId.c_str())) {
                                ImGui::TextUnformatted("Select Line Style");
                                ImGui::Separator();
                                ImGui::Spacing();

                                struct StyleItem {
                                    const char* name;
                                    StrokePattern pattern;
                                    const char* desc;
                                };
                                static const StyleItem s_Items[] = {
                                    { "Continuous", StrokePattern::Solid,          "Solid unbroken stroke" },
                                    { "Dashed",     StrokePattern::Dashed,         "Evenly spaced line dashes" },
                                    { "Dotted",     StrokePattern::Dotted,         "Crisp round dots trace" },
                                    { "Textured",   StrokePattern::TexturedPencil, "Pencil textured grain" }
                                };

                                bool styleChanged = false;
                                for (int i = 0; i < 4; i++) {
                                    bool isSelected = (activePreset->strokePattern == s_Items[i].pattern);
                                    ImGui::PushID(i);

                                    ImVec2 itemPos = ImGui::GetCursorScreenPos();
                                    float itemW = 190.0f;
                                    float itemH = 34.0f;
                                    bool itemClicked = ImGui::InvisibleButton("##style_row", ImVec2(itemW, itemH));
                                    bool itemHovered = ImGui::IsItemHovered();

                                    ImDrawList* popDl = ImGui::GetWindowDrawList();
                                    if (isSelected) {
                                        popDl->AddRectFilled(itemPos, ImVec2(itemPos.x + itemW, itemPos.y + itemH),
                                            ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected), 6.0f);
                                        popDl->AddRect(itemPos, ImVec2(itemPos.x + itemW, itemPos.y + itemH),
                                            ImGui::ColorConvertFloat4ToU32(theme.colorBorder), 6.0f, 0, 1.2f);
                                    } else if (itemHovered) {
                                        popDl->AddRectFilled(itemPos, ImVec2(itemPos.x + itemW, itemPos.y + itemH),
                                            ImGui::ColorConvertFloat4ToU32(theme.colorItemHover), 6.0f);
                                    }

                                    // Graphical sample line on the left
                                    float sampleY = itemPos.y + itemH * 0.5f;
                                    float sx1 = itemPos.x + 10.0f;
                                    float sx2 = itemPos.x + 50.0f;
                                    ImU32 sCol = isSelected ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                                            : ImGui::ColorConvertFloat4ToU32(activePreset->color);

                                    if (s_Items[i].pattern == StrokePattern::Solid) {
                                        popDl->AddLine(ImVec2(sx1, sampleY), ImVec2(sx2, sampleY), sCol, 3.0f);
                                    } else if (s_Items[i].pattern == StrokePattern::Dashed) {
                                        float dW = 10.0f, gW = 5.0f;
                                        popDl->AddLine(ImVec2(sx1, sampleY), ImVec2(sx1 + dW, sampleY), sCol, 3.0f);
                                        popDl->AddLine(ImVec2(sx1 + dW + gW, sampleY), ImVec2(sx2, sampleY), sCol, 3.0f);
                                    } else if (s_Items[i].pattern == StrokePattern::Dotted) {
                                        for (int d = 0; d < 4; d++) {
                                            popDl->AddCircleFilled(ImVec2(sx1 + d * 13.0f, sampleY), 2.2f, sCol);
                                        }
                                    } else {
                                        for (int d = 0; d < 6; d++) {
                                            float jy = ((d % 2 == 0) ? -1.0f : 1.0f);
                                            popDl->AddCircleFilled(ImVec2(sx1 + d * 8.0f, sampleY + jy), 1.8f, sCol);
                                        }
                                    }

                                    // Label text
                                    ImU32 textCol = isSelected ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                                               : ImGui::ColorConvertFloat4ToU32(theme.colorText);
                                    popDl->AddText(ImVec2(itemPos.x + 60.0f, itemPos.y + 4.0f), textCol, s_Items[i].name);

                                    ImU32 descCol = isSelected ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelectedText)
                                                               : ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted);
                                    popDl->AddText(ImVec2(itemPos.x + 60.0f, itemPos.y + 18.0f), descCol, s_Items[i].desc);

                                    if (itemClicked) {
                                        activePreset->strokePattern = s_Items[i].pattern;
                                        styleChanged = true;
                                        ImGui::CloseCurrentPopup();
                                    }

                                    ImGui::PopID();
                                }

                                if (styleChanged) {
                                    presetManager.ApplyPreset(*activePreset, activePen);
                                }

                                ImGui::EndPopup();
                            }
                            ImGui::PopStyleColor(2);
                            ImGui::PopStyleVar(2);

                            // Advance cursor so section sizing accurately wraps these controls
                            ImGui::SetCursorScreenPos(ImVec2(styleBtnX + styleBtnW, startPos.y));
                        }, 6.0f);
                    }

                    sec.Render();
                } // End sec_tools

                // -------------------------------------------------------------
                // SECTION 4: Input Mode (Touch Inking toggle)
                // -------------------------------------------------------------
                // "Inking" button toggles whether a single finger draws or navigates.
                // A custom widget draws a modern green-filled circle indicator badge
                // in the button's icon area so the user can tell at a glance whether
                // touch-inking is on, separate from the button highlight tint.
                //
                // When activated, the last active pen preset is also restored so the
                // pen carousel immediately highlights the tool that will be used.
                //
                // NOTE: Pan lives in the Selection section alongside Select/Lasso.
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_input")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_input", "Input", theme, isMini);

                    // Reflects the ACTIVE device's tool — not just touch.
                    // Green whenever the current device (mouse/stylus/touch) is inking.
                    bool activeDeviceIsInking = (inputSM.GetActiveDeviceTool() == InteractionState::Inking);

                    // Custom widget: Inking toggle button with green circle badge
                    sec.AddWidget([&]() {
                        ImDrawList* dl   = ImGui::GetWindowDrawList();
                        ImVec2 startPos  = ImGui::GetCursorScreenPos();
                        const float btnW = 62.0f;
                        const float btnH = isMini ? 30.0f : 58.0f;

                        // Invisible interaction zone
                        bool clicked = ImGui::InvisibleButton("##inking_touch_btn", ImVec2(btnW, btnH));
                        bool hovered = ImGui::IsItemHovered();
                        bool pressed = ImGui::IsItemActive();

                        // Background fill
                        ImU32 bgFill = activeDeviceIsInking
                            ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected)
                            : (pressed  ? ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected)
                            : (hovered  ? ImGui::ColorConvertFloat4ToU32(theme.colorItemHover)
                                        : IM_COL32(0, 0, 0, 0)));
                        if (bgFill != IM_COL32(0, 0, 0, 0)) {
                            dl->AddRectFilled(startPos,
                                ImVec2(startPos.x + btnW, startPos.y + btnH),
                                bgFill, 6.0f);
                        }

                        // Circle indicator — green filled when active, outlined when inactive
                        ImVec2 circleCenter(startPos.x + btnW * 0.5f,
                                            startPos.y + (isMini ? btnH * 0.5f : btnH * 0.38f));
                        const float circleR = isMini ? 6.0f : 10.0f;

                        if (activeDeviceIsInking) {
                            // Glowing green filled circle
                            dl->AddCircleFilled(circleCenter, circleR + 2.5f,
                                IM_COL32(50, 210, 110, 55));  // soft outer glow
                            dl->AddCircleFilled(circleCenter, circleR,
                                IM_COL32(55, 210, 115, 255)); // solid green fill
                            dl->AddCircle(circleCenter, circleR,
                                IM_COL32(180, 255, 200, 120), 0, 1.2f); // rim highlight
                        } else {
                            // Muted outlined circle
                            dl->AddCircle(circleCenter, circleR,
                                ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted), 0, 1.5f);
                        }

                        // Label "Inking" centred at bottom
                        if (!isMini) {
                            const char* label = "Inking";
                            float textW = ImGui::CalcTextSize(label).x;
                            ImVec2 textPos(startPos.x + (btnW - textW) * 0.5f,
                                           startPos.y + btnH - 16.0f);
                            dl->AddText(textPos,
                                ImGui::ColorConvertFloat4ToU32(theme.colorText), label);
                        }

                        // Tooltip
                        if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                            ImGui::BeginTooltip();
                            if (activeDeviceIsInking) {
                                ImGui::TextUnformatted("Inking: Active. Click to switch to Pan/Navigate mode.");
                            } else {
                                ImGui::TextUnformatted("Inking: Click to switch to drawing mode with the active pen.");
                            }
                            ImGui::EndTooltip();
                        }

                        // Toggle on click — targets the ACTIVE device, not hard-coded to Touch
                        if (clicked) {
                            InteractionState next = activeDeviceIsInking
                                ? InteractionState::Panning
                                : InteractionState::Inking;
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, next);

                            // Sync the drawWithTouch convenience flag only when touch is active
                            if (inputSM.ActiveDevice == DeviceType::Touch) {
                                drawWithTouch = (next == InteractionState::Inking);
                                SettingsManager::Instance().drawWithTouch = drawWithTouch;
                                SettingsManager::Instance().Save();
                            }

                            // Restore the last active pen preset so the pen carousel
                            // immediately highlights the tool that will be drawn with.
                            if (next == InteractionState::Inking) {
                                auto* p = presetManager.GetActivePreset();
                                if (p) presetManager.ApplyPreset(*p, activePen);
                            }
                        }

                        // Advance the section cursor past the custom widget
                        ImGui::SetCursorScreenPos(ImVec2(startPos.x + btnW, startPos.y));
                    }, 6.0f);

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 5: Stencils (Ruler)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_stencils")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_stencils", "Stencils", theme, isMini);
                    sec.AddLargeButton("stencil_ruler", 0, "Ruler", "Ruler: Toggle digital straightedge ruler overlay", rulerEnabled,
                        [&]() {
                            rulerEnabled = !rulerEnabled;
                            SettingsManager::Instance().rulerEnabled = rulerEnabled;
                            SettingsManager::Instance().Save();
                        }, false, ImVec2(48.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 6: Edit (Insert Space)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_edit")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_edit", "Edit", theme, isMini);

                    sec.AddLargeButton("insert_space", 0, "Insert Space", "Insert Space: Insert vertical space between notes", isInsertSpaceActive,
                        [&]() { isInsertSpaceActive = !isInsertSpaceActive; }, false, ImVec2(74.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 7: Shapes (Shapes Split Dropdown + Automatic Shapes Toggle)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_shapes")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_shapes", "Shapes", theme, isMini);

                    sec.AddSplitButton("shapes_picker", 0, "Shapes", "Insert geometric vector shape", false,
                        [&]() {
                            canvas.StartShapeCreation(Folio::ShapeType::Rectangle, canvas.shapeCreation.lockDrawingMode);
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::DrawingShape);
                            inputSM.currentAction = InteractionState::DrawingShape;
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Insert 2D Vector Shape");
                            menu.AddCustom([&]() {
                                struct ShapeItem {
                                    Folio::ShapeType type;
                                    const char* name;
                                };
                                static const ShapeItem s_items[] = {
                                    { Folio::ShapeType::Rectangle, "Rectangle" },
                                    { Folio::ShapeType::RoundedRectangle, "Rounded Rect" },
                                    { Folio::ShapeType::Ellipse, "Ellipse / Circle" },
                                    { Folio::ShapeType::Triangle, "Triangle" },
                                    { Folio::ShapeType::Diamond, "Diamond" },
                                    { Folio::ShapeType::Star, "Star (5-Point)" },
                                    { Folio::ShapeType::Arrow, "Arrow" },
                                    { Folio::ShapeType::DoubleArrow, "Double Arrow" },
                                    { Folio::ShapeType::Hexagon, "Hexagon" },
                                    { Folio::ShapeType::Line, "Line" }
                                };

                                for (const auto& item : s_items) {
                                    ImGui::PushID(static_cast<int>(item.type));
                                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                                    float rowHeight = 32.0f;
                                    float menuW = 210.0f;
                                    if (ImGui::Selectable("##shape_select", false, 0, ImVec2(menuW, rowHeight))) {
                                        canvas.StartShapeCreation(item.type, canvas.shapeCreation.lockDrawingMode);
                                        inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::DrawingShape);
                                        inputSM.currentAction = InteractionState::DrawingShape;
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImDrawList* dl = ImGui::GetWindowDrawList();
                                    ImVec2 iconMin(p0.x + 8.0f, p0.y + 5.0f);
                                    ImVec2 iconMax(p0.x + 30.0f, p0.y + 27.0f);
                                    Folio::ShapeObject::DrawShapeIconImGui(dl, item.type, iconMin, iconMax,
                                        ImGui::GetColorU32(theme.colorText),
                                        ImGui::GetColorU32(ImVec4(0.0f, 0.47f, 0.83f, 0.25f)));

                                    dl->AddText(ImVec2(p0.x + 38.0f, p0.y + 7.0f), ImGui::GetColorU32(theme.colorText), item.name);
                                    ImGui::PopID();
                                }
                            });
                        }, false, ImVec2(64.0f, 58.0f)
                    );

                    sec.AddLargeButton("auto_shapes", 0, "Auto", "Automatic Shapes: Snaps freehand geometric sketches into clean vector shapes", autoShapesEnabled,
                        [&]() {
                            autoShapesEnabled = !autoShapesEnabled;
                            SettingsManager::Instance().autoShapesEnabled = autoShapesEnabled;
                            SettingsManager::Instance().Save();
                        }, false, ImVec2(48.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 8: Math (Ink to Math)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_math")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_math", "Math", theme, isMini);
                    sec.AddLargeButton("ink_to_math", 0, "Math", "Ink to Math: Convert handwritten mathematical expressions to LaTeX / MathML", false,
                        [&]() {}, false, ImVec2(48.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 9: Mode (Full Page View)
                // -------------------------------------------------------------
                if (SettingsManager::Instance().IsSectionVisible("sec_mode")) {
                    FolioUI::ToolbarSectionBuilder sec("grp_draw_mode", "Mode", theme, isMini);
                    bool isFullPage = (displayMode == RibbonDisplayMode::FullyHidden);
                    sec.AddLargeButton("full_page_view", 0, "Full Page", "Full Page View: Toggle distraction-free canvas mode", isFullPage,
                        [&]() {
                            SetDisplayMode(displayMode == RibbonDisplayMode::FullyHidden ? RibbonDisplayMode::FullRibbon : RibbonDisplayMode::FullyHidden);
                        }, false, ImVec2(64.0f, 58.0f));
                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 10: Custom User Sections from Ribbon Editor
                // -------------------------------------------------------------
                for (const auto& secDef : SettingsManager::Instance().ribbonSections) {
                    if (secDef.id.rfind("sec_custom_", 0) == 0 && secDef.isVisible) {
                        FolioUI::ToolbarSectionBuilder sec(secDef.id.c_str(), secDef.title.c_str(), theme, isMini);
                        for (size_t bIdx = 0; bIdx < secDef.buttons.size(); ++bIdx) {
                            const auto& btnName = secDef.buttons[bIdx];
                            std::string btnId = secDef.id + "_btn_" + std::to_string(bIdx);
                            sec.AddLargeButton(btnId.c_str(), 0, btnName.c_str(), btnName.c_str(), false,
                                []() {}, false, ImVec2(52.0f, 58.0f));
                        }
                        sec.Render();
                    }
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
            else if (activeTab == RibbonTab::ShapeFormat) {
                auto selectedShape = canvas.GetSelectedShape(currentSession);

                // -------------------------------------------------------------
                // SECTION 1: Mode & Shape Picker
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_shape_mode", "Drawing Mode", theme, isMini);

                    // 1. Lock Drawing Mode Toggle
                    bool isLocked = canvas.shapeCreation.lockDrawingMode;
                    sec.AddLargeButton("btn_lock_draw_mode", 0, isLocked ? "Locked" : "Lock Draw",
                        "Lock Drawing Mode: Keep drawing this shape repeatedly without resetting to selection cursor",
                        isLocked,
                        [&]() {
                            canvas.shapeCreation.lockDrawingMode = !canvas.shapeCreation.lockDrawingMode;
                            if (canvas.shapeCreation.lockDrawingMode) {
                                canvas.shapeCreation.isActive = true;
                                inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::DrawingShape);
                                inputSM.currentAction = InteractionState::DrawingShape;
                            }
                            LOG_INFO(CanvasEngine, std::string("Lock Drawing Mode set to: ") + (canvas.shapeCreation.lockDrawingMode ? "ENABLED" : "DISABLED"));
                        },
                        false, ImVec2(76.0f, 58.0f)
                    );

                    // 2. Shape Switcher / Current Shape Type
                    const char* shapeTypeName = "Rectangle";
                    Folio::ShapeType curType = selectedShape ? selectedShape->shapeType : canvas.shapeCreation.shapeType;
                    switch (curType) {
                        case Folio::ShapeType::Rectangle: shapeTypeName = "Rectangle"; break;
                        case Folio::ShapeType::RoundedRectangle: shapeTypeName = "Round Rect"; break;
                        case Folio::ShapeType::Ellipse: shapeTypeName = "Ellipse"; break;
                        case Folio::ShapeType::Triangle: shapeTypeName = "Triangle"; break;
                        case Folio::ShapeType::Diamond: shapeTypeName = "Diamond"; break;
                        case Folio::ShapeType::Star: shapeTypeName = "Star"; break;
                        case Folio::ShapeType::Arrow: shapeTypeName = "Arrow"; break;
                        case Folio::ShapeType::DoubleArrow: shapeTypeName = "Dbl Arrow"; break;
                        case Folio::ShapeType::Hexagon: shapeTypeName = "Hexagon"; break;
                        case Folio::ShapeType::Line: shapeTypeName = "Line"; break;
                        default: break;
                    }

                    sec.AddSplitButton("btn_shape_type_switch", 0, shapeTypeName, "Convert selected shape or pick active draw shape", false,
                        [&]() {
                            canvas.StartShapeCreation(curType, canvas.shapeCreation.lockDrawingMode);
                            inputSM.SetToolForDevice(inputSM.ActiveDevice, InteractionState::DrawingShape);
                            inputSM.currentAction = InteractionState::DrawingShape;
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Switch Shape Geometry");
                            menu.AddCustom([&]() {
                                struct ShapeItem {
                                    Folio::ShapeType type;
                                    const char* name;
                                };
                                static const ShapeItem s_shapeItems[] = {
                                    { Folio::ShapeType::Rectangle, "Rectangle" },
                                    { Folio::ShapeType::RoundedRectangle, "Rounded Rect" },
                                    { Folio::ShapeType::Ellipse, "Ellipse / Circle" },
                                    { Folio::ShapeType::Triangle, "Triangle" },
                                    { Folio::ShapeType::Diamond, "Diamond" },
                                    { Folio::ShapeType::Star, "Star (5-Point)" },
                                    { Folio::ShapeType::Arrow, "Arrow" },
                                    { Folio::ShapeType::DoubleArrow, "Double Arrow" },
                                    { Folio::ShapeType::Hexagon, "Hexagon" },
                                    { Folio::ShapeType::Line, "Line" }
                                };

                                for (const auto& item : s_shapeItems) {
                                    ImGui::PushID(static_cast<int>(item.type) + 8000);
                                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                                    float rowHeight = 32.0f;
                                    float menuW = 210.0f;
                                    if (ImGui::Selectable("##switch_shape", false, 0, ImVec2(menuW, rowHeight))) {
                                        canvas.shapeCreation.shapeType = item.type;
                                        if (selectedShape) {
                                            selectedShape->shapeType = item.type;
                                            selectedShape->UpdateBounds();
                                            canvas.SyncSelectionToSpatialIndex(currentSession);
                                            canvas.selectionGizmo.RecalculateBounds();
                                            canvas.needsFullRebake = true;
                                            canvas.isDirty = true;
                                        }
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImDrawList* dl = ImGui::GetWindowDrawList();
                                    ImVec2 iconMin(p0.x + 8.0f, p0.y + 5.0f);
                                    ImVec2 iconMax(p0.x + 30.0f, p0.y + 27.0f);
                                    Folio::ShapeObject::DrawShapeIconImGui(dl, item.type, iconMin, iconMax,
                                        ImGui::GetColorU32(theme.colorText),
                                        ImGui::GetColorU32(ImVec4(0.0f, 0.47f, 0.83f, 0.25f)));

                                    dl->AddText(ImVec2(p0.x + 38.0f, p0.y + 7.0f), ImGui::GetColorU32(theme.colorText), item.name);
                                    ImGui::PopID();
                                }
                            });
                        },
                        false, ImVec2(80.0f, 58.0f)
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 2: Shape Fill (Infill)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_shape_fill", "Shape Fill", theme, isMini);

                    Folio::ShapeFillType curFill = selectedShape ? selectedShape->fillType : canvas.shapeCreation.defaultFillType;
                    const char* fillLabel = "Solid";
                    switch (curFill) {
                        case Folio::ShapeFillType::None: fillLabel = "No Fill"; break;
                        case Folio::ShapeFillType::Solid: fillLabel = "Solid"; break;
                        case Folio::ShapeFillType::SemiTransparent: fillLabel = "Translucent"; break;
                        case Folio::ShapeFillType::LinearGradient: fillLabel = "Linear Grad"; break;
                        case Folio::ShapeFillType::RadialGradient: fillLabel = "Radial Grad"; break;
                        case Folio::ShapeFillType::HatchDiagonal: fillLabel = "Hatch Diag"; break;
                        case Folio::ShapeFillType::HatchCross: fillLabel = "Hatch Cross"; break;
                        default: break;
                    }

                    sec.AddSplitButton("btn_shape_fill_type", 0, fillLabel, "Fill Style: None, Solid, Translucent, Gradient, or Hatch pattern", false,
                        [&]() {
                            Folio::ShapeFillType nextFill = (curFill == Folio::ShapeFillType::SemiTransparent) ? Folio::ShapeFillType::Solid :
                                                           (curFill == Folio::ShapeFillType::Solid) ? Folio::ShapeFillType::None :
                                                           Folio::ShapeFillType::SemiTransparent;
                            canvas.shapeCreation.defaultFillType = nextFill;
                            if (selectedShape) {
                                selectedShape->fillType = nextFill;
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                            }
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Infill Style");
                            auto setFill = [&](Folio::ShapeFillType ft) {
                                canvas.shapeCreation.defaultFillType = ft;
                                if (selectedShape) {
                                    selectedShape->fillType = ft;
                                    canvas.needsFullRebake = true;
                                    canvas.isDirty = true;
                                }
                            };
                            menu.AddItem("None (Transparent)", 0, "", [=]() { setFill(Folio::ShapeFillType::None); });
                            menu.AddItem("Solid Infill", 0, "", [=]() { setFill(Folio::ShapeFillType::Solid); });
                            menu.AddItem("Semi-Transparent (Translucent)", 0, "", [=]() { setFill(Folio::ShapeFillType::SemiTransparent); });
                            menu.AddItem("Linear Gradient", 0, "", [=]() { setFill(Folio::ShapeFillType::LinearGradient); });
                            menu.AddItem("Radial Gradient", 0, "", [=]() { setFill(Folio::ShapeFillType::RadialGradient); });
                        },
                        false, ImVec2(78.0f, 58.0f)
                    );

                    sec.AddSplitButton("btn_shape_fill_color", 0, "Fill Color", "Change shape infill color and transparency", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Theme Fill Colors");
                            menu.AddCustom([&]() {
                                static const BLRgba32 s_palette[] = {
                                    BLRgba32(0x00, 0x78, 0xD4, 0x40), // Accent Blue
                                    BLRgba32(0x10, 0x7C, 0x41, 0x40), // Green
                                    BLRgba32(0xD8, 0x3B, 0x01, 0x40), // Orange
                                    BLRgba32(0xE8, 0x11, 0x23, 0x40), // Red
                                    BLRgba32(0x5C, 0x2D, 0x91, 0x40), // Purple
                                    BLRgba32(0x00, 0x82, 0x72, 0x40), // Teal
                                    BLRgba32(0xFF, 0xB9, 0x00, 0x40), // Yellow
                                    BLRgba32(0x50, 0x50, 0x50, 0x40), // Grey
                                    // Solid row
                                    BLRgba32(0x00, 0x78, 0xD4, 0xFF),
                                    BLRgba32(0x10, 0x7C, 0x41, 0xFF),
                                    BLRgba32(0xD8, 0x3B, 0x01, 0xFF),
                                    BLRgba32(0xE8, 0x11, 0x23, 0xFF),
                                    BLRgba32(0x5C, 0x2D, 0x91, 0xFF),
                                    BLRgba32(0x00, 0x82, 0x72, 0xFF),
                                    BLRgba32(0xFF, 0xB9, 0x00, 0xFF),
                                    BLRgba32(0x20, 0x20, 0x20, 0xFF)
                                };

                                for (int i = 0; i < 16; ++i) {
                                    if (i > 0 && i % 8 != 0) ImGui::SameLine(0, 4.0f);
                                    ImGui::PushID(i + 2000);
                                    const auto& c = s_palette[i];
                                    ImVec4 col(c.r() / 255.0f, c.g() / 255.0f, c.b() / 255.0f, c.a() / 255.0f);
                                    if (ImGui::ColorButton("##fill_btn", col, ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24))) {
                                        canvas.shapeCreation.defaultFillColor = c;
                                        if (selectedShape) {
                                            selectedShape->fillColor = c;
                                            canvas.needsFullRebake = true;
                                            canvas.isDirty = true;
                                        }
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }

                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::Spacing();

                                BLRgba32 activeFill = selectedShape ? selectedShape->fillColor : canvas.shapeCreation.defaultFillColor;
                                float colArr[4] = { activeFill.r() / 255.0f, activeFill.g() / 255.0f, activeFill.b() / 255.0f, activeFill.a() / 255.0f };
                                if (ImGui::ColorEdit4("Custom Fill", colArr, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB)) {
                                    BLRgba32 newCol(
                                        static_cast<uint32_t>(colArr[0] * 255.0f),
                                        static_cast<uint32_t>(colArr[1] * 255.0f),
                                        static_cast<uint32_t>(colArr[2] * 255.0f),
                                        static_cast<uint32_t>(colArr[3] * 255.0f)
                                    );
                                    canvas.shapeCreation.defaultFillColor = newCol;
                                    if (selectedShape) {
                                        selectedShape->fillColor = newCol;
                                        canvas.needsFullRebake = true;
                                        canvas.isDirty = true;
                                    }
                                }
                            });
                        },
                        false, ImVec2(68.0f, 58.0f)
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 3: Shape Outline
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_shape_outline", "Outline", theme, isMini);

                    Folio::ShapeOutlineType curOut = selectedShape ? selectedShape->outlineType : canvas.shapeCreation.defaultOutlineType;
                    const char* outLabel = "Solid";
                    switch (curOut) {
                        case Folio::ShapeOutlineType::None: outLabel = "No Outline"; break;
                        case Folio::ShapeOutlineType::Solid: outLabel = "Solid"; break;
                        case Folio::ShapeOutlineType::Dashed: outLabel = "Dashed"; break;
                        case Folio::ShapeOutlineType::Dotted: outLabel = "Dotted"; break;
                        case Folio::ShapeOutlineType::DashDot: outLabel = "Dash-Dot"; break;
                        default: break;
                    }

                    sec.AddSplitButton("btn_shape_out_style", 0, outLabel, "Outline Pattern: Solid, Dashed, Dotted, or None", false,
                        [&]() {
                            Folio::ShapeOutlineType nextOut = (curOut == Folio::ShapeOutlineType::Solid) ? Folio::ShapeOutlineType::Dashed :
                                                             (curOut == Folio::ShapeOutlineType::Dashed) ? Folio::ShapeOutlineType::Dotted :
                                                             (curOut == Folio::ShapeOutlineType::Dotted) ? Folio::ShapeOutlineType::None :
                                                             Folio::ShapeOutlineType::Solid;
                            canvas.shapeCreation.defaultOutlineType = nextOut;
                            if (selectedShape) {
                                selectedShape->outlineType = nextOut;
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                            }
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Outline Style");
                            auto setOut = [&](Folio::ShapeOutlineType ot) {
                                canvas.shapeCreation.defaultOutlineType = ot;
                                if (selectedShape) {
                                    selectedShape->outlineType = ot;
                                    canvas.needsFullRebake = true;
                                    canvas.isDirty = true;
                                }
                            };
                            menu.AddItem("None", 0, "", [=]() { setOut(Folio::ShapeOutlineType::None); });
                            menu.AddItem("Solid Line", 0, "", [=]() { setOut(Folio::ShapeOutlineType::Solid); });
                            menu.AddItem("Dashed Line", 0, "", [=]() { setOut(Folio::ShapeOutlineType::Dashed); });
                            menu.AddItem("Dotted Line", 0, "", [=]() { setOut(Folio::ShapeOutlineType::Dotted); });
                            menu.AddItem("Dash-Dot Line", 0, "", [=]() { setOut(Folio::ShapeOutlineType::DashDot); });
                        },
                        false, ImVec2(78.0f, 58.0f)
                    );

                    sec.AddSplitButton("btn_shape_stroke_color", 0, "Outline Col", "Change outline border color", false,
                        [&]() {},
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Outline Palette");
                            menu.AddCustom([&]() {
                                static const BLRgba32 s_strokePalette[] = {
                                    BLRgba32(0x18, 0x1A, 0x20, 0xFF), // Dark charcoal
                                    BLRgba32(0x00, 0x78, 0xD4, 0xFF), // Accent Blue
                                    BLRgba32(0x10, 0x7C, 0x41, 0xFF), // Green
                                    BLRgba32(0xD8, 0x3B, 0x01, 0xFF), // Orange
                                    BLRgba32(0xE8, 0x11, 0x23, 0xFF), // Red
                                    BLRgba32(0x5C, 0x2D, 0x91, 0xFF), // Purple
                                    BLRgba32(0x00, 0x82, 0x72, 0xFF), // Teal
                                    BLRgba32(0xFF, 0xFF, 0xFF, 0xFF)  // White
                                };

                                for (int i = 0; i < 8; ++i) {
                                    if (i > 0) ImGui::SameLine(0, 4.0f);
                                    ImGui::PushID(i + 3000);
                                    const auto& c = s_strokePalette[i];
                                    ImVec4 col(c.r() / 255.0f, c.g() / 255.0f, c.b() / 255.0f, c.a() / 255.0f);
                                    if (ImGui::ColorButton("##strk_btn", col, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24))) {
                                        canvas.shapeCreation.defaultOutlineColor = c;
                                        if (selectedShape) {
                                            selectedShape->strokeColor = c;
                                            canvas.needsFullRebake = true;
                                            canvas.isDirty = true;
                                        }
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }

                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::Spacing();

                                BLRgba32 activeStroke = selectedShape ? selectedShape->strokeColor : canvas.shapeCreation.defaultOutlineColor;
                                float colArr[4] = { activeStroke.r() / 255.0f, activeStroke.g() / 255.0f, activeStroke.b() / 255.0f, activeStroke.a() / 255.0f };
                                if (ImGui::ColorEdit4("Custom Outline", colArr, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB)) {
                                    BLRgba32 newCol(
                                        static_cast<uint32_t>(colArr[0] * 255.0f),
                                        static_cast<uint32_t>(colArr[1] * 255.0f),
                                        static_cast<uint32_t>(colArr[2] * 255.0f),
                                        static_cast<uint32_t>(colArr[3] * 255.0f)
                                    );
                                    canvas.shapeCreation.defaultOutlineColor = newCol;
                                    if (selectedShape) {
                                        selectedShape->strokeColor = newCol;
                                        canvas.needsFullRebake = true;
                                        canvas.isDirty = true;
                                    }
                                }
                            });
                        },
                        false, ImVec2(68.0f, 58.0f)
                    );

                    double curW = selectedShape ? selectedShape->strokeWidth : canvas.shapeCreation.defaultStrokeWidth;
                    char wStr[32];
                    std::snprintf(wStr, sizeof(wStr), "%.1f mm", curW);

                    sec.AddSplitButton("btn_shape_weight", 0, wStr, "Outline Thickness: Adjust border stroke width in millimeters", false,
                        [&]() {
                            double nw = (curW < 0.9) ? 1.0 : (curW < 1.9) ? 2.0 : (curW < 2.9) ? 3.0 : (curW < 4.9) ? 5.0 : 0.5;
                            canvas.shapeCreation.defaultStrokeWidth = nw;
                            if (selectedShape) {
                                selectedShape->strokeWidth = nw;
                                selectedShape->UpdateBounds();
                                canvas.SyncSelectionToSpatialIndex(currentSession);
                                canvas.selectionGizmo.RecalculateBounds();
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                            }
                        },
                        [&](FolioUI::FlyoutMenuBuilder& menu) {
                            menu.AddHeader("Border Thickness");
                            auto setW = [&](double w) {
                                canvas.shapeCreation.defaultStrokeWidth = w;
                                if (selectedShape) {
                                    selectedShape->strokeWidth = w;
                                    selectedShape->UpdateBounds();
                                    canvas.SyncSelectionToSpatialIndex(currentSession);
                                    canvas.selectionGizmo.RecalculateBounds();
                                    canvas.needsFullRebake = true;
                                    canvas.isDirty = true;
                                }
                            };
                            menu.AddItem("Thin (0.5 mm)", 0, "", [=]() { setW(0.5); });
                            menu.AddItem("Normal (1.0 mm)", 0, "", [=]() { setW(1.0); });
                            menu.AddItem("Medium (2.0 mm)", 0, "", [=]() { setW(2.0); });
                            menu.AddItem("Thick (3.0 mm)", 0, "", [=]() { setW(3.0); });
                            menu.AddItem("Extra Thick (5.0 mm)", 0, "", [=]() { setW(5.0); });
                        },
                        false, ImVec2(68.0f, 58.0f)
                    );

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 4: Geometry & Parameters (Shape-specific)
                // -------------------------------------------------------------
                if (selectedShape) {
                    if (selectedShape->shapeType == Folio::ShapeType::RoundedRectangle ||
                        selectedShape->shapeType == Folio::ShapeType::Star ||
                        selectedShape->shapeType == Folio::ShapeType::Arrow ||
                        selectedShape->shapeType == Folio::ShapeType::DoubleArrow) {

                        FolioUI::ToolbarSectionBuilder sec("grp_shape_geom", "Geometry", theme, isMini);
                        sec.AddSplitButton("btn_shape_params", 0, "Adjust", "Fine-tune shape geometry attributes (corner radius, star points, arrow head)", false,
                            [&]() {},
                            [&](FolioUI::FlyoutMenuBuilder& menu) {
                                menu.AddHeader("Shape Parameters");
                                menu.AddCustom([&]() {
                                    if (selectedShape->shapeType == Folio::ShapeType::RoundedRectangle) {
                                        float cr = static_cast<float>(selectedShape->cornerRadius);
                                        if (ImGui::SliderFloat("Corner Radius", &cr, 0.5f, 25.0f, "%.1f mm")) {
                                            selectedShape->cornerRadius = cr;
                                            canvas.needsFullRebake = true;
                                            canvas.isDirty = true;
                                        }
                                    } else if (selectedShape->shapeType == Folio::ShapeType::Star) {
                                        int pts = static_cast<int>(selectedShape->param1);
                                        if (ImGui::SliderInt("Points", &pts, 3, 16)) {
                                            selectedShape->param1 = pts;
                                            canvas.needsFullRebake = true;
                                            canvas.isDirty = true;
                                        }
                                        float ratio = static_cast<float>(selectedShape->param2);
                                        if (ImGui::SliderFloat("Depth", &ratio, 0.15f, 0.85f, "%.2f")) {
                                            selectedShape->param2 = ratio;
                                            canvas.needsFullRebake = true;
                                            canvas.isDirty = true;
                                        }
                                    } else if (selectedShape->shapeType == Folio::ShapeType::Arrow ||
                                               selectedShape->shapeType == Folio::ShapeType::DoubleArrow) {
                                        float headRatio = static_cast<float>(selectedShape->param2);
                                        if (ImGui::SliderFloat("Head Width", &headRatio, 0.15f, 0.65f, "%.2f")) {
                                            selectedShape->param2 = headRatio;
                                            canvas.needsFullRebake = true;
                                            canvas.isDirty = true;
                                        }
                                    }
                                });
                            },
                            false, ImVec2(68.0f, 58.0f)
                        );
                        sec.Render();
                    }
                }

                // -------------------------------------------------------------
                // SECTION 5: Arrange & Actions
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_shape_arrange", "Arrange", theme, isMini);
                    sec.BeginStack();

                    sec.AddSmallButton("btn_shape_dup", 0, "Duplicate", "Clone shape with offset", false, [&]() {
                        if (selectedShape && currentSession) {
                            auto activePage = currentSession->GetActivePage();
                            if (activePage) {
                                auto clone = std::make_shared<Folio::ShapeObject>(*selectedShape);
                                clone->guuid = GUIDGenerator::GenerateV4();
                                clone->uid = UIDGenerator::Next();
                                clone->worldX += 10.0;
                                clone->worldY += 10.0;
                                clone->UpdateBounds();
                                activePage->AddObject(clone);

                                canvas.ClearSelection(currentSession);
                                clone->isSelected = 1;
                                canvas.selectionGizmo.SetSelectedObjects(activePage->objects);
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                                LOG_INFO(CanvasEngine, "Duplicated shape (new uid=" + std::to_string(clone->uid) + ")");
                            }
                        }
                    });

                    sec.AddSmallButton("btn_shape_del", 0, "Delete", "Delete selected shape", false, [&]() {
                        canvas.DeleteSelectedObjects(currentSession);
                    });

                    sec.EndStack();
                    sec.Render();
                }
            }
            else if (activeTab == RibbonTab::PdfTools) {
                auto* pv = Folio::PdfViewerPage::GetActiveInstance();

                // -------------------------------------------------------------
                // SECTION 1: Reading Mode (Invert Canvas & Sidebar Toggle)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_pdf_reading", "Reading Mode", theme, isMini);

                    bool isDark = canvas.inkColorInverted;
                    sec.AddLargeButton("btn_pdf_dark_mode", 0, isDark ? "Light Canvas" : "Invert Canvas",
                        "Invert Canvas: Invert PDF colors and canvas background for comfortable reading at night",
                        isDark,
                        [&]() {
                            canvas.inkColorInverted = !canvas.inkColorInverted;
                            isCanvasInverted = canvas.inkColorInverted;
                            if (canvas.inkColorInverted) {
                                canvas.canvasBgColor = BLRgba32(0x1E, 0x20, 0x26);
                                canvas.gridLineColor = BLRgba32(0x34, 0x38, 0x44);
                            } else {
                                canvas.canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
                                canvas.gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
                            }
                            canvas.isDirty = true;
                            canvas.needsFullRebake = true;
                        },
                        false, ImVec2(78.0f, 58.0f));

                    bool sbOpen = pv ? pv->isSidebarOpen : true;
                    sec.AddLargeButton("btn_pdf_sidebar_toggle", 0, sbOpen ? "Hide Panel" : "Show Panel",
                        "Toggle Navigation Sidebar (Thumbnails, Outline, Bookmarks)",
                        sbOpen,
                        [&]() {
                            if (pv) pv->ToggleSidebar();
                        },
                        false, ImVec2(76.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 2: Navigation
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_pdf_nav", "Navigation", theme, isMini);

                    sec.AddLargeButton("btn_pdf_prev_page", 0, "Prev Page",
                        "Go to previous document page",
                        false,
                        [&]() {
                            if (pv) pv->PrevPage();
                        },
                        false, ImVec2(72.0f, 58.0f));

                    sec.AddLargeButton("btn_pdf_next_page", 0, "Next Page",
                        "Go to next document page",
                        false,
                        [&]() {
                            if (pv) pv->NextPage();
                        },
                        false, ImVec2(72.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 3: Zoom Presets
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_pdf_zoom", "Zoom", theme, isMini);

                    sec.AddLargeButton("btn_pdf_fit_width", 0, "Fit Width",
                        "Fit document page width to viewport",
                        false,
                        [&]() {
                            if (pv) pv->SetZoomScale(1.0f);
                        },
                        false, ImVec2(68.0f, 58.0f));

                    sec.AddLargeButton("btn_pdf_zoom_100", 0, "100%",
                        "Zoom to 100%",
                        false,
                        [&]() {
                            if (pv) pv->SetZoomScale(1.0f);
                        },
                        false, ImVec2(54.0f, 58.0f));

                    sec.AddLargeButton("btn_pdf_zoom_150", 0, "150%",
                        "Zoom to 150%",
                        false,
                        [&]() {
                            if (pv) pv->SetZoomScale(1.5f);
                        },
                        false, ImVec2(54.0f, 58.0f));

                    sec.AddLargeButton("btn_pdf_zoom_200", 0, "200%",
                        "Zoom to 200%",
                        false,
                        [&]() {
                            if (pv) pv->SetZoomScale(2.0f);
                        },
                        false, ImVec2(54.0f, 58.0f));

                    sec.Render();
                }

                // -------------------------------------------------------------
                // SECTION 4: Text Annotation Tools (Snapping Highlighter & Selector)
                // -------------------------------------------------------------
                {
                    FolioUI::ToolbarSectionBuilder sec("grp_pdf_text_tools", "Text & Annotation", theme, isMini);

                    bool isHl = pv ? (pv->activeTool == Folio::PdfToolMode::Highlight) : true;
                    sec.AddLargeButton("btn_pdf_highlighter", 0, "Text Highlight",
                        "Text Highlighter: Drag across text to highlight. Click an existing highlight to erase it.",
                        isHl,
                        [&]() {
                            if (pv) pv->activeTool = Folio::PdfToolMode::Highlight;
                        },
                        false, ImVec2(80.0f, 58.0f));

                    bool isSel = pv ? (pv->activeTool == Folio::PdfToolMode::Select) : false;
                    sec.AddLargeButton("btn_pdf_select_mode", 0, "Select Text",
                        "Select Text: Select text on PDF pages to copy, extract Markdown, or highlight.",
                        isSel,
                        [&]() {
                            if (pv) pv->activeTool = Folio::PdfToolMode::Select;
                        },
                        false, ImVec2(76.0f, 58.0f));

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