/**
 * =========================================================================================
 * @file ui/overlays/command_palette.cpp
 * @brief Implementation of Spotlight-Style Action Command Palette
 * =========================================================================================
 */

#include "ui/overlays/command_palette.hpp"
#include "app/actions/ui_action_registry.hpp"
#include "ui/imgui_theme.hpp"
#include <algorithm>
#include <cctype>

namespace Folio {

namespace {

/**
 * @brief Recursively flattens an action hierarchy into a flat list of executable actions.
 * @param source Source action tree.
 * @param[out] outFlat Flattened list of leaf/executable actions.
 */
void CollectExecutableActions(const std::vector<UIAction>& source, std::vector<UIAction>& outFlat) {
    for (const auto& act : source) {
        if (!act.IsVisible()) {
            continue;
        }

        if (act.HasSubActions()) {
            CollectExecutableActions(act.subActions, outFlat);
        } else {
            outFlat.push_back(act);
        }
    }
}

} // namespace

void ActionCommandPalette::Open() {
    m_isOpen = true;
    m_focusRequested = true;
    m_searchQuery[0] = '\0';
    m_selectedIndex = 0;
}

void ActionCommandPalette::Close() {
    m_isOpen = false;
    m_searchQuery[0] = '\0';
    m_selectedIndex = 0;
}

void ActionCommandPalette::Toggle() {
    if (m_isOpen) {
        Close();
    } else {
        Open();
    }
}

bool ActionCommandPalette::SubstringMatch(const std::string& str, const std::string& query) {
    if (query.empty()) return true;
    if (str.empty()) return false;

    auto it = std::search(
        str.begin(), str.end(),
        query.begin(), query.end(),
        [](char ch1, char ch2) {
            return std::tolower(static_cast<unsigned char>(ch1)) ==
                   std::tolower(static_cast<unsigned char>(ch2));
        }
    );
    return it != str.end();
}

void ActionCommandPalette::Render(const ThemeManager& themeManager) {
    if (!m_isOpen) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Center the palette in the upper-middle region of the display
    ImVec2 displaySize = io.DisplaySize;
    float paletteW = std::min(580.0f, displaySize.x - 40.0f);
    float paletteH = std::min(420.0f, displaySize.y - 120.0f);
    float posX = (displaySize.x - paletteW) * 0.5f;
    float posY = std::max(60.0f, displaySize.y * 0.15f);

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(paletteW, paletteH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings;

    // Styling Scope
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    ImVec4 bgCol = themeManager.colorBg;
    bgCol.w = 0.96f; // Slight translucent blur feel
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgCol);
    ImGui::PushStyleColor(ImGuiCol_Border, themeManager.colorBorder);

    if (ImGui::Begin("##ActionCommandPalette", &m_isOpen, flags)) {
        // Dismiss on Escape key
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            Close();
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return;
        }

        // =====================================================================
        // 1. TOP SEARCH INPUT BAR
        // =====================================================================
        ImGui::PushFont(FolioTheme::FontBold ? FolioTheme::FontBold : FolioTheme::FontRegular);
        ImGui::TextColored(themeManager.colorPrimary, "🔍 Actions & Commands");
        ImGui::PopFont();
        ImGui::Spacing();

        if (m_focusRequested) {
            ImGui::SetKeyboardFocusHere();
            m_focusRequested = false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, themeManager.colorItemHover);
        ImGui::SetNextItemWidth(-1.0f);

        ImGui::InputTextWithHint("##SearchInput", "Type a command or search action (e.g. Zoom, Paste, Page)...",
                                 m_searchQuery, sizeof(m_searchQuery));

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // =====================================================================
        // 2. QUERY & FILTER ACTIONS
        // =====================================================================
        std::vector<UIAction> allActions = UIActionRegistry::Instance().GetAllActions();
        std::vector<UIAction> executableActions;
        executableActions.reserve(allActions.size() * 2);
        CollectExecutableActions(allActions, executableActions);

        std::string queryStr(m_searchQuery);
        std::vector<UIAction> filtered;
        filtered.reserve(executableActions.size());

        for (const auto& act : executableActions) {
            if (SubstringMatch(act.label, queryStr) ||
                SubstringMatch(act.category, queryStr) ||
                SubstringMatch(act.tooltip, queryStr) ||
                SubstringMatch(act.id, queryStr)) {
                filtered.push_back(act);
            }
        }

        // Clamp selection index
        if (filtered.empty()) {
            m_selectedIndex = 0;
        } else {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                m_selectedIndex = (m_selectedIndex <= 0) ? static_cast<int>(filtered.size()) - 1 : m_selectedIndex - 1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                m_selectedIndex = (m_selectedIndex >= static_cast<int>(filtered.size()) - 1) ? 0 : m_selectedIndex + 1;
            }
            m_selectedIndex = std::clamp(m_selectedIndex, 0, static_cast<int>(filtered.size()) - 1);
        }

        // Execute on Enter key
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !filtered.empty()) {
            const auto& chosen = filtered[m_selectedIndex];
            chosen.Execute();
            Close();
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return;
        }

        // =====================================================================
        // 3. SCROLLABLE RESULTS LIST
        // =====================================================================
        ImGui::BeginChild("##ResultsList", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (filtered.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(themeManager.colorTextMuted, "  No matching actions found for '%s'", m_searchQuery);
        } else {
            for (size_t i = 0; i < filtered.size(); ++i) {
                const auto& act = filtered[i];
                bool isSelected = (static_cast<int>(i) == m_selectedIndex);

                ImGui::PushID(static_cast<int>(i));

                std::string rowLabel = (act.icon.empty() ? "  " : act.icon + " ") + act.label;

                ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                float rowW = ImGui::GetContentRegionAvail().x;
                float rowH = 34.0f;

                ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_None;
                if (!act.IsEnabled()) {
                    selectFlags |= ImGuiSelectableFlags_Disabled;
                }

                if (ImGui::Selectable(rowLabel.c_str(), isSelected, selectFlags, ImVec2(rowW, rowH))) {
                    act.Execute();
                    Close();
                    ImGui::PopID();
                    break;
                }

                // Render Category Badge and Shortcut Hint on the right
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                float rightMargin = cursorPos.x + rowW - 10.0f;

                // Shortcut Hint
                if (!act.shortcut.empty()) {
                    ImVec2 scSize = ImGui::CalcTextSize(act.shortcut.c_str());
                    float scX = rightMargin - scSize.x;
                    float scY = cursorPos.y + (rowH - scSize.y) * 0.5f;
                    drawList->AddText(ImVec2(scX, scY),
                                      ImGui::ColorConvertFloat4ToU32(themeManager.colorTextMuted),
                                      act.shortcut.c_str());
                    rightMargin = scX - 16.0f;
                }

                // Category Badge
                if (!act.category.empty()) {
                    std::string catBadge = "[" + act.category + "]";
                    ImVec2 catSize = ImGui::CalcTextSize(catBadge.c_str());
                    float catX = rightMargin - catSize.x;
                    float catY = cursorPos.y + (rowH - catSize.y) * 0.5f;
                    drawList->AddText(ImVec2(catX, catY),
                                      ImGui::ColorConvertFloat4ToU32(themeManager.colorPrimary),
                                      catBadge.c_str());
                }

                ImGui::PopID();
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace Folio
