#pragma once
/**
 * =========================================================================================
 * @file app/actions/ui_action.hpp
 * @brief Universal, Backend-Agnostic UI Action & Command Model for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * --------------------------------
 * UIAction is the fundamental command and interaction descriptor in FolioNote. It completely
 * decouples command definition and execution from UI presentation:
 *
 *   1. Zero UI Engine Dependency:
 *      Uses pure standard C++20 (<string>, <functional>, <vector>, <cstdint>).
 *      Contains NO ImGui, OpenGL, or platform headers.
 *      This allows core engines, headless unit tests, and future UI frontends (such as
 *      ThorSVG, custom vector renderers, or a standalone ImGui UI builder) to define,
 *      inspect, and dispatch actions without any graphics backend coupling.
 *
 *   2. Unified Interaction Model:
 *      A single UIAction can simultaneously represent:
 *        - A button in the Ribbon bar or toolbar
 *        - An item in a right-click context menu
 *        - A row in a Command Palette / Actions search modal
 *        - A global keyboard accelerator / shortcut
 *        - A nested flyout submenu (via `subActions`)
 *
 *   3. Hierarchical Composition (Submenus):
 *      Actions can contain arbitrary levels of `subActions`. UI renderers recursively
 *      traverse this tree to produce cascading context menus, dropdown flyouts, or tool palettes.
 *
 *   4. Dynamic State Predicates:
 *      In addition to static flags (`isEnabled`, `isSticky`), actions support dynamic
 *      lambdas (`isEnabledFn`, `isCheckedFn`, `isVisibleFn`) evaluated per-frame to
 *      accurately reflect active application state.
 */

#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include <utility>

namespace Folio {

/**
 * @struct UIAction
 * @brief Universal model representing a user-invocable action, tool, or menu item.
 */
struct UIAction {
    // -------------------------------------------------------------------------
    // Identification & Categorization
    // -------------------------------------------------------------------------
    std::string id = "";                 ///< Machine-readable identifier (e.g. "canvas.zoom_in", "layer.front")
    std::string category = "";           ///< Domain category (e.g. "Canvas", "Edit", "Layer", "View")
    std::string label = "";              ///< Primary human-readable display text (e.g. "Bring to Front")
    std::string tooltip = "";            ///< Detailed tooltip or accessibility description
    std::string shortcut = "";           ///< Keyboard shortcut string hint (e.g. "Ctrl+D", "Del")

    // -------------------------------------------------------------------------
    // Visual Presentation & Icons
    // -------------------------------------------------------------------------
    std::string icon = "";               ///< Typographic UTF-8 glyph or emoji fallback (e.g. "📄", "🗑")
    std::string iconKey = "";            ///< High-DPI Vector SVG asset key resolved via IconManager
    uint64_t iconTexture = 0;            ///< Optional raw GPU texture handle (0 = fallback to iconKey / icon)
    int32_t order = 100;                 ///< Visual ordering weight: lower values appear first (ascending)
    bool isSeparatorBefore = false;      ///< True if a visual divider line precedes this item
    bool isDestructive = false;          ///< True if action mutates or removes data (styled in danger accent)

    // -------------------------------------------------------------------------
    // Static State Flags
    // -------------------------------------------------------------------------
    bool isEnabled = true;               ///< Static interactivity flag (true = clickable, false = greyed out)
    bool isSticky = false;               ///< Static active/selected toggle indicator (true = checked)

    // -------------------------------------------------------------------------
    // Dynamic State Evaluators (Optional Lambdas)
    // -------------------------------------------------------------------------
    std::function<bool()> isVisibleFn = nullptr;  ///< Dynamic predicate: returns false to hide item completely
    std::function<bool()> isEnabledFn = nullptr;  ///< Dynamic predicate: returns false to disable/grey out item
    std::function<bool()> isCheckedFn = nullptr;  ///< Dynamic predicate: returns true if active/checked

    // -------------------------------------------------------------------------
    // Command Execution
    // -------------------------------------------------------------------------
    std::function<void()> onTrigger = nullptr;    ///< Execution closure invoked upon activation

    // -------------------------------------------------------------------------
    // Hierarchical Submenus & Flyouts
    // -------------------------------------------------------------------------
    std::vector<UIAction> subActions;             ///< Child actions forming a nested submenu or flyout

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    UIAction() = default;

    /**
     * @brief Full parameterized constructor.
     */
    UIAction(
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
        uint64_t inIconTexture = 0,
        std::string inId = "",
        std::string inCategory = ""
    ) : id(std::move(inId)),
        category(std::move(inCategory)),
        label(std::move(inLabel)),
        shortcut(std::move(inShortcut)),
        icon(std::move(inIcon)),
        iconKey(std::move(inIconKey)),
        iconTexture(inIconTexture),
        order(inOrder),
        isSeparatorBefore(inSepBefore),
        isDestructive(inDestructive),
        isEnabled(inEnabled),
        isSticky(inSticky),
        onTrigger(std::move(inTrigger)) {}

    // =========================================================================
    // STATE RESOLUTION HELPERS
    // =========================================================================

    /**
     * @brief Determines whether the action should be presented to the user.
     * @return true if visible; false if hidden.
     */
    [[nodiscard]] bool IsVisible() const {
        if (isVisibleFn) {
            return isVisibleFn();
        }
        return true;
    }

    /**
     * @brief Determines whether the action is interactive.
     * @return true if enabled; false if disabled / greyed out.
     */
    [[nodiscard]] bool IsEnabled() const {
        if (isEnabledFn) {
            return isEnabledFn();
        }
        return isEnabled;
    }

    /**
     * @brief Determines whether the toggle / checkmark indicator is active.
     * @return true if checked/selected; false otherwise.
     */
    [[nodiscard]] bool IsChecked() const {
        if (isCheckedFn) {
            return isCheckedFn();
        }
        return isSticky;
    }

    /**
     * @brief Checks if this action defines child submenu items.
     * @return true if child actions are present.
     */
    [[nodiscard]] bool HasSubActions() const noexcept {
        return !subActions.empty();
    }

    /**
     * @brief Executes the action if an execution closure is registered and it is enabled.
     */
    void Execute() const {
        if (IsEnabled() && onTrigger) {
            onTrigger();
        }
    }

    // =========================================================================
    // FLUENT BUILDER METHODS
    // =========================================================================

    UIAction& SetId(std::string inId) { id = std::move(inId); return *this; }
    UIAction& SetCategory(std::string inCat) { category = std::move(inCat); return *this; }
    UIAction& SetTooltip(std::string inTooltip) { tooltip = std::move(inTooltip); return *this; }
    UIAction& SetShortcut(std::string inShortcut) { shortcut = std::move(inShortcut); return *this; }
    UIAction& SetIcon(std::string inIcon) { icon = std::move(inIcon); return *this; }
    UIAction& SetIconKey(std::string inIconKey) { iconKey = std::move(inIconKey); return *this; }
    UIAction& SetOrder(int32_t inOrder) { order = inOrder; return *this; }
    UIAction& SetSeparatorBefore(bool sep) { isSeparatorBefore = sep; return *this; }
    UIAction& SetDestructive(bool dest) { isDestructive = dest; return *this; }
    UIAction& SetEnabled(bool enabled) { isEnabled = enabled; return *this; }
    UIAction& SetSticky(bool sticky) { isSticky = sticky; return *this; }
    UIAction& SetOnTrigger(std::function<void()> trigger) { onTrigger = std::move(trigger); return *this; }
    UIAction& SetVisibleFn(std::function<bool()> fn) { isVisibleFn = std::move(fn); return *this; }
    UIAction& SetEnabledFn(std::function<bool()> fn) { isEnabledFn = std::move(fn); return *this; }
    UIAction& SetCheckedFn(std::function<bool()> fn) { isCheckedFn = std::move(fn); return *this; }

    /**
     * @brief Appends a child action, turning this item into a parent submenu.
     * @param child Child action descriptor.
     * @return Reference to self for chaining.
     */
    UIAction& AddSubAction(UIAction child) {
        subActions.push_back(std::move(child));
        return *this;
    }
};

} // namespace Folio
