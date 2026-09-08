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
#include "app/app_view_mode.hpp"

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

    // Rename modal state
    std::string renameTargetGuid;
    int renameTargetType = 0; // 1 = Section, 2 = Group, 3 = Page
    char renameBuffer[256] = "";
    bool openRenamePopup = false;

    // Clipboard state
    std::shared_ptr<CanvasPage> copiedPage = nullptr;
    std::shared_ptr<Section> copiedSection = nullptr;

    // Password modal state
    std::string passwordTargetSecGuid;
    int passwordModalMode = 0; // 1 = Set Password, 2 = Unlock Section
    char passwordBuffer[128] = "";
    bool openPasswordModal = false;
    bool passwordError = false;

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
    void Render(float posX, float posY, float height, DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme, AppViewMode* pViewMode = nullptr) {
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
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse |
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
            RenderExpandedLayout(totalW, height, session, canvas, theme, pViewMode);
        } else if (renderState == ModernNavState::PagesOnly) {
            RenderPagesOnlyLayout(height, session, canvas, theme);
        }

        RenderRenameModal(activeNb, canvas);
        RenderPasswordModal(activeNb, canvas);

        ImGui::End();
        ImGui::PopStyleVar(8);
        ImGui::PopStyleColor(2);
        ImGui::PopFont();
    }

private:
    // ========================================================================
    // 4. ATOMIC SUB-COMPONENTS
    // ========================================================================

    void RenderRenameModal(const std::shared_ptr<Notebook>& activeNb, CanvasEngine& canvas) {
        if (openRenamePopup) {
            ImGui::OpenPopup("RenameModal##Nav");
            openRenamePopup = false;
        }
        if (ImGui::BeginPopupModal("RenameModal##Nav", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* targetTypeName = (renameTargetType == 1 ? "Section" : (renameTargetType == 2 ? "Section Group" : "Page"));
            ImGui::Text("Rename %s:", targetTypeName);
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            bool enterPressed = ImGui::InputText("##RenameInput", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)) || enterPressed) {
                if (strlen(renameBuffer) > 0 && activeNb) {
                    if (renameTargetType == 1) {
                        if (auto s = activeNb->FindSectionByGuid(renameTargetGuid)) {
                            s->name = renameBuffer;
                        }
                    } else if (renameTargetType == 2) {
                        if (auto g = activeNb->FindSectionGroupByGuid(renameTargetGuid)) {
                            g->name = renameBuffer;
                        }
                    } else if (renameTargetType == 3) {
                        auto sec = activeNb->GetActiveSection();
                        if (sec) {
                            if (auto p = sec->FindPageByGuid(renameTargetGuid)) {
                                p->title = renameBuffer;
                            }
                        }
                    }
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void RenderPasswordModal(const std::shared_ptr<Notebook>& activeNb, CanvasEngine& canvas) {
        if (openPasswordModal) {
            ImGui::OpenPopup("PasswordModal##Nav");
            openPasswordModal = false;
        }
        if (ImGui::BeginPopupModal("PasswordModal##Nav", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            auto sec = activeNb ? activeNb->FindSectionByGuid(passwordTargetSecGuid) : nullptr;
            if (!sec) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            if (passwordModalMode == 1) { // Set Password
                ImGui::Text("Set Password for: %s", sec->name.c_str());
                ImGui::TextColored(ImVec4(0.85f, 0.45f, 0.20f, 1.0f), "Warning: Protected sections require this password to unlock.");
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::IsWindowAppearing()) {
                    ImGui::SetKeyboardFocusHere();
                }
                bool enterPressed = ImGui::InputText("##NewSecPassword", passwordBuffer, sizeof(passwordBuffer), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                if (ImGui::Button("Set Password", ImVec2(120.0f, 0.0f)) || enterPressed) {
                    if (strlen(passwordBuffer) > 0) {
                        sec->isPasswordProtected = true;
                        sec->password = passwordBuffer;
                        sec->isLocked = false;
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(0.0f, 10.0f);
                if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }
            } else if (passwordModalMode == 2) { // Unlock Section
                ImGui::Text("Unlock Section: %s", sec->name.c_str());
                if (passwordError) {
                    ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.25f, 1.0f), "Incorrect password. Please try again.");
                }
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::IsWindowAppearing()) {
                    ImGui::SetKeyboardFocusHere();
                }
                bool enterPressed = ImGui::InputText("##UnlockSecPassword", passwordBuffer, sizeof(passwordBuffer), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                if (ImGui::Button("Unlock", ImVec2(100.0f, 0.0f)) || enterPressed) {
                    if (std::string(passwordBuffer) == sec->password) {
                        sec->isLocked = false;
                        activeNb->SetActiveSection(sec);
                        sec->activePageIndex = 0;
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                        ImGui::CloseCurrentPopup();
                    } else {
                        passwordError = true;
                    }
                }
                ImGui::SameLine(0.0f, 10.0f);
                if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    bool RenderHierarchyItemCard(
        const char* strId,
        const char* label,
        bool isSelected,
        float itemWidth,
        const ThemeManager& theme,
        GLuint iconTex = 0,
        float indentX = 0.0f,
        bool showChevron = false,
        bool isCollapsed = false,
        bool isFolder = false,
        bool* outChevronClicked = nullptr,
        bool isLocked = false
    ) {
        ImGui::SetCursorPosX(5.0f + indentX);
        ImVec2 pMin = ImGui::GetCursorScreenPos();
        ImVec2 size(itemWidth, ModernNavConfig::ROW_ITEM_HEIGHT);
        ImVec2 pMax = ImVec2(pMin.x + size.x, pMin.y + size.y);

        bool isClicked = ImGui::InvisibleButton(strId, size);
        bool isHovered = ImGui::IsItemHovered();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Hierarchy connector line
        if (indentX > 0.0f) {
            ImU32 guideCol = ImGui::ColorConvertFloat4ToU32(ImVec4(theme.colorTextMuted.x, theme.colorTextMuted.y, theme.colorTextMuted.z, 0.45f));
            float branchX = pMin.x - 8.0f;
            float branchMidY = pMin.y + size.y * 0.5f;
            drawList->AddLine(ImVec2(branchX, pMin.y - 3.0f), ImVec2(branchX, branchMidY), guideCol, 1.2f);
            drawList->AddLine(ImVec2(branchX, branchMidY), ImVec2(pMin.x - 1.0f, branchMidY), guideCol, 1.2f);
        }

        // Rounded selection/hover pill
        if (isSelected) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected);
            drawList->AddRectFilled(pMin, pMax, col, ModernNavConfig::CORNER_ROUNDING);
        } else if (isHovered) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(theme.colorItemHover);
            drawList->AddRectFilled(pMin, pMax, col, ModernNavConfig::CORNER_ROUNDING);
        }

        float curX = pMin.x + 6.0f;

        // Folding chevron
        if (showChevron) {
            float chevMidX = curX + 6.0f;
            float chevMidY = pMin.y + size.y * 0.5f;
            ImU32 chevCol = ImGui::ColorConvertFloat4ToU32(theme.colorItemText);

            if (isCollapsed) {
                // Collapsed: right-pointing '>'
                drawList->AddLine(ImVec2(chevMidX - 2.5f, chevMidY - 4.0f), ImVec2(chevMidX + 2.5f, chevMidY), chevCol, 1.5f);
                drawList->AddLine(ImVec2(chevMidX + 2.5f, chevMidY), ImVec2(chevMidX - 2.5f, chevMidY + 4.0f), chevCol, 1.5f);
            } else {
                // Expanded: down-pointing 'v'
                drawList->AddLine(ImVec2(chevMidX - 4.0f, chevMidY - 2.0f), ImVec2(chevMidX, chevMidY + 2.5f), chevCol, 1.5f);
                drawList->AddLine(ImVec2(chevMidX, chevMidY + 2.5f), ImVec2(chevMidX + 4.0f, chevMidY - 2.0f), chevCol, 1.5f);
            }

            if (outChevronClicked && isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                if (mousePos.x <= curX + 16.0f) {
                    *outChevronClicked = true;
                }
            }
            curX += 16.0f;
        }

        // Lock indicator
        if (isLocked) {
            float lockMidY = pMin.y + size.y * 0.5f;
            ImU32 lockCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.88f, 0.45f, 0.20f, 1.0f));
            drawList->AddRect(ImVec2(curX + 2.0f, lockMidY - 7.0f), ImVec2(curX + 10.0f, lockMidY - 1.0f), lockCol, 3.0f, 0, 1.5f);
            drawList->AddRectFilled(ImVec2(curX, lockMidY - 2.0f), ImVec2(curX + 12.0f, lockMidY + 6.0f), lockCol, 2.0f);
            curX += 17.0f;
        }

        // Folder icon or SVG icon
        if (isFolder) {
            float fMidY = pMin.y + size.y * 0.5f;
            ImU32 folderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.92f, 0.72f, 0.28f, 1.0f));
            ImU32 folderTab = ImGui::ColorConvertFloat4ToU32(ImVec4(0.80f, 0.60f, 0.20f, 1.0f));
            drawList->AddRectFilled(ImVec2(curX, fMidY - 7.0f), ImVec2(curX + 6.0f, fMidY - 3.0f), folderTab, 1.0f);
            drawList->AddRectFilled(ImVec2(curX, fMidY - 4.0f), ImVec2(curX + 17.0f, fMidY + 6.0f), folderCol, 2.0f);
            drawList->AddRect(ImVec2(curX, fMidY - 4.0f), ImVec2(curX + 17.0f, fMidY + 6.0f), folderTab, 2.0f, 0, 1.0f);
            curX += 23.0f;
        } else if (iconTex != 0) {
            float iconSize = 20.0f;
            float iconY = pMin.y + (size.y - iconSize) * 0.5f;
            drawList->AddImage((ImTextureID)(intptr_t)iconTex, ImVec2(curX, iconY), ImVec2(curX + iconSize, iconY + iconSize));
            curX += iconSize + 6.0f;
        } else {
            curX += 4.0f;
        }

        // Title text with clipping
        float fontH = ImGui::GetFontSize();
        ImVec2 textPos(curX, pMin.y + (size.y - fontH) * 0.5f);
        ImU32 textCol = ImGui::ColorConvertFloat4ToU32(isSelected ? theme.colorItemSelectedText : theme.colorItemText);
        
        drawList->PushClipRect(pMin, ImVec2(pMax.x - 4.0f, pMax.y), true);
        drawList->AddText(textPos, textCol, label);
        drawList->PopClipRect();

        // Advance cursor for next item without creating an intervening widget
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);

        return isClicked;
    }

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
        GLuint logoTex = g_IconManager.LoadOrGetSVG("app_logo", "assets/icons/logo.svg", 128);
        if (logoTex != 0) {
            ImGui::Image((ImTextureID)(intptr_t)logoTex, ImVec2(32.0f, 32.0f));
            ImGui::SameLine(0, 10.0f);
        }
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
        ImGui::BeginChild("##ColSections", ImVec2(colWidth, colHeight), false, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        // Action buttons: [+ Section] and [+ Group]
        float btnMargin = ModernNavConfig::ACTION_BUTTON_MARGIN;
        float totalAvailW = std::max(60.0f, colWidth - (btnMargin * 2.0f));
        float gap = 4.0f;
        float btnSecW = std::floor((totalAvailW - gap) * 0.58f);
        float btnGrpW = totalAvailW - gap - btnSecW;

        ImGui::SetCursorPos(ImVec2(btnMargin, 3.0f));
        ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
        if (RenderActionButton("+ Section", btnSecW, ModernNavConfig::ACTION_BUTTON_HEIGHT, theme, theme.colorSectionBg)) {
            auto newSec = std::make_shared<Section>("New Section");
            activeNb->AddSection(newSec);
            activeNb->SetActiveSection(newSec);
            canvas.needsFullRebake = true;
            canvas.isDirty = true;
        }
        ImGui::SameLine(0.0f, gap);
        if (RenderActionButton("+ Group", btnGrpW, ModernNavConfig::ACTION_BUTTON_HEIGHT, theme, theme.colorSectionBg)) {
            auto newGrp = std::make_shared<SectionGroup>("New Group");
            auto newSec = std::make_shared<Section>("New Section");
            newGrp->AddSection(newSec);
            activeNb->AddSectionGroup(newGrp);
            activeNb->SetActiveSection(newSec);
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

        auto activeSec = activeNb ? activeNb->GetActiveSection() : nullptr;

        static const struct ColorOption { const char* name; const char* svg; } colorOptions[] = {
            {"Blue", "blue-section-simple.svg"},
            {"Green", "green-section-simple.svg"},
            {"Magenta", "magenta-section-simple.svg"},
            {"Orange", "orange-section-simple.svg"},
            {"Pink", "pink-section-simple.svg"},
            {"Red", "red-section-simple.svg"},
            {"Salad Green", "saladgreen-section-simple.svg"},
            {"Sky Blue", "skyblue-section-simple.svg"},
            {"Yellow", "yellow-section-simple.svg"}
        };

        // -----------------------------------------------------------------
        // 1. SECTION GROUPS
        // -----------------------------------------------------------------
        for (size_t g = 0; g < activeNb->sectionGroups.size(); ++g) {
            auto& group = activeNb->sectionGroups[g];
            if (!group) continue;
            ImGui::PushID(static_cast<int>(1000 + g));

            bool containsActive = false;
            if (activeSec) {
                for (const auto& s : group->sections) {
                    if (s && s->guid == activeSec->guid) { containsActive = true; break; }
                }
            }

            std::string grpCardId = "##GrpCard_" + group->guid;
            bool chevronClicked = false;
            bool grpClicked = RenderHierarchyItemCard(
                grpCardId.c_str(), group->name.c_str(), 
                containsActive && group->isCollapsed, 
                colWidth - 10.0f, theme, 0, 0.0f, true, group->isCollapsed, true, &chevronClicked
            );

            if (grpClicked) {
                group->isCollapsed = !group->isCollapsed;
            }

            // Double Click to Rename Group
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                renameTargetType = 2;
                renameTargetGuid = group->guid;
                strncpy(renameBuffer, group->name.c_str(), sizeof(renameBuffer) - 1);
                renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                openRenamePopup = true;
            }

            // Drag Source for Section Group
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("NAV_GROUP_DND", &g, sizeof(size_t));
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
                ImGui::Text("Moving Group: %s", group->name.c_str());
                ImGui::PopStyleColor();
                ImGui::EndDragDropSource();
            }

            // Drag Target on Group Header (receives section or group)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pSec = ImGui::AcceptDragDropPayload("NAV_SECTION_DND")) {
                    std::string draggedGuid((const char*)pSec->Data);
                    activeNb->MoveSectionToGroup(draggedGuid, group->guid);
                    group->isCollapsed = false;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                if (const ImGuiPayload* pGrp = ImGui::AcceptDragDropPayload("NAV_GROUP_DND")) {
                    size_t srcG = *(const size_t*)pGrp->Data;
                    ImVec2 itemMin = ImGui::GetItemRectMin();
                    ImVec2 itemMax = ImGui::GetItemRectMax();
                    ImVec2 mousePos = ImGui::GetMousePos();
                    bool insertBefore = (mousePos.y < itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
                    size_t targetG = insertBefore ? g : g + 1;
                    if (srcG < targetG) targetG--;
                    activeNb->MoveSectionGroup(srcG, targetG);
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(itemMin, itemMax, ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected), ModernNavConfig::CORNER_ROUNDING, 0, 2.0f);
                ImGui::EndDragDropTarget();
            }

            // Context menu on group
            if (ImGui::BeginPopupContextItem(("##GroupCtx_" + group->guid).c_str())) {
                if (ImGui::MenuItem("New Section in Group")) {
                    auto newSec = std::make_shared<Section>("New Section");
                    group->AddSection(newSec);
                    activeNb->SetActiveSection(newSec);
                    group->isCollapsed = false;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                if (ImGui::MenuItem("Rename Group")) {
                    renameTargetType = 2;
                    renameTargetGuid = group->guid;
                    strncpy(renameBuffer, group->name.c_str(), sizeof(renameBuffer) - 1);
                    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                    openRenamePopup = true;
                }
                if (ImGui::MenuItem("Delete Group")) {
                    activeNb->sectionGroups.erase(activeNb->sectionGroups.begin() + g);
                    if (containsActive) {
                        auto fallback = activeNb->GetActiveSection();
                        activeNb->SetActiveSection(fallback);
                    }
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Move Up", nullptr, false, g > 0)) {
                    activeNb->MoveSectionGroup(g, g - 1);
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                if (ImGui::MenuItem("Move Down", nullptr, false, g + 1 < activeNb->sectionGroups.size())) {
                    activeNb->MoveSectionGroup(g, g + 1);
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                ImGui::EndPopup();
            }

            // Render nested child sections when not collapsed
            if (!group->isCollapsed) {
                for (size_t cs = 0; cs < group->sections.size(); ++cs) {
                    auto& sec = group->sections[cs];
                    if (!sec) continue;
                    ImGui::PushID(static_cast<int>(cs));

                    bool isSecActive = (activeSec && sec->guid == activeSec->guid);
                    GLuint iconTex = 0;
                    if (!sec->iconFile.empty()) {
                        iconTex = g_IconManager.LoadOrGetSVG(sec->iconFile, "assets/icons/Sections_Notebooks/" + sec->iconFile, 64);
                    }
                    float indentX = 14.0f;
                    float secW = std::max(60.0f, colWidth - 10.0f - indentX);
                    std::string secId = "##GrpSec_" + sec->guid;

                    if (RenderHierarchyItemCard(secId.c_str(), sec->name.c_str(), isSecActive, secW, theme, iconTex, indentX, false, false, false, nullptr, sec->isPasswordProtected && sec->isLocked)) {
                        if (sec->isPasswordProtected && sec->isLocked) {
                            passwordTargetSecGuid = sec->guid;
                            passwordModalMode = 2; // Unlock
                            memset(passwordBuffer, 0, sizeof(passwordBuffer));
                            passwordError = false;
                            openPasswordModal = true;
                        } else {
                            activeNb->SetActiveSection(sec);
                            sec->activePageIndex = 0;
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                        }
                    }

                    // Double Click to Rename Section
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        renameTargetType = 1;
                        renameTargetGuid = sec->guid;
                        strncpy(renameBuffer, sec->name.c_str(), sizeof(renameBuffer) - 1);
                        renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                        openRenamePopup = true;
                    }

                    // Drag source for child section
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("NAV_SECTION_DND", sec->guid.c_str(), sec->guid.size() + 1);
                        ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
                        ImGui::Text("Moving: %s", sec->name.c_str());
                        ImGui::PopStyleColor();
                        ImGui::EndDragDropSource();
                    }

                    // Drag target on child section
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* pSec = ImGui::AcceptDragDropPayload("NAV_SECTION_DND")) {
                            std::string draggedGuid((const char*)pSec->Data);
                            ImVec2 itemMin = ImGui::GetItemRectMin();
                            ImVec2 itemMax = ImGui::GetItemRectMax();
                            ImVec2 mousePos = ImGui::GetMousePos();
                            bool insertBefore = (mousePos.y < itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
                            size_t targetIdx = insertBefore ? cs : cs + 1;

                            auto it = std::find_if(group->sections.begin(), group->sections.end(), [&](const std::shared_ptr<Section>& s) {
                                return s && s->guid == draggedGuid;
                            });
                            if (it != group->sections.end()) {
                                size_t srcIdx = std::distance(group->sections.begin(), it);
                                if (srcIdx < targetIdx) targetIdx--;
                                auto moved = *it;
                                group->sections.erase(it);
                                if (targetIdx <= group->sections.size()) {
                                    group->sections.insert(group->sections.begin() + targetIdx, moved);
                                } else {
                                    group->sections.push_back(moved);
                                }
                            } else {
                                activeNb->MoveSectionToGroup(draggedGuid, group->guid);
                            }
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                        }
                        ImVec2 itemMin = ImGui::GetItemRectMin();
                        ImVec2 itemMax = ImGui::GetItemRectMax();
                        ImVec2 mousePos = ImGui::GetMousePos();
                        bool insertBefore = (mousePos.y < itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
                        float lineY = insertBefore ? itemMin.y : itemMax.y;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 col = ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected);
                        dl->AddLine(ImVec2(itemMin.x, lineY), ImVec2(itemMin.x + secW, lineY), col, 2.5f);
                        dl->AddCircleFilled(ImVec2(itemMin.x, lineY), 3.0f, col);
                        ImGui::EndDragDropTarget();
                    }

                    // Context menu on child section
                    if (ImGui::BeginPopupContextItem(("##SecCtx_" + sec->guid).c_str())) {
                        if (ImGui::MenuItem("Rename Section")) {
                            renameTargetType = 1;
                            renameTargetGuid = sec->guid;
                            strncpy(renameBuffer, sec->name.c_str(), sizeof(renameBuffer) - 1);
                            renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                            openRenamePopup = true;
                        }
                        if (group->sections.size() > 1 || !activeNb->sections.empty()) {
                            if (ImGui::MenuItem("Delete Section")) {
                                group->sections.erase(group->sections.begin() + cs);
                                if (isSecActive) {
                                    activeNb->SetActiveSection(activeNb->GetActiveSection());
                                }
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                                ImGui::EndPopup();
                                ImGui::PopID();
                                break;
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Move Up", nullptr, false, cs > 0)) {
                            auto moved = group->sections[cs];
                            group->sections.erase(group->sections.begin() + cs);
                            group->sections.insert(group->sections.begin() + cs - 1, moved);
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                        }
                        if (ImGui::MenuItem("Move Down", nullptr, false, cs + 1 < group->sections.size())) {
                            auto moved = group->sections[cs];
                            group->sections.erase(group->sections.begin() + cs);
                            group->sections.insert(group->sections.begin() + cs + 1, moved);
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                        }
                        if (ImGui::MenuItem("Move to Root Sections")) {
                            activeNb->MoveSectionToRoot(sec->guid);
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Copy Section")) {
                            copiedSection = sec->Clone();
                        }
                        if (ImGui::MenuItem("Paste Section Below", nullptr, false, copiedSection != nullptr)) {
                            if (copiedSection) {
                                auto pasteSec = copiedSection->Clone();
                                pasteSec->groupGuid = group->guid;
                                group->sections.insert(group->sections.begin() + cs + 1, pasteSec);
                                activeNb->SetActiveSection(pasteSec);
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                            }
                        }
                        if (ImGui::MenuItem("Copy Section Link")) {
                            std::string linkText = "[" + sec->name + "](folionote://section/" + sec->guid + ")";
                            SDL_SetClipboardText(linkText.c_str());
                        }
                        ImGui::Separator();
                        if (ImGui::BeginMenu("Section Color")) {
                            for (const auto& opt : colorOptions) {
                                bool isCurrent = (sec->iconFile == opt.svg);
                                if (ImGui::MenuItem(opt.name, nullptr, isCurrent)) {
                                    sec->iconFile = opt.svg;
                                }
                            }
                            ImGui::EndMenu();
                        }
                        if (!sec->isPasswordProtected) {
                            if (ImGui::MenuItem("Password Protect...")) {
                                passwordTargetSecGuid = sec->guid;
                                passwordModalMode = 1;
                                memset(passwordBuffer, 0, sizeof(passwordBuffer));
                                passwordError = false;
                                openPasswordModal = true;
                            }
                        } else {
                            if (sec->isLocked) {
                                if (ImGui::MenuItem("Unlock Section...")) {
                                    passwordTargetSecGuid = sec->guid;
                                    passwordModalMode = 2;
                                    memset(passwordBuffer, 0, sizeof(passwordBuffer));
                                    passwordError = false;
                                    openPasswordModal = true;
                                }
                            } else {
                                if (ImGui::MenuItem("Lock Section Now")) {
                                    sec->isLocked = true;
                                }
                            }
                            if (ImGui::MenuItem("Remove Password Protection")) {
                                sec->isPasswordProtected = false;
                                sec->password.clear();
                                sec->isLocked = false;
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
        }

        // -----------------------------------------------------------------
        // 2. ROOT SECTIONS
        // -----------------------------------------------------------------
        if (!activeNb->sectionGroups.empty() && !activeNb->sections.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
        }

        for (size_t s = 0; s < activeNb->sections.size(); ++s) {
            auto& sec = activeNb->sections[s];
            if (!sec) continue;
            ImGui::PushID(static_cast<int>(s));
            
            GLuint iconTex = 0;
            if (!sec->iconFile.empty()) {
                iconTex = g_IconManager.LoadOrGetSVG(sec->iconFile, "assets/icons/Sections_Notebooks/" + sec->iconFile, 64);
            }

            bool isSecActive = (activeSec && sec->guid == activeSec->guid);
            std::string secId = "##RootSec_" + sec->guid;
            if (RenderHierarchyItemCard(secId.c_str(), sec->name.c_str(), isSecActive, colWidth - 10.0f, theme, iconTex, 0.0f, false, false, false, nullptr, sec->isPasswordProtected && sec->isLocked)) {
                if (sec->isPasswordProtected && sec->isLocked) {
                    passwordTargetSecGuid = sec->guid;
                    passwordModalMode = 2;
                    memset(passwordBuffer, 0, sizeof(passwordBuffer));
                    passwordError = false;
                    openPasswordModal = true;
                } else {
                    activeNb->SetActiveSection(sec);
                    sec->activePageIndex = 0;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
            }

            // Double Click to Rename Section
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                renameTargetType = 1;
                renameTargetGuid = sec->guid;
                strncpy(renameBuffer, sec->name.c_str(), sizeof(renameBuffer) - 1);
                renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                openRenamePopup = true;
            }

            // Drag source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("NAV_SECTION_DND", sec->guid.c_str(), sec->guid.size() + 1);
                ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
                ImGui::Text("Moving: %s", sec->name.c_str());
                ImGui::PopStyleColor();
                ImGui::EndDragDropSource();
            }

            // Drag target
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pSec = ImGui::AcceptDragDropPayload("NAV_SECTION_DND")) {
                    std::string draggedGuid((const char*)pSec->Data);
                    ImVec2 itemMin = ImGui::GetItemRectMin();
                    ImVec2 itemMax = ImGui::GetItemRectMax();
                    ImVec2 mousePos = ImGui::GetMousePos();
                    bool insertBefore = (mousePos.y < itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
                    size_t targetIdx = insertBefore ? s : s + 1;

                    bool isAlreadyRoot = false;
                    size_t srcIdx = 0;
                    for (size_t i = 0; i < activeNb->sections.size(); ++i) {
                        if (activeNb->sections[i] && activeNb->sections[i]->guid == draggedGuid) {
                            isAlreadyRoot = true;
                            srcIdx = i;
                            break;
                        }
                    }
                    if (isAlreadyRoot) {
                        if (srcIdx < targetIdx) targetIdx--;
                        activeNb->MoveSection(srcIdx, targetIdx);
                    } else {
                        activeNb->MoveSectionToRoot(draggedGuid, targetIdx);
                    }
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                ImVec2 mousePos = ImGui::GetMousePos();
                bool insertBefore = (mousePos.y < itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
                float lineY = insertBefore ? itemMin.y : itemMax.y;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 col = ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected);
                dl->AddLine(ImVec2(itemMin.x, lineY), ImVec2(itemMin.x + colWidth - 10.0f, lineY), col, 2.5f);
                dl->AddCircleFilled(ImVec2(itemMin.x, lineY), 3.0f, col);
                ImGui::EndDragDropTarget();
            }

            // Context menu
            if (ImGui::BeginPopupContextItem(("##RootSecCtx_" + sec->guid).c_str())) {
                if (ImGui::MenuItem("Rename Section")) {
                    renameTargetType = 1;
                    renameTargetGuid = sec->guid;
                    strncpy(renameBuffer, sec->name.c_str(), sizeof(renameBuffer) - 1);
                    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                    openRenamePopup = true;
                }
                if (activeNb->sections.size() > 1 || !activeNb->sectionGroups.empty()) {
                    if (ImGui::MenuItem("Delete Section")) {
                        activeNb->RemoveSection(sec->guid);
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Move Up", nullptr, false, s > 0)) {
                    activeNb->MoveSection(s, s - 1);
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                if (ImGui::MenuItem("Move Down", nullptr, false, s + 1 < activeNb->sections.size())) {
                    activeNb->MoveSection(s, s + 1);
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                if (!activeNb->sectionGroups.empty()) {
                    if (ImGui::BeginMenu("Move to Group")) {
                        for (auto& grp : activeNb->sectionGroups) {
                            if (!grp) continue;
                            if (ImGui::MenuItem(grp->name.c_str())) {
                                activeNb->MoveSectionToGroup(sec->guid, grp->guid);
                                grp->isCollapsed = false;
                                canvas.needsFullRebake = true;
                                canvas.isDirty = true;
                                break;
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy Section")) {
                    copiedSection = sec->Clone();
                }
                if (ImGui::MenuItem("Paste Section Below", nullptr, false, copiedSection != nullptr)) {
                    if (copiedSection) {
                        auto pasteSec = copiedSection->Clone();
                        activeNb->sections.insert(activeNb->sections.begin() + s + 1, pasteSec);
                        activeNb->SetActiveSection(pasteSec);
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                }
                if (ImGui::MenuItem("Copy Section Link")) {
                    std::string linkText = "[" + sec->name + "](folionote://section/" + sec->guid + ")";
                    SDL_SetClipboardText(linkText.c_str());
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Section Color")) {
                    for (const auto& opt : colorOptions) {
                        bool isCurrent = (sec->iconFile == opt.svg);
                        if (ImGui::MenuItem(opt.name, nullptr, isCurrent)) {
                            sec->iconFile = opt.svg;
                        }
                    }
                    ImGui::EndMenu();
                }
                if (!sec->isPasswordProtected) {
                    if (ImGui::MenuItem("Password Protect...")) {
                        passwordTargetSecGuid = sec->guid;
                        passwordModalMode = 1;
                        memset(passwordBuffer, 0, sizeof(passwordBuffer));
                        passwordError = false;
                        openPasswordModal = true;
                    }
                } else {
                    if (sec->isLocked) {
                        if (ImGui::MenuItem("Unlock Section...")) {
                            passwordTargetSecGuid = sec->guid;
                            passwordModalMode = 2;
                            memset(passwordBuffer, 0, sizeof(passwordBuffer));
                            passwordError = false;
                            openPasswordModal = true;
                        }
                    } else {
                        if (ImGui::MenuItem("Lock Section Now")) {
                            sec->isLocked = true;
                        }
                    }
                    if (ImGui::MenuItem("Remove Password Protection")) {
                        sec->isPasswordProtected = false;
                        sec->password.clear();
                        sec->isLocked = false;
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        // Empty bottom area drop target (moves to root)
        ImGui::Dummy(ImVec2(colWidth - 10.0f, 32.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pSec = ImGui::AcceptDragDropPayload("NAV_SECTION_DND")) {
                std::string draggedGuid((const char*)pSec->Data);
                activeNb->MoveSectionToRoot(draggedGuid);
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            }
            ImGui::EndDragDropTarget();
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
        ImGui::BeginChild("##ColPages", ImVec2(colWidth, colHeight), false, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        // Add Page Action Button (Thinner, background-matching resting state)
        float btnMargin = ModernNavConfig::ACTION_BUTTON_MARGIN;
        float btnWidth = std::max(60.0f, colWidth - (btnMargin * 2.0f));
        ImGui::SetCursorPos(ImVec2(btnMargin, 3.0f));
        ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
        if (RenderActionButton("+ Add Page", btnWidth, ModernNavConfig::ACTION_BUTTON_HEIGHT, theme, theme.colorPageBg)) {
            auto activeSec = activeNb ? activeNb->GetActiveSection() : nullptr;
            if (activeSec) {
                auto newPage = std::make_shared<CanvasPage>("New Untitled");
                newPage->nestingLevel = 0;
                activeSec->pages.push_back(newPage);
                activeSec->activePageIndex = activeSec->pages.size() - 1;
                canvas.ApplyDefaultTemplate();
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

        auto activeSec = activeNb ? activeNb->GetActiveSection() : nullptr;
        if (activeSec) {
            int collapsedLevel = -1;
            for (size_t p = 0; p < activeSec->pages.size(); ++p) {
                auto& page = activeSec->pages[p];
                if (!page) continue;

                // Hierarchy folding check: skip descendant pages of a collapsed parent
                if (collapsedLevel >= 0) {
                    if (page->nestingLevel > collapsedLevel) {
                        continue;
                    } else {
                        collapsedLevel = -1;
                    }
                }
                if (page->isCollapsed) {
                    collapsedLevel = page->nestingLevel;
                }

                // Check if page has child subpages right below it
                bool hasChildren = (p + 1 < activeSec->pages.size() && activeSec->pages[p + 1]->nestingLevel > page->nestingLevel);

                float indentX = std::clamp(page->nestingLevel, 0, 2) * 16.0f;
                float itemW = std::max(60.0f, colWidth - 10.0f - indentX);

                ImGui::PushID(static_cast<int>(p));
                bool chevronClicked = false;
                bool isSelected = (activeSec->activePageIndex == p);
                std::string pageId = "##PageItem_" + page->guid;

                if (RenderHierarchyItemCard(pageId.c_str(), page->title.c_str(), isSelected, itemW, theme, 0, indentX, hasChildren, page->isCollapsed, false, &chevronClicked)) {
                    if (chevronClicked) {
                        page->isCollapsed = !page->isCollapsed;
                    } else {
                        activeSec->activePageIndex = p;
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                }

                // Double Click to Rename Page
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renameTargetType = 3;
                    renameTargetGuid = page->guid;
                    strncpy(renameBuffer, page->title.c_str(), sizeof(renameBuffer) - 1);
                    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                    openRenamePopup = true;
                }

                // Drag Source (Click-hold and drag to reorder)
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("NAV_PAGE_DND", &p, sizeof(size_t));
                    ImGui::PushStyleColor(ImGuiCol_Text, theme.colorText);
                    ImGui::Text("Moving: %s", page->title.c_str());
                    ImGui::PopStyleColor();
                    ImGui::EndDragDropSource();
                }

                // Drag Target (Drop above or below this page)
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NAV_PAGE_DND")) {
                        size_t srcIdx = *(const size_t*)payload->Data;
                        ImVec2 itemMin = ImGui::GetItemRectMin();
                        ImVec2 itemMax = ImGui::GetItemRectMax();
                        ImVec2 mousePos = ImGui::GetMousePos();
                        bool insertBefore = (mousePos.y < itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
                        size_t targetIdx = insertBefore ? p : p + 1;
                        if (srcIdx < targetIdx) targetIdx--;
                        activeSec->MovePage(srcIdx, targetIdx);
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                    ImVec2 itemMin = ImGui::GetItemRectMin();
                    ImVec2 itemMax = ImGui::GetItemRectMax();
                    ImVec2 mousePos = ImGui::GetMousePos();
                    bool insertBefore = (mousePos.y < itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
                    float lineY = insertBefore ? itemMin.y : itemMax.y;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 col = ImGui::ColorConvertFloat4ToU32(theme.colorItemSelected);
                    dl->AddLine(ImVec2(itemMin.x, lineY), ImVec2(itemMin.x + itemW, lineY), col, 2.5f);
                    dl->AddCircleFilled(ImVec2(itemMin.x, lineY), 3.0f, col);
                    ImGui::EndDragDropTarget();
                }

                // Context Menu on Page Item
                if (ImGui::BeginPopupContextItem(("##PageCtx_" + page->guid).c_str())) {
                    if (ImGui::MenuItem("Rename")) {
                        renameTargetType = 3;
                        renameTargetGuid = page->guid;
                        strncpy(renameBuffer, page->title.c_str(), sizeof(renameBuffer) - 1);
                        renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                        openRenamePopup = true;
                    }
                    if (activeSec->pages.size() > 1) {
                        if (ImGui::MenuItem("Delete Page")) {
                            activeSec->RemovePage(page->guid);
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                            ImGui::EndPopup();
                            ImGui::PopID();
                            break;
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Move Up", nullptr, false, p > 0)) {
                        activeSec->MovePage(p, p - 1);
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                    if (ImGui::MenuItem("Move Down", nullptr, false, p + 1 < activeSec->pages.size())) {
                        activeSec->MovePage(p, p + 1);
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                    if (ImGui::MenuItem("Make Subpage", nullptr, false, page->nestingLevel < 2)) {
                        activeSec->DemotePage(p);
                    }
                    if (ImGui::MenuItem("Promote Page", nullptr, false, page->nestingLevel > 0)) {
                        activeSec->PromotePage(p);
                    }
                    if (ImGui::MenuItem("Add Subpage Below")) {
                        auto newSub = std::make_shared<CanvasPage>("New Untitled");
                        newSub->nestingLevel = std::min(2, page->nestingLevel + 1);
                        activeSec->pages.insert(activeSec->pages.begin() + p + 1, newSub);
                        activeSec->activePageIndex = p + 1;
                        canvas.ApplyDefaultTemplate();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy Page")) {
                        copiedPage = page->Clone();
                    }
                    if (ImGui::MenuItem("Paste Page Below", nullptr, false, copiedPage != nullptr)) {
                        if (copiedPage) {
                            auto pastePg = copiedPage->Clone();
                            activeSec->pages.insert(activeSec->pages.begin() + p + 1, pastePg);
                            activeSec->activePageIndex = p + 1;
                            canvas.needsFullRebake = true;
                            canvas.isDirty = true;
                        }
                    }
                    if (ImGui::MenuItem("Copy Page Link")) {
                        std::string linkText = "[" + page->title + "](folionote://page/" + page->guid + ")";
                        SDL_SetClipboardText(linkText.c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            // Drop target on empty space below page list
            ImGui::Dummy(ImVec2(colWidth - 10.0f, 32.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NAV_PAGE_DND")) {
                    size_t srcIdx = *(const size_t*)payload->Data;
                    if (activeSec->pages.size() > 1 && srcIdx < activeSec->pages.size() - 1) {
                        activeSec->MovePage(srcIdx, activeSec->pages.size() - 1);
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                }
                ImGui::EndDragDropTarget();
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

    void RenderExpandedLayout(float totalW, float height, DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme, AppViewMode* pViewMode = nullptr) {
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