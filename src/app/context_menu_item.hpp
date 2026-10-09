#pragma once
/**
 * =========================================================================================
 * @file app/context_menu_item.hpp
 * @brief Unified Context Menu and Canvas Object Action Item Descriptor Model
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INHERITANCE:
 * -----------------------------------
 * ContextMenuItem publicly inherits from the decoupled, universal `UIAction` descriptor
 * declared in `app/actions/ui_action.hpp`.
 *
 * This provides 100% backwards compatibility with all existing object customization hooks
 * (`CanvasObject::CustomizeActions`, `ObjectActionRegistry`) and matches forward declarations
 * (`struct ContextMenuItem;`) across the codebase, while empowering every menu item with:
 *   - Nested hierarchical submenus (`subActions`)
 *   - Machine-readable IDs (`id`) and categorization (`category`)
 *   - Dynamic per-frame state evaluators (`isVisibleFn`, `isEnabledFn`, `isCheckedFn`)
 *   - Universal execution closures
 */

#include "app/actions/ui_action.hpp"

namespace Folio {

/**
 * @struct ContextMenuItem
 * @brief Context menu item descriptor derived from UIAction for backward compatibility.
 */
struct ContextMenuItem : public UIAction {
    using UIAction::UIAction;

    ContextMenuItem() = default;
    ContextMenuItem(UIAction act) : UIAction(std::move(act)) {}
};

// Semantic type aliases for backwards compatibility and clarity across domains
using CanvasAction = ContextMenuItem;
using CanvasObjectAction = ContextMenuItem;

} // namespace Folio
