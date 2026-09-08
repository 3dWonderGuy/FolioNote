#pragma once

#include "imgui.h"
#include "app/theme_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/icon_manager.hpp"
#include "ui/components/toolbar_builder.hpp"
#include <vector>
#include <string>
#include <algorithm>

struct EditorCommandDef {
    std::string id;
    std::string name;
    std::string category;
    std::string iconKey;
    std::string tooltip;
};

struct EditorSectionDef {
    std::string id;
    std::string title;
    bool isVisible = true;
    std::vector<std::string> buttons;
};

class RibbonEditorOverlay {
public:
    bool isVisible = false;

    // Active Ribbon Structure (Tabs & Sections)
    std::vector<EditorSectionDef> sections = {
        { "sec_history",   "History",        true, { "Undo", "Redo" } },
        { "sec_selection", "Selection",      true, { "Select", "Lasso" } },
        { "sec_tools",     "Drawing Tools",  true, { "Eraser", "Pens & Nibs", "+ Add" } },
        { "sec_input",     "Input Mode",     true, { "Draw with Touch" } },
        { "sec_stencils",  "Stencils",       true, { "Ruler" } },
        { "sec_edit",      "Edit",           true, { "Insert Space" } },
        { "sec_shapes",    "Shapes",         true, { "Shapes Picker", "Automatic Shapes" } },
        { "sec_math",      "Math",           true, { "Ink to Math" } },
        { "sec_mode",      "Mode",           true, { "Full Page View" } }
    };

    // Master Catalog of Global Available Options
    std::vector<EditorCommandDef> globalCatalog = {
        // Clipboard & History
        { "cmd_undo", "Undo", "History", "ribbon_undo_redo", "Undo last stroke (Ctrl+Z)" },
        { "cmd_redo", "Redo", "History", "ribbon_undo_redo", "Redo stroke (Ctrl+Y)" },
        { "cmd_cut", "Cut", "Clipboard", "", "Cut selection to clipboard (Ctrl+X)" },
        { "cmd_copy", "Copy", "Clipboard", "", "Copy selection (Ctrl+C)" },
        { "cmd_paste", "Paste", "Clipboard", "", "Paste from clipboard (Ctrl+V)" },

        // Selection
        { "cmd_select", "Select Pointer", "Selection", "ribbon_select", "Pointer selection & transform" },
        { "cmd_lasso", "Lasso Select", "Selection", "lasso", "Freehand lasso vector selection" },
        
        // Drawing & Inking Tools
        { "cmd_pen", "Pen", "Drawing Tools", "pen", "Ballpoint inking pen" },
        { "cmd_fountain", "Fountain Pen", "Drawing Tools", "pen", "Calligraphy fountain pen" },
        { "cmd_pencil", "Pencil", "Drawing Tools", "pen", "2B graphite pencil with tilt dynamics" },
        { "cmd_brush", "Brush", "Drawing Tools", "pen", "Watercolor paintbrush" },
        { "cmd_highlighter", "Highlighter", "Drawing Tools", "high", "Chisel tip highlighter" },
        { "cmd_laser", "Laser Pointer", "Drawing Tools", "pen", "Laser pointer for presentations" },
        { "cmd_eraser", "Eraser", "Drawing Tools", "eraser", "Stroke and point eraser" },
        { "cmd_ruler", "Ruler", "Drawing Tools", "", "Digital straightedge ruler" },

        // Page & View
        { "cmd_space", "Insert Space", "Page & View", "", "Insert vertical note space" },
        { "cmd_format_bg", "Format Background", "Page & View", "", "Rule lines and paper background" },
        { "cmd_grid", "Grid Lines", "Page & View", "", "Toggle grid rule lines" },
        { "cmd_lined", "Ruled Paper", "Page & View", "", "Toggle ruled paper lines" },
        { "cmd_blank", "Blank Canvas", "Page & View", "", "Clear paper rule lines" },
        { "cmd_zoom_fit", "Fit to Page", "Page & View", "", "Fit canvas to window" },
        { "cmd_zoom_100", "100% Zoom", "Page & View", "", "Reset zoom scale to 100%" },
        { "cmd_full_page", "Full Page View", "Page & View", "", "Toggle distraction-free canvas" },

        // Shapes & Recognition
        { "cmd_shapes", "Shapes Flyout", "Shapes", "", "Geometric shapes menu" },
        { "cmd_auto_shapes", "Auto Shapes", "Shapes", "", "Snap sketches into clean shapes" },
        { "cmd_math", "Ink to Math", "Math", "", "Convert handwriting to math formula" }
    };

    int selectedGlobalCmdIdx = 0;
    int selectedSectionIdx = 0;
    char newCategoryName[64] = "Custom Category";
    char globalFilter[64] = "";

    void Render(const ThemeManager& theme) {
        if (!isVisible) return;

        ImGui::SetNextWindowSize(ImVec2(1100, 720), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 70), ImGuiCond_FirstUseEver);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        OverlayThemeScope overlayScope(theme);

        if (ImGui::Begin("Ribbon Customizer & Layout Editor [F6]##RibbonEditor", &isVisible, flags)) {
            // Header
            ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
            ImGui::TextColored(theme.colorPrimary, "RIBBON CUSTOMIZER & LAYOUT EDITOR");
            ImGui::SameLine(0, 10.0f);
            ImGui::TextColored(theme.colorTextMuted, "| Reorder Sections, Add Buttons & Global Commands");
            ImGui::PopFont();

            ImGui::TextWrapped(
                "Customize the ribbon toolbar: reorder categories, toggle section visibility, add new custom categories, "
                "and populate them with buttons from the global commands catalog."
            );
            ImGui::Separator();
            ImGui::Spacing();

            // =========================================================
            // 1. LIVE RIBBON STRIP PREVIEW
            // =========================================================
            ImGui::TextColored(theme.colorText, "LIVE RIBBON PREVIEW:");
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorShelf);
            ImGui::PushStyleColor(ImGuiCol_Border, theme.colorBorder);

            if (ImGui::BeginChild("##LiveEditorRibbonShelf", ImVec2(0, 110.0f), true, 
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_HorizontalScrollbar)) {
                ImGui::SetCursorPos(ImVec2(14, 8));

                for (size_t sIdx = 0; sIdx < sections.size(); ++sIdx) {
                    const auto& secDef = sections[sIdx];
                    if (!secDef.isVisible) continue;

                    FolioUI::ToolbarSectionBuilder sec(secDef.id.c_str(), secDef.title.c_str(), theme);
                    for (size_t bIdx = 0; bIdx < secDef.buttons.size(); ++bIdx) {
                        const auto& btnName = secDef.buttons[bIdx];
                        std::string btnId = secDef.id + "_btn_" + std::to_string(bIdx);
                        sec.AddLargeButton(btnId.c_str(), 0, btnName.c_str(), btnName.c_str(), false,
                            []() {}, false, ImVec2(52.0f, 54.0f));
                    }
                    sec.Render();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // =========================================================
            // 2. TWO-COLUMN CUSTOMIZATION WORKBENCH
            // =========================================================
            float contentAvailW = ImGui::GetContentRegionAvail().x;
            float centerBtnW = 80.0f;
            float centerGap = 12.0f;
            float colW = (contentAvailW - centerBtnW - 2.0f * centerGap) * 0.5f;

            // --- LEFT COLUMN: GLOBAL AVAILABLE OPTIONS CATALOG ---
            ImGui::BeginChild("##GlobalCatalogCol", ImVec2(colW, 350.0f), true);
            ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
            ImGui::TextColored(theme.colorPrimary, "Global Available Commands & Tools");
            ImGui::PopFont();
            ImGui::TextDisabled("Select a command to add to the active section:");

            ImGui::SetNextItemWidth(colW - 20.0f);
            ImGui::InputTextWithHint("##FilterGlobal", "Filter options...", globalFilter, sizeof(globalFilter));
            ImGui::Spacing();

            if (ImGui::BeginListBox("##GlobalCommandsList", ImVec2(colW - 20.0f, 250.0f))) {
                for (size_t i = 0; i < globalCatalog.size(); ++i) {
                    const auto& cmd = globalCatalog[i];
                    if (globalFilter[0] != '\0') {
                        std::string lowerFilter = globalFilter;
                        std::string lowerName = cmd.name;
                        std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        if (lowerName.find(lowerFilter) == std::string::npos) continue;
                    }

                    std::string label = "[" + cmd.category + "] " + cmd.name;
                    bool isSelected = (selectedGlobalCmdIdx == (int)i);
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        selectedGlobalCmdIdx = (int)i;
                    }
                    if (ImGui::IsItemHovered() && !cmd.tooltip.empty()) {
                        ImGui::SetTooltip("%s", cmd.tooltip.c_str());
                    }
                }
                ImGui::EndListBox();
            }
            ImGui::EndChild();

            ImGui::SameLine(0, centerGap);

            // --- CENTER ACTION BUTTONS ---
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, 120.0f));
            if (ImGui::Button("Add ->", ImVec2(centerBtnW, 34.0f))) {
                if (selectedSectionIdx >= 0 && selectedSectionIdx < (int)sections.size() &&
                    selectedGlobalCmdIdx >= 0 && selectedGlobalCmdIdx < (int)globalCatalog.size()) {
                    sections[selectedSectionIdx].buttons.push_back(globalCatalog[selectedGlobalCmdIdx].name);
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add selected command to active category");

            ImGui::Spacing();
            if (ImGui::Button("<- Remove", ImVec2(centerBtnW, 34.0f))) {
                if (selectedSectionIdx >= 0 && selectedSectionIdx < (int)sections.size()) {
                    auto& btns = sections[selectedSectionIdx].buttons;
                    if (!btns.empty()) btns.pop_back();
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove last button from active category");
            ImGui::EndGroup();

            ImGui::SameLine(0, centerGap);

            // --- RIGHT COLUMN: ACTIVE RIBBON STRUCTURE & REORDERING ---
            ImGui::BeginChild("##ActiveRibbonCol", ImVec2(colW, 350.0f), true);
            ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
            ImGui::TextColored(theme.colorPrimary, "Active Ribbon Categories & Layout");
            ImGui::PopFont();
            ImGui::TextDisabled("Select category to manage buttons & reorder:");

            if (ImGui::BeginListBox("##ActiveSectionsList", ImVec2(colW - 20.0f, 180.0f))) {
                for (size_t i = 0; i < sections.size(); ++i) {
                    auto& sec = sections[i];
                    std::string itemLabel = (sec.isVisible ? "[v] " : "[ ] ") + sec.title + " (" + std::to_string(sec.buttons.size()) + " buttons)";
                    bool isSelected = (selectedSectionIdx == (int)i);
                    if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                        selectedSectionIdx = (int)i;
                    }
                }
                ImGui::EndListBox();
            }

            // Reorder and visibility controls for selected section
            if (selectedSectionIdx >= 0 && selectedSectionIdx < (int)sections.size()) {
                auto& curSec = sections[selectedSectionIdx];
                ImGui::Checkbox("Show this section on ribbon", &curSec.isVisible);
                ImGui::SameLine(0, 15.0f);

                if (ImGui::Button("Move Left", ImVec2(80, 26)) && selectedSectionIdx > 0) {
                    std::swap(sections[selectedSectionIdx], sections[selectedSectionIdx - 1]);
                    selectedSectionIdx--;
                }
                ImGui::SameLine(0, 6.0f);
                if (ImGui::Button("Move Right", ImVec2(80, 26)) && selectedSectionIdx + 1 < (int)sections.size()) {
                    std::swap(sections[selectedSectionIdx], sections[selectedSectionIdx + 1]);
                    selectedSectionIdx++;
                }

                // Show buttons inside selected section
                ImGui::TextDisabled("Buttons: ");
                ImGui::SameLine();
                for (size_t b = 0; b < curSec.buttons.size(); ++b) {
                    ImGui::TextColored(theme.colorPrimary, "%s", curSec.buttons[b].c_str());
                    if (b + 1 < curSec.buttons.size()) {
                        ImGui::SameLine();
                        ImGui::Text("|");
                        ImGui::SameLine();
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Add new category
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputTextWithHint("##NewCatName", "New Category Name", newCategoryName, sizeof(newCategoryName));
            ImGui::SameLine(0, 8.0f);
            if (ImGui::Button("+ Add Category", ImVec2(120, 28))) {
                if (newCategoryName[0] != '\0') {
                    std::string newId = "sec_custom_" + std::to_string(sections.size() + 1);
                    sections.push_back({ newId, newCategoryName, true, {} });
                    selectedSectionIdx = (int)sections.size() - 1;
                }
            }

            ImGui::EndChild();

            // =========================================================
            // 3. FOOTER ACTIONS
            // =========================================================
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Reset to Default Layout", ImVec2(180, 34))) {
                sections = {
                    { "sec_history",   "History",        true, { "Undo", "Redo" } },
                    { "sec_selection", "Selection",      true, { "Select", "Lasso" } },
                    { "sec_tools",     "Drawing Tools",  true, { "Eraser", "Pens & Nibs", "+ Add" } },
                    { "sec_input",     "Input Mode",     true, { "Draw with Touch" } },
                    { "sec_stencils",  "Stencils",       true, { "Ruler" } },
                    { "sec_edit",      "Edit",           true, { "Insert Space" } },
                    { "sec_shapes",    "Shapes",         true, { "Shapes Picker", "Automatic Shapes" } },
                    { "sec_math",      "Math",           true, { "Ink to Math" } },
                    { "sec_mode",      "Mode",           true, { "Full Page View" } }
                };
                selectedSectionIdx = 0;
            }

            float rightX = ImGui::GetWindowWidth() - 130.0f - ImGui::GetStyle().WindowPadding.x;
            if (rightX > ImGui::GetCursorPosX() + 10.0f) {
                ImGui::SameLine(rightX);
            } else {
                ImGui::SameLine(0, 10.0f);
            }

            ImGui::PushStyleColor(ImGuiCol_Button, theme.colorPrimary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(theme.colorPrimary.x * 1.15f, theme.colorPrimary.y * 1.15f, theme.colorPrimary.z * 1.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(theme.colorPrimary.x * 0.85f, theme.colorPrimary.y * 0.85f, theme.colorPrimary.z * 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button("Apply & Close", ImVec2(130, 34))) {
                isVisible = false;
            }
            ImGui::PopStyleColor(4);
        }
        ImGui::End();
    }
};

using ToolbarDemoOverlay = RibbonEditorOverlay;
