#pragma once
#include "imgui.h"
#include "core/engine/canvas_engine.hpp"
#include "input/input_state_machine.hpp"
#include "app/theme_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/icon_manager.hpp"
#include <string>

enum class AppViewMode { CanvasWorkspace, NotebookHub };
enum class RibbonTab { Home, Insert, Draw, History, Review, View, Help };

class RibbonBar {
public:
    RibbonTab activeTab = RibbonTab::Draw;
    bool isCollapsed = false;

    // --- Ribbon UI Components ---

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
        int pushedColors = 0;
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
            pushedColors = 2;
        }

        bool clicked = false;
        if (iconTex != 0) {
            // UV coordinate flipping:
            // Normal: (0,0) -> (1,1)
            // Flipped X: (1,0) -> (0,1)
            ImVec2 uv0 = flipHorizontal ? ImVec2(1.0f, 0.0f) : ImVec2(0.0f, 0.0f);
            ImVec2 uv1 = flipHorizontal ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 1.0f);

            clicked = ImGui::ImageButton(
                strId,
                (ImTextureID)(intptr_t)iconTex,
                ImVec2(size.x - 18.0f, size.y - 20.0f),
                uv0, uv1,
                ImVec4(0, 0, 0, 0),
                ImVec4(1, 1, 1, 1)
            );
        } else {
            // Fallback text button if SVG file is missing on disk
            clicked = ImGui::Button(label ? label : strId, size);
        }

        if (pushedColors > 0) ImGui::PopStyleColor(pushedColors);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip);
            ImGui::EndTooltip();
        }

        return clicked;
    }

    static void BeginGroup(const char* groupId) {
        ImGui::BeginGroup();
        ImGui::PushID(groupId);
    }

    static void EndGroup(const char* label, const ThemeManager& theme, float groupWidth) {
        ImGui::PopID();
        
        ImVec2 groupMin = ImGui::GetItemRectMin();
        ImVec2 groupMax = ImGui::GetItemRectMax();
        float actualW = (groupWidth > 0.0f) ? groupWidth : (groupMax.x - groupMin.x);
        if (actualW < 40.0f) actualW = 40.0f;

        ImVec2 textSize = ImGui::CalcTextSize(label);
        float textX = groupMin.x + (actualW - textSize.x) * 0.5f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Category Label
        drawList->AddText(
            ImVec2(textX, groupMin.y + 54.0f),
            ImGui::ColorConvertFloat4ToU32(theme.colorTextMuted),
            label
        );

        // Vertical Section Separator Line
        float dividerX = groupMin.x + actualW + 8.0f;
        drawList->AddLine(
            ImVec2(dividerX, groupMin.y + 2.0f),
            ImVec2(dividerX, groupMin.y + 68.0f),
            ImGui::ColorConvertFloat4ToU32(theme.colorBorder),
            1.0f
        );

        ImGui::EndGroup();
        ImGui::SameLine(0, 20.0f); // Spacing to next section
    }

    float GetCurrentHeight() const {
        return isCollapsed ? 68.0f : 160.0f;
    }

    void Render(float width, AppViewMode& outViewMode, CanvasEngine& canvas, InputStateMachine& inputSM, const ThemeManager& theme) {
        float currentH = GetCurrentHeight();
        if (width <= 0.0f) width = ImGui::GetContentRegionAvail().x;

        // 1. "File" Hub Button
        bool isHubOpen = (outViewMode == AppViewMode::NotebookHub);
        ImGui::PushStyleColor(ImGuiCol_Button, isHubOpen ? theme.colorPrimaryHover : theme.colorPrimary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPrimaryHover);
        
        ImGui::PushFont(isHubOpen ? FolioTheme::FontRibbonBoldLarge : FolioTheme::FontRibbonLarge);
        if (ImGui::Button("File", ImVec2(90, 52))) {
            outViewMode = (outViewMode == AppViewMode::NotebookHub) ? AppViewMode::CanvasWorkspace : AppViewMode::NotebookHub;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(2);

        ImGui::SameLine(0, 16.0f);

        // 2. Ribbon Tabs Row
        const char* tabNames[] = { "Home", "Insert", "Draw", "History", "Review", "View", "Help" };
        RibbonTab tabEnums[] = {
            RibbonTab::Home, RibbonTab::Insert, RibbonTab::Draw,
            RibbonTab::History, RibbonTab::Review, RibbonTab::View, RibbonTab::Help
        };

        for (int i = 0; i < 7; ++i) {
            bool isSelected = (activeTab == tabEnums[i] && !isCollapsed && outViewMode == AppViewMode::CanvasWorkspace);

            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, theme.colorShelf);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorShelf);
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
                ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorPanel);
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorTextMuted);
                ImGui::PushFont(FolioTheme::FontRibbonLarge);
            }

            ImGui::PushID(i);
            if (ImGui::Button(tabNames[i], ImVec2(120, 52))) {
                if (outViewMode == AppViewMode::NotebookHub) outViewMode = AppViewMode::CanvasWorkspace;
                if (activeTab == tabEnums[i]) isCollapsed = !isCollapsed;
                else { activeTab = tabEnums[i]; isCollapsed = false; }
            }
            ImGui::PopID();
            ImGui::PopFont();
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 6.0f);
        }

        // Collapse / Expand Toggle
        ImGui::SameLine(width - 64.0f);
        ImGui::PushFont(FolioTheme::FontRibbonLarge);
        if (ImGui::Button(isCollapsed ? "v" : "^", ImVec2(50, 52))) {
            isCollapsed = !isCollapsed;
        }
        ImGui::PopFont();

        // 3. Lower Shelf Subsections
        if (!isCollapsed && outViewMode == AppViewMode::CanvasWorkspace) {
            ImGui::SetCursorPosY(58.0f);
            ImGui::SetCursorPosX(0.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorShelf);
            ImGui::BeginChild("##ToolShelf", ImVec2(width, currentH - 58.0f), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos(ImVec2(16, 10));

            // Load shared icons (auto-cached)
            GLuint redoIcon   = g_IconManager.LoadOrGetSVG("redo",   "assets/icons/undo-redo.svg", 128);
            GLuint iconLasso  = g_IconManager.LoadOrGetSVG("lasso",  "assets/icons/lasso.svg", 128);
            GLuint iconEraser = g_IconManager.LoadOrGetSVG("eraser", "assets/icons/eraser.svg", 128);
            GLuint iconPen    = g_IconManager.LoadOrGetSVG("pen",    "assets/icons/pen.svg", 128);
            GLuint iconHigh   = g_IconManager.LoadOrGetSVG("high",   "assets/icons/highlighter.svg", 128);

            if (activeTab == RibbonTab::Home) {
                BeginGroup("grp_home_clipboard");
                IconButton("##Cut", 0, "Cut", "Cut selection", theme, false, false, ImVec2(60, 48));
                ImGui::SameLine();
                IconButton("##Copy", 0, "Copy", "Copy selection", theme, false, false, ImVec2(60, 48));
                ImGui::SameLine();
                IconButton("##Paste", 0, "Paste", "Paste clipboard", theme, false, false, ImVec2(60, 48));
                EndGroup("Clipboard", theme, 195.0f);

                BeginGroup("grp_home_history");
                // Mirrored Redo Icon -> Undo
                if (IconButton("##UndoHome", redoIcon, "Undo", "Undo action (Ctrl+Z)", theme, false, true)) {
                    // canvas.UndoLast();
                }
                ImGui::SameLine();
                // Normal Redo Icon -> Redo
                if (IconButton("##RedoHome", redoIcon, "Redo", "Redo action (Ctrl+Y)", theme, false, false)) {
                    // Trigger redo if state machine has redo history
                }
                EndGroup("History", theme, 125.0f);
            }
            else if (activeTab == RibbonTab::Draw) {
                // SUBSECTION 1: Tools
                BeginGroup("grp_tools");
                // Undo (using horizontally flipped redoIcon)
                if (IconButton("##Undo", redoIcon, "Undo", "Undo last stroke (Ctrl+Z)", theme, false, true)) {
                    // TODO: trigger undo
                }
                ImGui::SameLine();
                // Redo (using standard redoIcon)
                if (IconButton("##Redo", redoIcon, "Redo", "Redo stroke (Ctrl+Y)", theme, false, false)) {
                    // Redo action
                }
                ImGui::SameLine();
                bool isLasso = (inputSM.currentAction == InteractionState::Selecting);
                if (IconButton("##Lasso", iconLasso, "Lasso", "Lasso Select", theme, isLasso)) {
                    inputSM.currentAction = InteractionState::Selecting;
                }
                ImGui::SameLine();
                bool isEraser = (inputSM.currentAction == InteractionState::Eraser);
                if (IconButton("##Eraser", iconEraser, "Eraser", "Vector Stroke Eraser", theme, isEraser)) {
                    inputSM.currentAction = InteractionState::Eraser;
                }
                EndGroup("Tools", theme, 245.0f);

                // SUBSECTION 2: Pens & Highlighters
                BeginGroup("grp_pens");
                auto& activePen = inputSM.palette.GetActivePen();
                bool isWhitePen = (inputSM.currentAction == InteractionState::Inking && activePen.penType == PenType::Pen && activePen.color == BLRgba32(0xFF, 0xFF, 0xFF));
                if (IconButton("##PenWhite", iconPen, "White", "White 3px Pen", theme, isWhitePen)) {
                    inputSM.currentAction = InteractionState::Inking;
                    activePen.penType = PenType::Pen;
                    activePen.color = BLRgba32(0xFF, 0xFF, 0xFF);
                    activePen.baseSize = 3.0f;
                }
                ImGui::SameLine();
                bool isRedPen = (inputSM.currentAction == InteractionState::Inking && activePen.penType == PenType::Pen && activePen.color == BLRgba32(0xE5, 0x39, 0x35));
                if (IconButton("##PenRed", iconPen, "Red", "Red 3px Pen", theme, isRedPen)) {
                    inputSM.currentAction = InteractionState::Inking;
                    activePen.penType = PenType::Pen;
                    activePen.color = BLRgba32(0xE5, 0x39, 0x35);
                    activePen.baseSize = 3.0f;
                }
                ImGui::SameLine();
                bool isHigh = (inputSM.currentAction == InteractionState::Inking && activePen.penType == PenType::Highlighter);
                if (IconButton("##Highlighter", iconHigh, "High", "Highlighter", theme, isHigh)) {
                    inputSM.currentAction = InteractionState::Inking;
                    activePen.penType = PenType::Highlighter;
                    activePen.color = BLRgba32(0xFF, 0xD6, 0x00);
                    activePen.baseSize = 16.0f;
                }
                EndGroup("Pens", theme, 185.0f);

                // SUBSECTION 3: Stroke Properties
                BeginGroup("grp_props");
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
                ImGui::SetNextItemWidth(120.0f);
                ImGui::SliderFloat("##WidthSlider", &activePen.baseSize, 1.0f, 32.0f, "%.1fpx");

                ImGui::SameLine(0, 12.0f);
                static ImVec4 customColor = ImVec4(1, 1, 1, 1);
                if (ImGui::ColorEdit4("##ColorPicker", (float*)&customColor, ImGuiColorEditFlags_NoInputs)) {
                    activePen.color = BLRgba32(
                        (uint8_t)(customColor.x * 255), (uint8_t)(customColor.y * 255),
                        (uint8_t)(customColor.z * 255), (uint8_t)(customColor.w * 255)
                    );
                }
                EndGroup("Stroke Settings", theme, 190.0f);
            }
            else if (activeTab == RibbonTab::View) {
                BeginGroup("grp_view_zoom");
                if (IconButton("##ResetZoom", 0, "100%", "Reset View to 100%", theme, false, false, ImVec2(65, 48))) {
                    canvas.transform.panXMm = 0.0;
                    canvas.transform.panYMm = 0.0;
                    canvas.transform.zoom = 1.0;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                EndGroup("Zoom", theme, 75.0f);

                BeginGroup("grp_view_grid");
                bool isGrid = (canvas.currentPaperStyle == PaperStyle::Grid);
                if (IconButton("##PaperGrid", 0, "Grid", "Grid Paper", theme, isGrid, false, ImVec2(65, 48))) {
                    canvas.currentPaperStyle = PaperStyle::Grid;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::SameLine();
                bool isLined = (canvas.currentPaperStyle == PaperStyle::Lined);
                if (IconButton("##PaperLined", 0, "Lined", "Lined Paper", theme, isLined, false, ImVec2(65, 48))) {
                    canvas.currentPaperStyle = PaperStyle::Lined;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                ImGui::SameLine();
                bool isBlank = (canvas.currentPaperStyle == PaperStyle::Blank);
                if (IconButton("##PaperBlank", 0, "Blank", "Blank Paper", theme, isBlank, false, ImVec2(65, 48))) {
                    canvas.currentPaperStyle = PaperStyle::Blank;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                }
                EndGroup("Paper Style", theme, 215.0f);
            }
            else {
                BeginGroup("grp_general");
                IconButton("##General", 0, "Ready", "Feature coming soon", theme, false, false, ImVec2(80, 48));
                EndGroup("Status", theme, 90.0f);
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
    }
};