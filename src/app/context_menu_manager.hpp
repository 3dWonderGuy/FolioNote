#pragma once
/**
 * =========================================================================================
 * @file app/context_menu_manager.hpp
 * @brief Centralized, Decoupled Context Menu Management & Presentation Service for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * Context menus in FolioNote are an application-level UI service, not an object-level concern.
 * A single unified system coordinates context interactions across disparate domains:
 *
 *   1. Canvas Background:
 *      Paste, Change Paper Style (Submenu), Toggle Grid, Reset Zoom, Reset View, Create Link to Page.
 *
 *   2. Canvas Objects:
 *      Universal transformations (Cut, Copy, Duplicate, Stacking/Layering Submenu, Delete)
 *      plus domain-specific actions (e.g., AttachmentObject: Open, Re-link, Copy Path).
 *
 *   3. Navigation Elements:
 *      Section tabs (Rename, Color tag, Password protection, Delete), Page entries (Duplicate, Move).
 *
 *   4. Toolbar & Ribbon Presets:
 *      Pen style customization, Favorite slot management.
 *
 * KEY PRINCIPLES:
 * - Decoupled from CanvasObject: CanvasObject remains pure vector geometry & rendering without
 *   inheriting or depending on ImGui UI presentation code.
 * - Single Styling Authority: All menus render through ContextMenuThemeScope, ensuring pixel-perfect
 *   theming, rounded corners, icons, and keyboard shortcuts across light and dark modes.
 * - Dynamic Action Dispatch: Actions are encapsulated in std::function<void()> callbacks,
 *   supporting undo/redo history recording and seamless async modal triggering.
 * - Hierarchical Submenus: Automatically detects when an action contains `subActions` and
 *   recursively renders ImGui cascading submenus (`ImGui::BeginMenu` / `ImGui::EndMenu`).
 */

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>

#include <imgui.h>
#include <SDL3/SDL.h>

#include "app/theme_manager.hpp"
#include "app/context_menu_item.hpp"
#include "app/actions/ui_action_registry.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/attachment_container/attachment_container.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/history/canvas_command.hpp"
#include "core/document/document_session.hpp"
#include "core/canvas_engine/canvas_engine.hpp"
#include "ui/imgui_theme.hpp"
#include "core/objects/object_action_registry.hpp"
#include "io/facade/file_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

/**
 * @enum ContextMenuTarget
 * @brief Categorizes the underlying subject of the active right-click context menu.
 */
enum class ContextMenuTarget {
    None,               ///< No menu currently requested
    CanvasBackground,   ///< Right-click on empty infinite canvas paper
    CanvasObject,       ///< Right-click on one or more selected canvas objects
    NavSection,         ///< Right-click on a section tab in the navigation tree
    NavPage,            ///< Right-click on a page item in the navigation tree
    ToolbarPreset,      ///< Right-click on a pen/highlighter/shape preset button
    Custom              ///< Custom UI widget or external plugin
};

/**
 * @class ContextMenuManager
 * @brief Global coordinator and presenter for context menus across the application.
 */
class ContextMenuManager {
public:
    // =========================================================================
    // CONSTRUCTOR
    // =========================================================================
    ContextMenuManager() = default;

    // =========================================================================
    // ACTIVATION APIS
    // =========================================================================

    /**
     * @brief Opens the context menu for empty canvas background clicks.
     *
     * Working Process:
     *   Populates standard canvas operations (Paste, Select All, Reset View, Paper Style submenu, etc.),
     *   stores click world coordinates, and signals ImGui to open the popup.
     *
     * @param worldPos World coordinates in millimeters where the right-click occurred.
     * @param session Active document session reference.
     * @param canvas Canvas engine reference.
     */
    void OpenForBackground(const Point2D& worldPos, DocumentSession& session, CanvasEngine& canvas) {
        m_target = ContextMenuTarget::CanvasBackground;
        m_clickWorldPos = worldPos;
        m_targetObject = nullptr;
        m_items.clear();
        m_headerTitle = "Canvas";
        m_headerIcon = "📐";

        BuildBackgroundMenu(session, canvas);
        m_requestOpen = true;
    }

    /**
     * @brief Opens the context menu for a specific clicked or selected canvas object.
     *
     * Working Process:
     *   Inspects object type tag. For specialized objects (like AttachmentObject),
     *   injects domain actions (Open, Re-link, Copy Path). Then appends standard
     *   universal actions (Duplicate, Layering submenu, Delete).
     *
     * @param obj Shared pointer to the clicked canvas object.
     * @param worldPos Click location in world millimeters.
     * @param session Active document session reference.
     * @param canvas Canvas engine reference.
     */
    void OpenForObject(std::shared_ptr<CanvasObject> obj, const Point2D& worldPos,
                       DocumentSession& session, CanvasEngine& canvas) {
        if (!obj) return;

        m_target = ContextMenuTarget::CanvasObject;
        m_clickWorldPos = worldPos;
        m_targetObject = obj;
        m_items.clear();

        BuildObjectMenu(obj, session, canvas);
        m_requestOpen = true;
    }

    /**
     * @brief Opens a custom context menu with pre-built menu items.
     *
     * @param target Target category.
     * @param title Header title.
     * @param icon Header icon glyph.
     * @param items Pre-configured menu item descriptors.
     */
    void OpenCustom(ContextMenuTarget target, const std::string& title, const std::string& icon,
                    std::vector<ContextMenuItem> items) {
        m_target = target;
        m_targetObject = nullptr;
        m_headerTitle = title;
        m_headerIcon = icon;
        m_items = std::move(items);
        m_requestOpen = true;
    }

    /**
     * @brief Cancels or closes any open context menu.
     */
    void Close() {
        m_target = ContextMenuTarget::None;
        m_targetObject = nullptr;
        m_items.clear();
        m_requestOpen = false;
    }

    // =========================================================================
    // RENDERING & INTERACTION
    // =========================================================================

    /**
     * @brief Recursively renders a hierarchy of action items into ImGui popups and submenus.
     *
     * GENERAL WORKING PROCESS:
     * 1. Evaluates per-item dynamic visibility: skips items where `item.IsVisible() == false`.
     * 2. Renders horizontal separators if `item.isSeparatorBefore` is true.
     * 3. Checks if `item.HasSubActions()`:
     *    - If child actions exist, invokes `ImGui::BeginMenu(label, isEnabled)`.
     *    - Recursively calls `RenderActionList(item.subActions)`.
     *    - Concludes with `ImGui::EndMenu()`.
     * 4. If leaf action, renders `ImGui::MenuItem(label, shortcut, isChecked, isEnabled)`.
     *    - On activation, executes `item.Execute()`.
     *
     * @param items List of actions or sub-actions to render.
     */
    template <typename ActionContainer>
    void RenderActionList(const ActionContainer& items) {
        for (const auto& item : items) {
            if (!item.IsVisible()) {
                continue;
            }

            if (item.isSeparatorBefore) {
                ImGui::Separator();
            }

            // Push destructive red styling if marked
            if (item.isDestructive) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.28f, 0.28f, 1.0f));
            }

            std::string displayLabel = (item.icon.empty() ? "" : item.icon + " ") + item.label;
            const char* shortcutCStr = item.shortcut.empty() ? nullptr : item.shortcut.c_str();

            if (item.HasSubActions()) {
                // Cascading Submenu
                if (ImGui::BeginMenu(displayLabel.c_str(), item.IsEnabled())) {
                    RenderActionList(item.subActions);
                    ImGui::EndMenu();
                }
            } else {
                // Leaf Command Item
                if (ImGui::MenuItem(displayLabel.c_str(), shortcutCStr, item.IsChecked(), item.IsEnabled())) {
                    item.Execute();
                }
            }

            if (item.isDestructive) {
                ImGui::PopStyleColor();
            }
        }
    }

    /**
     * @brief Renders the active context menu using unified ImGui theming.
     *
     * Working Process:
     *   1. If `m_requestOpen` is flagged, triggers `ImGui::OpenPopup(m_popupId)`.
     *   2. Applies `ContextMenuThemeScope` for smooth padding, rounded corners, and consistent dark/light styling.
     *   3. If open, renders header badge (if configured).
     *   4. Delegates to `RenderActionList` to render all items and submenus hierarchically.
     *   5. Resets state upon popup closure.
     *
     * @param themeManager Active application ThemeManager reference.
     */
    void Render(const ThemeManager& themeManager) {
        if (m_requestOpen) {
            ImGui::OpenPopup(m_popupId.c_str());
            m_requestOpen = false;
        }

        ContextMenuThemeScope ctxScope(themeManager);
        if (ImGui::BeginPopup(m_popupId.c_str())) {
            // Optional Header Title
            if (!m_headerTitle.empty()) {
                ImGui::PushFont(FolioTheme::FontNavBoldLarge ? FolioTheme::FontNavBoldLarge : FolioTheme::FontBold);
                std::string headerText = (m_headerIcon.empty() ? "" : m_headerIcon + " ") + m_headerTitle;
                ImGui::TextColored(themeManager.colorPrimary, "%s", headerText.c_str());
                ImGui::PopFont();
                ImGui::Separator();
            }

            // Render all items hierarchically
            RenderActionList(m_items);

            ImGui::EndPopup();
        } else {
            // Popup closed by clicking outside
            if (m_target != ContextMenuTarget::None && !m_requestOpen) {
                m_target = ContextMenuTarget::None;
                m_targetObject = nullptr;
                m_items.clear();
            }
        }
    }

    [[nodiscard]] bool IsActive() const noexcept { return m_target != ContextMenuTarget::None; }
    [[nodiscard]] ContextMenuTarget GetActiveTarget() const noexcept { return m_target; }

private:
    // =========================================================================
    // INTERNAL MENU BUILDERS
    // =========================================================================

    /**
     * @brief Populates menu actions for empty canvas background clicks.
     *
     * WORKING PROCESS & UX FLOW:
     * 1. Detects clipboard availability across both session clipboard (CanvasObjects) and OS text clipboard.
     * 2. Paste action intelligently prioritizes session geometric objects; falls back to creating a TextBox.
     * 3. Provides global selection actions (Select All, Clear Selection).
     * 4. Provides hierarchical submenus for:
     *    - Paper Style (Blank, Ruled, Grid, Dots)
     *    - View Controls (Reset Zoom, 100%, Home Origin)
     *
     * @param session Active document session providing selection, clipboard, and page models.
     * @param canvas Canvas engine providing viewport transformations and rendering state.
     */
    void BuildBackgroundMenu(DocumentSession& session, CanvasEngine& canvas) {
        auto activePg = session.GetActivePage();

        // 1. Paste (handles either copied CanvasObjects or OS clipboard plain text)
        bool canPaste = session.HasClipboardContent() || SDL_HasClipboardText();
        m_items.push_back(ContextMenuItem{
            "Paste", "Ctrl+V", "📋", false, canPaste, false,
            [&session, &canvas, clickPos = m_clickWorldPos]() {
                auto pg = session.GetActivePage();
                if (!pg) return;
                if (session.HasClipboardContent()) {
                    session.PasteObjects(clickPos.x, clickPos.y);
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                } else if (SDL_HasClipboardText()) {
                    char* clipText = SDL_GetClipboardText();
                    if (clipText) {
                        if (clipText[0] != '\0') {
                            auto tb = std::make_shared<Folio::TextBoxObject>();
                            tb->worldX = clickPos.x;
                            tb->worldY = clickPos.y;
                            tb->text = clipText;
                            tb->UpdateBounds();
                            session.AddTextBox(tb);
                            canvas.isDirty = true;
                            canvas.needsFullRebake = true;
                        }
                        SDL_free(clipText);
                    }
                }
            }
        });

        // 2. Select All
        m_items.push_back(ContextMenuItem{
            "Select All", "Ctrl+A", "⬚", false, (activePg && !activePg->objects.empty()), false,
            [&session, &canvas]() {
                auto allObjs = session.QueryVisible(canvas.GetViewport());
                for (auto& obj : allObjs) {
                    if (obj) obj->isSelected = 1;
                }
                canvas.selectionGizmo.SetSelectedObjects(allObjs);
                canvas.isDirty = true;
            }
        });

        // 3. Clear Selection (if selection active)
        if (canvas.selectionGizmo.HasSelection()) {
            m_items.push_back(ContextMenuItem{
                "Clear Selection", "Esc", "✕", false, true, false,
                [&canvas]() {
                    canvas.selectionGizmo.ClearSelection();
                    canvas.isDirty = true;
                }
            });
        }

        // 4. Submenu: Paper Style
        if (activePg) {
            ContextMenuItem paperSubmenu;
            paperSubmenu.label = "Paper Style";
            paperSubmenu.icon = "📝";
            paperSubmenu.isSeparatorBefore = true;

            const struct { const char* label; PaperStyle style; } styles[] = {
                { "Blank / Plain", PaperStyle::Blank },
                { "Ruled / Lined", PaperStyle::Lined },
                { "Grid / Graph", PaperStyle::Grid },
                { "Dotted", PaperStyle::Dotted },
                { "Cornell Notes", PaperStyle::Cornell }
            };

            for (const auto& s : styles) {
                ContextMenuItem styleItem;
                styleItem.label = s.label;
                styleItem.isSticky = (activePg->paperStyle == s.style);
                styleItem.onTrigger = [activePg, &canvas, st = s.style]() {
                    activePg->paperStyle = st;
                    canvas.currentPaperStyle = st;
                    canvas.isDirty = true;
                    canvas.needsFullRebake = true;
                };
                paperSubmenu.AddSubAction(std::move(styleItem));
            }
            m_items.push_back(std::move(paperSubmenu));
        }

        // 5. Submenu: View Controls
        {
            ContextMenuItem viewSubmenu;
            viewSubmenu.label = "View";
            viewSubmenu.icon = "👁";
            viewSubmenu.isSeparatorBefore = false;

            ContextMenuItem resetZoom;
            resetZoom.label = "Reset Zoom (100%)";
            resetZoom.shortcut = "Ctrl+0";
            resetZoom.icon = "🔍";
            resetZoom.onTrigger = [&canvas]() {
                canvas.transform.zoom = 1.0;
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            };
            viewSubmenu.AddSubAction(std::move(resetZoom));

            ContextMenuItem resetOrigin;
            resetOrigin.label = "Reset View to Origin";
            resetOrigin.shortcut = "Home";
            resetOrigin.icon = "⌂";
            resetOrigin.onTrigger = [&canvas]() {
                canvas.transform.panXMm = 0.0;
                canvas.transform.panYMm = 0.0;
                canvas.transform.zoom = 1.0;
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            };
            viewSubmenu.AddSubAction(std::move(resetOrigin));

            m_items.push_back(std::move(viewSubmenu));
        }
    }

    /**
     * @brief Populates menu actions for a clicked or selected canvas object.
     *
     * WORKING PROCESS & ARCHITECTURAL DECOUPLING:
     * 1. Extracts contextual metadata (display name, type glyph) for header display.
     * 2. Configures a pure CanvasActionContext providing UI-agnostic functors for:
     *    - Bring to Front / Send to Back (via CanvasPage stacking manipulation or Order submenu)
     *    - Object deletion with undo history recording
     *    - Object duplication
     *    - Attachment file replacement dialogs
     * 3. Dispatches BuildActionsForObject() to ObjectActionRegistry.
     *
     * @param obj Target CanvasObject clicked or selected.
     * @param session Active document session providing document operations and command history.
     * @param canvas Canvas engine providing spatial coordinates and rendering state.
     */
    void BuildObjectMenu(std::shared_ptr<CanvasObject> obj, DocumentSession& session, CanvasEngine& canvas) {
        if (!obj) return;
        auto activePg = session.GetActivePage();

        if (obj->type == ObjectType::AttachmentFile) {
            auto attach = std::dynamic_pointer_cast<AttachmentObject>(obj);
            if (attach) {
                m_headerTitle = attach->displayName;
                m_headerIcon = attach->isEmbedded ? "📦" : "📎";
            }
        } else {
            m_headerTitle = "Selection";
            m_headerIcon = "⬚";
        }

        // Delegate to ObjectActionRegistry in core/objects:
        // Automatically provides default actions (Delete, Order Submenu, Duplicate)
        // and queries obj->CustomizeActions(actions) for domain-specific commands.
        std::vector<ContextMenuItem> actions = ObjectActionRegistry::BuildActionsForObject(
            obj, activePg, canvas, session
        );

        for (auto& act : actions) {
            m_items.push_back(std::move(act));
        }
    }

    // =========================================================================
    // STATE FIELDS
    // =========================================================================
    ContextMenuTarget m_target = ContextMenuTarget::None;
    Point2D m_clickWorldPos{0.0, 0.0};
    std::shared_ptr<CanvasObject> m_targetObject = nullptr;
    std::string m_headerTitle = "";
    std::string m_headerIcon = "";
    std::vector<ContextMenuItem> m_items;
    bool m_requestOpen = false;
    const std::string m_popupId = "##GlobalFolioContextMenu";
};

} // namespace Folio
