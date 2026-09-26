#pragma once
/**
 * =========================================================================================
 * @file app/context_menu_item.hpp
 * @brief Unified Context Menu and Canvas Object Action Item Descriptor Model
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * --------------------------------
 * This header defines the universal action and menu item model (`ContextMenuItem`) used
 * across both the core document/canvas object system and the application context menu UI.
 *
 * It decouples action declaration from UI rendering:
 *   - Core objects (CanvasObject and subclasses) and registries (ObjectActionRegistry) declare
 *     executable commands, visual labels, icons, and display weights using pure standard C++
 *     types (`std::string`, `std::function`, `cstdint`).
 *   - The UI presentation layer (`ContextMenuManager`) ingests these descriptors and renders
 *     them inside styled ImGui popups with active theming and keyboard shortcuts.
 *
 * BUTTON ORDERING & WEIGHT HIERARCHY (`order`):
 * ---------------------------------------------
 * Items are deterministically sorted by their explicit `order` weight (ascending):
 *   - 0   ..  99 : Domain Primary actions (Delete = 0, Open = 10, Edit = 20, Play = 30)
 *   - 100 .. 199 : Domain Secondary actions (Re-link = 110, Copy Path = 120, Explorer = 130)
 *   - 200 .. 299 : Layering & Z-Order (Bring to Front = 200, Forward = 210, Backward = 220, Back = 230)
 *   - 300 .. 399 : Layout & Duplication (Duplicate = 300, Lock Aspect = 310, Align = 320)
 *   - 900 .. 999 : Extensions & Plugin Additions
 *
 * ICON RESOLUTION HIERARCHY:
 * --------------------------
 * Icons are flexibly resolved by UI renderers through a multi-tier fallback:
 *   1. `iconKey`: Vector SVG asset key (e.g. "trash", "open", "duplicate") resolved via IconManager.
 *   2. `iconTexture`: Pre-baked GPU texture ID (ImTextureID / GLuint) for dynamic live previews.
 *   3. `icon`: Typographic UTF-8 glyph or emoji fallback (e.g. "📄", "📎", "🗑").
 */

#include <string>
#include <functional>
#include <cstdint>
#include <utility>

namespace Folio {

/**
 * @struct ContextMenuItem
 * @brief Lightweight model representing a single clickable command row in a context menu,
 * toolbar, or command palette.
 *
 * MATHEMATICAL & LOGICAL WORKING PROCESS:
 * - Deterministic Sort: Menus sort items via `std::stable_sort` on `order` to maintain
 *   insertion order among equal weights while enforcing hierarchical layout.
 * - Non-blocking Trigger: `onTrigger` stores an executable closure (`std::function<void()>`)
 *   capturing document session, page, or object handles to dispatch operations safely.
 */
struct ContextMenuItem {
    std::string label;                   ///< Primary display text (e.g. "Delete", "Duplicate")
    std::string shortcut = "";           ///< Optional keyboard shortcut hint (e.g. "Del", "Ctrl+D")
    std::string icon = "";               ///< Typographic UTF-8 glyph or emoji fallback (e.g. "📄", "🗑")
    std::string iconKey = "";            ///< High-DPI Vector SVG asset key resolved via IconManager
    uint64_t iconTexture = 0;            ///< Optional raw GPU texture handle (0 = fallback to iconKey / icon)
    int32_t order = 100;                 ///< Display ordering weight: lower values appear first
    bool isSeparatorBefore = false;      ///< When true, draws a divider line immediately above this item
    bool isEnabled = true;               ///< When false, item is displayed greyed-out and unclickable
    bool isSticky = false;               ///< When true, rendered with active toggle / checkmark indicator
    bool isDestructive = false;          ///< When true, rendered with red/danger accent (e.g. Delete)
    std::function<void()> onTrigger;     ///< Functor invoked upon item click or activation

    ContextMenuItem() = default;

    /**
     * @brief Parameterized constructor for concise in-place item configuration.
     *
     * @param inLabel Display text for the menu entry.
     * @param inShortcut Key combo string hint.
     * @param inIcon UTF-8 emoji or glyph symbol.
     * @param inSepBefore True if a horizontal line separator precedes this item.
     * @param inEnabled True if interactive, false if greyed out.
     * @param inDestructive True if command mutates/deletes state (styled red).
     * @param inTrigger Execution closure.
     * @param inOrder Visual sort priority weight (default: 100).
     * @param inSticky True if active selection checkbox applies.
     * @param inIconKey Vector asset name for high-DPI rendering.
     * @param inIconTexture Direct GPU texture descriptor handle.
     */
    ContextMenuItem(
        std::string inLabel,
        std::string inShortcut = "",
        std::string inIcon = "",
        bool inSepBefore = false,
        bool inEnabled = true,
        bool inDestructive = false,
        std::function<void()> inTrigger = nullptr,
        int32_t inOrder = 100,
        bool inSticky = false,
        std::string inIconKey = "",
        uint64_t inIconTexture = 0
    ) : label(std::move(inLabel)),
        shortcut(std::move(inShortcut)),
        icon(std::move(inIcon)),
        iconKey(std::move(inIconKey)),
        iconTexture(inIconTexture),
        order(inOrder),
        isSeparatorBefore(inSepBefore),
        isEnabled(inEnabled),
        isSticky(inSticky),
        isDestructive(inDestructive),
        onTrigger(std::move(inTrigger)) {}
};

// Semantic type aliases for backwards compatibility and clarity across domains
using CanvasAction = ContextMenuItem;
using CanvasObjectAction = ContextMenuItem;

} // namespace Folio
