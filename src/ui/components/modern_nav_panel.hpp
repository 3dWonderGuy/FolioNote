#pragma once
#include "imgui.h"
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <algorithm>  // std::lerp (C++20) fallback handled below
#include "core/document/document_session.hpp"
#include "core/engine/canvas_engine.hpp"
#include "app/theme_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/icon_manager.hpp"

// ============================================================================
// 1. CONFIGURATION CONSTANTS & METRICS
// ============================================================================
namespace ModernNavConfig {
    // ------------------------------------------------------------------------
    // Column & Panel Width Constraints (Preserved clamping boundaries)
    // ------------------------------------------------------------------------
    constexpr float SECTION_MIN_WIDTH    = 120.0f; // Minimum draggable width of Sections column
    constexpr float SECTION_MAX_WIDTH    = 350.0f; // Maximum draggable width of Sections column
    constexpr float PAGES_MIN_WIDTH      = 140.0f; // Minimum draggable width of Pages column
    constexpr float PANEL_MAX_WIDTH      = 550.0f; // Maximum overall width of the entire navigation panel

    // ------------------------------------------------------------------------
    // Startup Dimensions
    // ------------------------------------------------------------------------
    constexpr float DEFAULT_SECTION_W    = 200.0f; // Initial width of Sections column at startup
    constexpr float DEFAULT_PAGES_W      = 200.0f; // Initial width of Pages column at startup

    // ------------------------------------------------------------------------
    // Interaction Geometry & Proportions (Scaled 1.5x for optimal balance)
    // ------------------------------------------------------------------------
    constexpr float SPLITTER_WIDTH       = 4.0f;   // Visual width of draggable splitters
    constexpr float TOP_BUTTON_HEIGHT    = 40.0f;  // Height of top navigation header buttons (1.5x base)
    constexpr float ACTION_BUTTON_HEIGHT = 34.0f;  // Height of "+ Section" and "+ Add Page" action buttons
    constexpr float ACTION_BUTTON_MARGIN = 14.0f;  // Left & Right side margins inside the column for action buttons
    constexpr float ROW_ITEM_HEIGHT      = 38.0f;  // Height of each notebook, section, and page item row (1.5x base)
    constexpr float FLYOUT_HEIGHT        = 210.0f; // Maximum height of the Notebook selection dropdown flyout
    constexpr float SCROLLBAR_WIDTH      = 15.0f;  // Thickness of hover-aware modern scrollbars
    constexpr float SCROLLBAR_ROUNDING   = 3.0f;   // Corner rounding of modern scrollbar thumbs
    constexpr float CORNER_ROUNDING      = 3.0f;   // Corner rounding applied to cards, buttons, and flyouts
}

// ============================================================================
// 2. ENUMS & STATE TYPES
// ============================================================================
enum class ModernNavState {
    Expanded,     // Full view: Sections column + Pages column + Splitters + Top notebook header
    PagesOnly,    // Collapsed view: Single Pages column with top section indicator + expand button
    Fully_Hidden  // Completely hidden: zero width allocated (canvas gets full window space)
};

// ============================================================================
// 3. MAIN NAVIGATION PANEL COMPONENT
// ============================================================================
class ModernNavPanel {
public:
    ModernNavState state = ModernNavState::Expanded;
    bool showNotebookDropdown = false;

    // Layout dimensions
    float sectionWidth = ModernNavConfig::DEFAULT_SECTION_W;
    float pagesWidth   = ModernNavConfig::DEFAULT_PAGES_W;

    // Hysteresis drag accumulators (prevent sub-pixel jitter during splitter resizing)
    float sectionDragAcc = ModernNavConfig::DEFAULT_SECTION_W;
    float pagesDragAcc   = ModernNavConfig::DEFAULT_PAGES_W;

    // -----------------------------------------------------------------------
    // Smooth Collapse / Expand Animation
    // animatedTotalWidth chases the logical target each frame via exponential
    // smoothing so the sidebar glides in / out instead of snapping.
    // ANIM_SPEED controls responsiveness: higher = faster (12 ≈ ~80 ms settle).
    // -----------------------------------------------------------------------
    float animatedTotalWidth = ModernNavConfig::DEFAULT_SECTION_W * 2.0f +
                               ModernNavConfig::SPLITTER_WIDTH * 2.0f;
    static constexpr float ANIM_SPEED = 14.0f; // frames-per-second feel

    // ------------------------------------------------------------------------
    // Geometry Queries & State Transitions
    // ------------------------------------------------------------------------
    // Returns the *animated* width – used by app.hpp for canvas positioning so
    // the canvas smoothly expands/contracts in lock-step with the sidebar.
    // Pixel-snapped to avoid sub-pixel jitter at the sidebar/canvas seam.
    [[nodiscard]] float GetTotalWidth() const noexcept {
        return std::round(animatedTotalWidth);
    }

    // Returns the logical (target) width the sidebar is heading toward.
    [[nodiscard]] float GetTargetWidth() const noexcept {
        if (state == ModernNavState::Fully_Hidden) return 0.0f;
        if (state == ModernNavState::PagesOnly)    return pagesWidth + ModernNavConfig::SPLITTER_WIDTH;
        return sectionWidth + ModernNavConfig::SPLITTER_WIDTH + pagesWidth + ModernNavConfig::SPLITTER_WIDTH;
    }

    [[nodiscard]] float GetCurrentMinWidth() const noexcept {
        if (state == ModernNavState::PagesOnly) {
            return ModernNavConfig::PAGES_MIN_WIDTH;
        }
        return ModernNavConfig::SECTION_MIN_WIDTH + ModernNavConfig::SPLITTER_WIDTH + ModernNavConfig::PAGES_MIN_WIDTH;
    }

    void ToggleState() noexcept {
        state = (state == ModernNavState::Expanded) ? ModernNavState::PagesOnly : ModernNavState::Expanded;
    }

    // True while the sidebar is mid-transition (animated != target).
    // Used externally to suppress expensive canvas resizes during the glide.
    [[nodiscard]] bool IsAnimating() const noexcept {
        return animatedTotalWidth != GetTargetWidth();
    }

    // Advance the animation one frame. Call once per frame BEFORE Render().
    // Uses exponential smoothing: animatedWidth += (target - current) * (1 - e^(-k*dt))
    // which gives a fast-start, ease-out curve independent of frame rate.
    void TickAnimation() noexcept {
        float dt     = ImGui::GetIO().DeltaTime;
        float target = GetTargetWidth();
        // Clamp dt to avoid huge jump after a hitch / alt-tab
        float k      = 1.0f - std::exp(-ANIM_SPEED * std::min(dt, 0.1f));
        animatedTotalWidth += (target - animatedTotalWidth) * k;
        // Snap to exact target once within half a pixel to stop micro-drift
        if (std::fabs(animatedTotalWidth - target) < 0.5f)
            animatedTotalWidth = target;
    }

    // ------------------------------------------------------------------------
    // Main Panel Lifecycle / Orchestrator
    // ------------------------------------------------------------------------
    void Render(float posX, float posY, float height, DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        // Advance smooth animation each frame
        TickAnimation();

        // Even when Fully_Hidden we still let the animation drain to zero
        if (state == ModernNavState::Fully_Hidden && animatedTotalWidth < 0.5f) return;

        // Pixel-snap the window size so the sidebar always occupies whole pixels.
        // The internal float still interpolates smoothly; we only snap the output.
        float totalW = std::round(animatedTotalWidth);

        ImGui::SetNextWindowPos(ImVec2(posX, posY));
        ImGui::SetNextWindowSize(ImVec2(totalW, height));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | 
                                 ImGuiWindowFlags_NoResize | 
                                 ImGuiWindowFlags_NoMove | 
                                 ImGuiWindowFlags_NoCollapse | 
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushFont(FolioTheme::FontNavLarge ? FolioTheme::FontNavLarge : FolioTheme::FontRegular);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.colorNavBg);
        ImGui::PushStyleColor(ImGuiCol_Separator, theme.colorBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ModernNavConfig::CORNER_ROUNDING);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ModernNavConfig::CORNER_ROUNDING);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

        ImGui::Begin("##NavigationRoot", nullptr, flags);

        auto& ws = session.workspace;
        auto activeNb = ws.GetActiveNotebook();

        if (!activeNb) {
            RenderEmptyState(theme);
            ImGui::End();
            ImGui::PopStyleVar(8);
            ImGui::PopStyleColor(2);
            ImGui::PopFont();
            return;
        }

        // Render the layout that matches the *logical* target state, not the
        // animated width, so content immediately shows the correct column set.
        ModernNavState renderState = state;
        if (state == ModernNavState::Fully_Hidden) {
            // During hide animation use PagesOnly layout so content fades out gracefully
            renderState = ModernNavState::PagesOnly;
        }

        if (renderState == ModernNavState::Expanded) {
            RenderExpandedLayout(totalW, height, session, canvas, theme);
        } else if (renderState == ModernNavState::PagesOnly) {
            RenderPagesOnlyLayout(height, session, canvas, theme);
        }

        ImGui::End();
        ImGui::PopStyleVar(8);
        ImGui::PopStyleColor(2);
        ImGui::PopFont();
    }

private:
    // ========================================================================
    // 4. ATOMIC SUB-COMPONENTS
    // ========================================================================

    bool RenderItemCard(const char* label, bool isSelected, float itemWidth, const ThemeManager& theme, GLuint iconTex = 0) {
        ImGui::SetCursorPosX(5.0f);
        ImVec2 pMin = ImGui::GetCursorScreenPos();
        ImVec2 size(itemWidth, ModernNavConfig::ROW_ITEM_HEIGHT);
        ImVec2 pMax = ImVec2(pMin.x + size.x, pMin.y + size.y);

        bool isClicked = ImGui::InvisibleButton(label, size);
        bool isHovered = ImGui::IsItemHovered();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Draw rounded background pill with theme-driven neutral selection/hover colors
        if (isSelected) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected);
            drawList->AddRectFilled(pMin, pMax, col, ModernNavConfig::CORNER_ROUNDING);
        } else if (isHovered) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, col, ModernNavConfig::CORNER_ROUNDING);
        }

        // 2. Render Icon (if present) & Vertically centered text
        float startX = pMin.x + 8.0f;
        if (iconTex != 0) {
            float iconSize = 22.0f;
            float iconY = pMin.y + (size.y - iconSize) * 0.5f;
            ImVec2 iconMin(startX, iconY);
            ImVec2 iconMax(startX + iconSize, iconY + iconSize);
            drawList->AddImage((ImTextureID)(intptr_t)iconTex, iconMin, iconMax);
            startX += iconSize + 8.0f;
        } else {
            startX += 6.0f; // 14px total left step for items without icon
        }

        float fontH = ImGui::GetFontSize();
        ImVec2 textPos = ImVec2(startX, pMin.y + (size.y - fontH) * 0.5f);
        ImU32 textCol = ImGui::ColorConvertFloat4ToU32(isSelected ? theme.colorItemSelectedText : theme.colorItemText);
        drawList->AddText(textPos, textCol, label);

        // 3. Small gap between items
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        return isClicked;
    }

    bool RenderIconButton(const char* id, GLuint iconTex, float width, float height, const ThemeManager& theme, const ImVec4& bgCol, float iconSize = 18.0f) {
        ImVec2 pMin = ImGui::GetCursorScreenPos();
        ImVec2 size(width, height);
        ImVec2 pMax = ImVec2(pMin.x + size.x, pMin.y + size.y);

        bool isClicked = ImGui::InvisibleButton(id, size);
        bool isHovered = ImGui::IsItemHovered();
        bool isHeld    = ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (isHeld) {
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(bgCol);
            ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, bg, ModernNavConfig::CORNER_ROUNDING);
            drawList->AddRect(pMin, pMax, borderCol, ModernNavConfig::CORNER_ROUNDING, 0, 1.5f);
        } else if (isHovered) {
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, bg, ModernNavConfig::CORNER_ROUNDING);
        } else {
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(bgCol);
            drawList->AddRectFilled(pMin, pMax, bg, ModernNavConfig::CORNER_ROUNDING);
        }

        if (iconTex != 0) {
            ImVec2 iconPos = ImVec2(pMin.x + (size.x - iconSize) * 0.5f, pMin.y + (size.y - iconSize) * 0.5f);
            ImU32 tint = ImGui::ColorConvertFloat4ToU32(theme.colorItemText);
            drawList->AddImage((ImTextureID)(intptr_t)iconTex, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize), ImVec2(0, 0), ImVec2(1, 1), tint);
        }

        return isClicked;
    }

    bool RenderActionButton(const char* label, float width, float height, const ThemeManager& theme, const ImVec4& columnBg, GLuint iconTex = 0, bool alignLeft = false) {
        ImVec2 pMin = ImGui::GetCursorScreenPos();
        ImVec2 size(width, height);
        ImVec2 pMax = ImVec2(pMin.x + size.x, pMin.y + size.y);

        bool isClicked = ImGui::InvisibleButton(label, size);
        bool isHovered = ImGui::IsItemHovered();
        bool isHeld    = ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (isHeld) {
            // When pressed: rest of button matches background color, with thin border matching hover color
            ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(columnBg);
            ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, bgCol, ModernNavConfig::CORNER_ROUNDING);
            drawList->AddRect(pMin, pMax, borderCol, ModernNavConfig::CORNER_ROUNDING, 0, 1.5f);
        } else if (isHovered) {
            // When hovering: fills with hover color
            ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, bgCol, ModernNavConfig::CORNER_ROUNDING);
        } else {
            // Normal resting state: matches column background color
            ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(columnBg);
            drawList->AddRectFilled(pMin, pMax, bgCol, ModernNavConfig::CORNER_ROUNDING);
        }

        float fontH = ImGui::GetFontSize();
        ImU32 textCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemText);

        if (alignLeft) {
            float curX = pMin.x + 8.0f;
            if (iconTex != 0) {
                float iconSize = 22.0f;
                float iconY = pMin.y + (size.y - iconSize) * 0.5f;
                drawList->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(curX, iconY), ImVec2(curX + iconSize, iconY + iconSize));
                curX += iconSize + 8.0f;
            }
            ImVec2 textPos = ImVec2(curX, pMin.y + (size.y - fontH) * 0.5f);
            drawList->AddText(textPos, textCol, label);
        } else {
            ImVec2 textSize = ImGui::CalcTextSize(label);
            ImVec2 textPos = ImVec2(pMin.x + (size.x - textSize.x) * 0.5f, pMin.y + (size.y - textSize.y) * 0.5f);
            drawList->AddText(textPos, textCol, label);
        }

        return isClicked;
    }

    bool RenderCombinedSectionHeaderButton(
        const char* id,
        GLuint arrowTex,
        GLuint secIconTex,
        const char* secName,
        float width,
        float height,
        const ThemeManager& theme,
        const ImVec4& bgCol
    ) {
        ImVec2 pMin = ImGui::GetCursorScreenPos();
        ImVec2 size(width, height);
        ImVec2 pMax = ImVec2(pMin.x + size.x, pMin.y + size.y);

        bool isClicked = ImGui::InvisibleButton(id, size);
        bool isHovered = ImGui::IsItemHovered();
        bool isHeld    = ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Background / Interaction state (blends into background, highlights on hover, thin border on press)
        if (isHeld) {
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(bgCol);
            ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, bg, ModernNavConfig::CORNER_ROUNDING);
            drawList->AddRect(pMin, pMax, borderCol, ModernNavConfig::CORNER_ROUNDING, 0, 1.5f);
        } else if (isHovered) {
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, bg, ModernNavConfig::CORNER_ROUNDING);
        } else {
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(bgCol);
            drawList->AddRectFilled(pMin, pMax, bg, ModernNavConfig::CORNER_ROUNDING);
        }

        float curX = pMin.x + 8.0f;

        // 2. Expand Arrow pointing right (flipped horizontally with UVs: uv0=(1,0), uv1=(0,1))
        if (arrowTex != 0) {
            float arrowSize = 16.0f;
            float arrowY = pMin.y + (size.y - arrowSize) * 0.5f;
            ImVec2 arrowMin(curX, arrowY);
            ImVec2 arrowMax(curX + arrowSize, arrowY + arrowSize);
            ImU32 arrowTint = ImGui::ColorConvertFloat4ToU32(theme.colorItemText);
            // Flip UVs horizontally so the arrow points cleanly to the RIGHT >
            drawList->AddImage((ImTextureID)(intptr_t)arrowTex, arrowMin, arrowMax, ImVec2(1.0f, 0.0f), ImVec2(0.0f, 1.0f), arrowTint);
            curX += arrowSize + 8.0f;
        }

        // 3. Section Color SVG Icon
        if (secIconTex != 0) {
            float iconSize = 22.0f;
            float iconY = pMin.y + (size.y - iconSize) * 0.5f;
            ImVec2 iconMin(curX, iconY);
            ImVec2 iconMax(curX + iconSize, iconY + iconSize);
            drawList->AddImage((ImTextureID)(intptr_t)secIconTex, iconMin, iconMax);
            curX += iconSize + 8.0f;
        }

        // 4. Section Name Text (Vertically centered)
        float fontH = ImGui::GetFontSize();
        ImVec2 textPos = ImVec2(curX, pMin.y + (size.y - fontH) * 0.5f);
        ImU32 textCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemText);
        drawList->AddText(textPos, textCol, secName);

        return isClicked;
    }

    void RenderSplitter(const char* strId, float height, float& dragAcc, float& targetWidth, float minW, float maxW, const ThemeManager& theme) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.colorItemSelected);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colorItemHover);

        ImGui::Button(strId, ImVec2(ModernNavConfig::SPLITTER_WIDTH, height));
        if (ImGui::IsItemActive()) {
            dragAcc += ImGui::GetIO().MouseDelta.x;
            targetWidth = std::clamp(dragAcc, minW, maxW);
        } else {
            dragAcc = targetWidth;
        }

        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        ImGui::PopStyleColor(3);
    }

    void RenderEmptyState(const ThemeManager& theme) {
        ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge);
        ImGui::TextColored(theme.colorTextMuted, "FolioNote");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::SetCursorPos(ImVec2(12.0f, 60.0f));
        ImGui::TextColored(theme.colorTextMuted, "No notebook open.");
    }

    // ========================================================================
    // 5. COMPOSITE SECTIONS & COLUMNS
    // ========================================================================

    void RenderSectionsColumn(float colWidth, float colHeight, DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto& ws = session.workspace;
        auto activeNb = ws.GetActiveNotebook();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorSectionBg);
        ImGui::BeginChild("##ColSections", ImVec2(colWidth, colHeight), false, ImGuiWindowFlags_NoScrollbar);
        
        // Add Section Action Button (Thinner, background-matching resting state)
        float btnMargin = ModernNavConfig::ACTION_BUTTON_MARGIN;
        float btnWidth = std::max(60.0f, colWidth - (btnMargin * 2.0f));
        ImGui::SetCursorPos(ImVec2(btnMargin, 3.0f));
        ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
        if (RenderActionButton("+ Section", btnWidth, ModernNavConfig::ACTION_BUTTON_HEIGHT, theme, theme.colorSectionBg)) {
            activeNb->sections.push_back(std::make_shared<Section>("New Section"));
            activeNb->activeSectionIndex = activeNb->sections.size() - 1;
            canvas.needsFullRebake = true;
            canvas.isDirty = true;
        }
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        // Scrollable Section Items List (Hover-revealed modern scrollbar)
        bool isSectionHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        float sectionGrabAlpha = isSectionHovered ? 0.35f : 0.0f;
        float sectionGrabHoverAlpha = isSectionHovered ? 0.60f : 0.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ModernNavConfig::SCROLLBAR_WIDTH);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, ModernNavConfig::SCROLLBAR_ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(theme.colorTextMuted.x, theme.colorTextMuted.y, theme.colorTextMuted.z, sectionGrabAlpha));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(theme.colorTextMuted.x, theme.colorTextMuted.y, theme.colorTextMuted.z, sectionGrabHoverAlpha));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, theme.colorItemSelected);

        ImGui::BeginChild("##SectionList", ImVec2(0.0f, 0.0f), false, 0);

        for (size_t s = 0; s < activeNb->sections.size(); ++s) {
            auto& sec = activeNb->sections[s];
            if (!sec) continue;
            ImGui::PushID(static_cast<int>(s));
            
            // Load preset color SVG icon assigned to this section
            GLuint iconTex = 0;
            if (!sec->iconFile.empty()) {
                iconTex = g_IconManager.LoadOrGetSVG(sec->iconFile, "assets/icons/Sections_Notebooks/" + sec->iconFile, 64);
            }

            if (RenderItemCard(sec->name.c_str(), activeNb->activeSectionIndex == s, colWidth - 10.0f, theme, iconTex)) {
                activeNb->activeSectionIndex = s;
                sec->activePageIndex = 0;
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            }
            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        ImGui::EndChild();
        ImGui::PopStyleColor(); // colorSectionBg
    }

    void RenderPagesColumn(float colWidth, float colHeight, DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto& ws = session.workspace;
        auto activeNb = ws.GetActiveNotebook();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorPageBg);
        ImGui::BeginChild("##ColPages", ImVec2(colWidth, colHeight), false, ImGuiWindowFlags_NoScrollbar);
        
        // Add Page Action Button (Thinner, background-matching resting state)
        float btnMargin = ModernNavConfig::ACTION_BUTTON_MARGIN;
        float btnWidth = std::max(60.0f, colWidth - (btnMargin * 2.0f));
        ImGui::SetCursorPos(ImVec2(btnMargin, 3.0f));
        ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
        if (RenderActionButton("+ Add Page", btnWidth, ModernNavConfig::ACTION_BUTTON_HEIGHT, theme, theme.colorPageBg)) {
            auto activeSec = activeNb->GetActiveSection();
            if (activeSec) {
                activeSec->pages.push_back(std::make_shared<CanvasPage>("Untitled page"));
                activeSec->activePageIndex = activeSec->pages.size() - 1;
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            }
        }
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        // Scrollable Page Items List (Hover-revealed modern scrollbar)
        bool isPagesHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        float pagesGrabAlpha = isPagesHovered ? 0.35f : 0.0f;
        float pagesGrabHoverAlpha = isPagesHovered ? 0.60f : 0.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ModernNavConfig::SCROLLBAR_WIDTH);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, ModernNavConfig::SCROLLBAR_ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(theme.colorTextMuted.x, theme.colorTextMuted.y, theme.colorTextMuted.z, pagesGrabAlpha));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(theme.colorTextMuted.x, theme.colorTextMuted.y, theme.colorTextMuted.z, pagesGrabHoverAlpha));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, theme.colorItemSelected);

        ImGui::BeginChild("##PageList", ImVec2(0.0f, 0.0f), false, 0);

        auto activeSec = activeNb->GetActiveSection();
        if (activeSec) {
            for (size_t p = 0; p < activeSec->pages.size(); ++p) {
                auto& page = activeSec->pages[p];
                if (!page) continue;
                ImGui::PushID(static_cast<int>(p));
                if (RenderItemCard(page->title.c_str(), activeSec->activePageIndex == p, colWidth - 10.0f, theme)) {
                    activeSec->activePageIndex = p;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        ImGui::EndChild();
        ImGui::PopStyleColor(); // colorPageBg
    }

    // ========================================================================
    // 6. TOP-LEVEL STATE LAYOUTS
    // ========================================================================

    void RenderExpandedLayout(float totalW, float height, DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto& ws = session.workspace;
        auto activeNb = ws.GetActiveNotebook();

        // 1. Full-Width Top Header: [ < (Collapse) ] + [ Notebook Name  v ]
        GLuint arrowLeftTex = g_IconManager.LoadOrGetSVG("arrow_left", "assets/icons/Navigation/arrow-left.svg", 64, true);

        ImGui::SetCursorPos(ImVec2(6.0f, 6.0f));
        if (RenderIconButton("##CollapseNav", arrowLeftTex, ModernNavConfig::TOP_BUTTON_HEIGHT, ModernNavConfig::TOP_BUTTON_HEIGHT, theme, theme.colorNavBg, 18.0f)) {
            ToggleState();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Collapse Sections (Pages Only)");
        }

        ImGui::SameLine(0.0f, 6.0f);
        GLuint activeNbIconTex = 0;
        if (!activeNb->iconFile.empty()) {
            activeNbIconTex = g_IconManager.LoadOrGetSVG(activeNb->iconFile, "assets/icons/Sections_Notebooks/" + activeNb->iconFile, 64);
        }
        std::string nbLabel = activeNb->name + "  v";
        float nbBtnWidth = std::max(60.0f, totalW - ModernNavConfig::TOP_BUTTON_HEIGHT - 18.0f);
        if (RenderActionButton(nbLabel.c_str(), nbBtnWidth, ModernNavConfig::TOP_BUTTON_HEIGHT, theme, theme.colorNavBg, activeNbIconTex, true)) {
            showNotebookDropdown = !showNotebookDropdown;
        }

        // 2. Full-Width Notebook Selection Flyout
        if (showNotebookDropdown) {
            ImGui::SetCursorPosX(6.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colorSectionBg);

            // Hover-aware modern scrollbar for notebook selection
            bool isFlyoutHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
            float flyoutGrabAlpha = isFlyoutHovered ? 0.35f : 0.0f;
            float flyoutGrabHoverAlpha = isFlyoutHovered ? 0.60f : 0.0f;

            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ModernNavConfig::SCROLLBAR_WIDTH);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, ModernNavConfig::SCROLLBAR_ROUNDING);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(theme.colorTextMuted.x, theme.colorTextMuted.y, theme.colorTextMuted.z, flyoutGrabAlpha));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(theme.colorTextMuted.x, theme.colorTextMuted.y, theme.colorTextMuted.z, flyoutGrabHoverAlpha));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, theme.colorItemSelected);

            ImGui::BeginChild("##NbFlyout", ImVec2(totalW - 12.0f, ModernNavConfig::FLYOUT_HEIGHT), true);
            for (size_t i = 0; i < ws.notebooks.size(); ++i) {
                auto& nb = ws.notebooks[i];
                if (!nb) continue;
                ImGui::PushID(static_cast<int>(i));

                GLuint nbIconTex = 0;
                if (!nb->iconFile.empty()) {
                    nbIconTex = g_IconManager.LoadOrGetSVG(nb->iconFile, "assets/icons/Sections_Notebooks/" + nb->iconFile, 64);
                }

                if (RenderItemCard(nb->name.c_str(), ws.activeNotebookIndex == i, totalW - 24.0f, theme, nbIconTex)) {
                    ws.activeNotebookIndex = i;
                    showNotebookDropdown = false;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(2);
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        float contentY = ImGui::GetCursorPosY();
        float columnsHeight = std::max(10.0f, height - contentY);

        // 3. Sections Column
        RenderSectionsColumn(sectionWidth, columnsHeight, session, canvas, theme);

        // 4. Middle Splitter (Sections <-> Pages)
        ImGui::SameLine(0.0f, 0.0f);
        RenderSplitter("##SplitterSections", columnsHeight, sectionDragAcc, sectionWidth,
                       ModernNavConfig::SECTION_MIN_WIDTH, ModernNavConfig::SECTION_MAX_WIDTH, theme);
        ImGui::SameLine(0.0f, 0.0f);

        // 5. Pages Column
        RenderPagesColumn(pagesWidth, columnsHeight, session, canvas, theme);

        // 6. Outer Splitter (Pages <-> Canvas)
        ImGui::SameLine(0.0f, 0.0f);
        float maxPagesAllowed = ModernNavConfig::PANEL_MAX_WIDTH - (sectionWidth + ModernNavConfig::SPLITTER_WIDTH);
        RenderSplitter("##SplitterPages", columnsHeight, pagesDragAcc, pagesWidth,
                       ModernNavConfig::PAGES_MIN_WIDTH, maxPagesAllowed, theme);
    }

    void RenderPagesOnlyLayout(float height, DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        auto& ws = session.workspace;
        auto activeNb = ws.GetActiveNotebook();
        auto activeSec = activeNb ? activeNb->GetActiveSection() : nullptr;

        // 1. Single Blended Header Button: [ > (Expand) (SectionIcon) Section Name ]
        // Arrow pointing right for expanding
        GLuint arrowLeftTex = g_IconManager.LoadOrGetSVG("arrow_left", "assets/icons/Navigation/arrow-left.svg", 64, true);
        GLuint secIconTex = 0;
        if (activeSec && !activeSec->iconFile.empty()) {
            secIconTex = g_IconManager.LoadOrGetSVG(activeSec->iconFile, "assets/icons/Sections_Notebooks/" + activeSec->iconFile, 64, false);
        }

        std::string secName = activeSec ? activeSec->name : "Section";
        float headerBtnWidth = std::max(60.0f, pagesWidth - 12.0f);

        ImGui::SetCursorPos(ImVec2(6.0f, 6.0f));
        if (RenderCombinedSectionHeaderButton("##PagesHeaderBtn", arrowLeftTex, secIconTex, secName.c_str(), headerBtnWidth, ModernNavConfig::TOP_BUTTON_HEIGHT, theme, theme.colorNavBg)) {
            ToggleState();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Expand Sections (Current: %s)", secName.c_str());
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        float contentY = ImGui::GetCursorPosY();
        float columnsHeight = std::max(10.0f, height - contentY);

        // 2. Pages Column
        RenderPagesColumn(pagesWidth, columnsHeight, session, canvas, theme);

        // 3. Outer Splitter (Pages <-> Canvas)
        ImGui::SameLine(0.0f, 0.0f);
        RenderSplitter("##SplitterPages", columnsHeight, pagesDragAcc, pagesWidth,
                       ModernNavConfig::PAGES_MIN_WIDTH, ModernNavConfig::PANEL_MAX_WIDTH, theme);
    }
};