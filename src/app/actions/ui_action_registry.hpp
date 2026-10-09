#pragma once
/**
 * =========================================================================================
 * @file app/actions/ui_action_registry.hpp
 * @brief Central Registry, Discovery, and Dispatch Service for UI Actions
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INVERSION OF CONTROL:
 * --------------------------------------------
 * UIActionRegistry serves as the single source of truth for command discovery, shortcut
 * acceleration, and contextual action resolution across FolioNote.
 *
 * It decouples command producers (CanvasEngine, DocumentSession, InputManager) from command
 * consumers (RibbonBar, ContextMenuManager, Flyout menus, Command Palette, or future UI builder).
 *
 * WORKING PROCESS:
 * 1. Startup Registration: Subsystems register their capabilities as `UIAction` descriptors
 *    storing closures that mutate engine state.
 * 2. Discovery & Introspection: Menus, toolbars, and search palettes query actions by category,
 *    tag, or context provider.
 * 3. Execution & Dispatch: Given an action ID or keyboard shortcut chord, the registry validates
 *    predicates (`IsVisible`, `IsEnabled`) and executes the registered closure safely.
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <optional>
#include "app/actions/ui_action.hpp"

namespace Folio {

/**
 * @class UIActionRegistry
 * @brief Global and scoped manager for UIAction registration, querying, and execution.
 */
class UIActionRegistry {
public:
    using ContextProvider = std::function<std::vector<UIAction>()>;

    UIActionRegistry() = default;
    ~UIActionRegistry() = default;

    /**
     * @brief Access the global singleton registry instance.
     * @return Reference to the global UIActionRegistry.
     */
    static UIActionRegistry& Instance();

    // =========================================================================
    // ACTION REGISTRATION & MANAGEMENT
    // =========================================================================

    /**
     * @brief Registers or replaces an action in the catalog.
     * @param action Universal action descriptor. Must contain a valid, non-empty `id`.
     */
    void RegisterAction(UIAction action);

    /**
     * @brief Unregisters an action by its unique identifier.
     * @param id Action identifier.
     * @return true if an action was removed; false if not found.
     */
    bool UnregisterAction(const std::string& id);

    /**
     * @brief Looks up a registered action by identifier.
     * @param id Action identifier.
     * @return Pointer to registered UIAction, or nullptr if not registered.
     */
    [[nodiscard]] const UIAction* FindAction(const std::string& id) const;

    /**
     * @brief Checks if an action with the specified identifier is registered.
     * @param id Action identifier.
     * @return true if found.
     */
    [[nodiscard]] bool HasAction(const std::string& id) const;

    // =========================================================================
    // EXECUTION & SHORTCUT DISPATCH
    // =========================================================================

    /**
     * @brief Executes an action by its identifier if it exists and is enabled.
     * @param id Action identifier.
     * @return true if the action was found and executed; false otherwise.
     */
    bool Execute(const std::string& id) const;

    /**
     * @brief Evaluates an incoming keyboard shortcut chord against registered actions.
     * @param shortcut Normalized shortcut chord string (e.g. "Ctrl+Z", "Delete").
     * @return true if an active, enabled action handled the shortcut.
     */
    bool DispatchShortcut(const std::string& shortcut) const;

    // =========================================================================
    // QUERYING & FILTERING
    // =========================================================================

    /**
     * @brief Retrieves all registered actions belonging to a specific domain category.
     * @param category Domain category string (e.g. "Canvas", "Edit", "Layer").
     * @return Stably sorted list of actions matching the category.
     */
    [[nodiscard]] std::vector<UIAction> GetActionsByCategory(const std::string& category) const;

    /**
     * @brief Returns all registered actions in the registry.
     * @return Vector containing copies of all registered actions.
     */
    [[nodiscard]] std::vector<UIAction> GetAllActions() const;

    // =========================================================================
    // CONTEXTUAL ACTION PROVIDERS
    // =========================================================================

    /**
     * @brief Registers a dynamic provider for contextual actions (e.g. Selection, Page, Canvas).
     * @param contextName Identifier for the context (e.g. "canvas.selection", "nav.page").
     * @param provider Callback returning actions relevant to the current state.
     */
    void RegisterContextProvider(const std::string& contextName, ContextProvider provider);

    /**
     * @brief Removes a registered contextual action provider.
     * @param contextName Identifier for the context.
     */
    void UnregisterContextProvider(const std::string& contextName);

    /**
     * @brief Resolves all actions currently generated by a registered context provider.
     * @param contextName Identifier for the context.
     * @return Evaluated vector of actions for the active context.
     */
    [[nodiscard]] std::vector<UIAction> GetContextActions(const std::string& contextName) const;

    /**
     * @brief Clears all registered actions and context providers.
     */
    void Clear();

private:
    std::unordered_map<std::string, UIAction> m_actions;
    std::unordered_map<std::string, std::string> m_shortcutMap; // Normalized shortcut -> Action ID
    std::unordered_map<std::string, ContextProvider> m_contextProviders;
};

} // namespace Folio
