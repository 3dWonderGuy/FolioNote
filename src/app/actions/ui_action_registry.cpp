/**
 * =========================================================================================
 * @file app/actions/ui_action_registry.cpp
 * @brief Implementation of UIActionRegistry Dispatcher and Discovery Service
 * =========================================================================================
 */

#include "app/actions/ui_action_registry.hpp"
#include <algorithm>

namespace Folio {

UIActionRegistry& UIActionRegistry::Instance() {
    static UIActionRegistry instance;
    return instance;
}

void UIActionRegistry::RegisterAction(UIAction action) {
    if (action.id.empty()) {
        // Fallback: If no explicit ID provided, synthesize one from category and label
        action.id = (action.category.empty() ? "action" : action.category) + "." + action.label;
    }

    // Register or update shortcut mapping if a valid keyboard shortcut is declared
    if (!action.shortcut.empty()) {
        m_shortcutMap[action.shortcut] = action.id;
    }

    m_actions[action.id] = std::move(action);
}

bool UIActionRegistry::UnregisterAction(const std::string& id) {
    auto it = m_actions.find(id);
    if (it == m_actions.end()) {
        return false;
    }

    // Remove associated keyboard shortcut mapping if present
    if (!it->second.shortcut.empty()) {
        auto scIt = m_shortcutMap.find(it->second.shortcut);
        if (scIt != m_shortcutMap.end() && scIt->second == id) {
            m_shortcutMap.erase(scIt);
        }
    }

    m_actions.erase(it);
    return true;
}

const UIAction* UIActionRegistry::FindAction(const std::string& id) const {
    auto it = m_actions.find(id);
    if (it != m_actions.end()) {
        return &it->second;
    }
    return nullptr;
}

bool UIActionRegistry::HasAction(const std::string& id) const {
    return m_actions.find(id) != m_actions.end();
}

bool UIActionRegistry::Execute(const std::string& id) const {
    const UIAction* act = FindAction(id);
    if (!act) {
        return false;
    }

    if (!act->IsVisible() || !act->IsEnabled()) {
        return false;
    }

    act->Execute();
    return true;
}

bool UIActionRegistry::DispatchShortcut(const std::string& shortcut) const {
    if (shortcut.empty()) {
        return false;
    }

    auto scIt = m_shortcutMap.find(shortcut);
    if (scIt == m_shortcutMap.end()) {
        return false;
    }

    return Execute(scIt->second);
}

std::vector<UIAction> UIActionRegistry::GetActionsByCategory(const std::string& category) const {
    std::vector<UIAction> result;
    for (const auto& [id, action] : m_actions) {
        if (action.category == category && action.IsVisible()) {
            result.push_back(action);
        }
    }

    // Deterministic sorting by `order` weight ascending
    std::stable_sort(result.begin(), result.end(), [](const UIAction& a, const UIAction& b) {
        return a.order < b.order;
    });

    return result;
}

std::vector<UIAction> UIActionRegistry::GetAllActions() const {
    std::vector<UIAction> result;
    result.reserve(m_actions.size());
    for (const auto& [id, action] : m_actions) {
        if (action.IsVisible()) {
            result.push_back(action);
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const UIAction& a, const UIAction& b) {
        return a.order < b.order;
    });

    return result;
}

void UIActionRegistry::RegisterContextProvider(const std::string& contextName, ContextProvider provider) {
    if (provider) {
        m_contextProviders[contextName] = std::move(provider);
    }
}

void UIActionRegistry::UnregisterContextProvider(const std::string& contextName) {
    m_contextProviders.erase(contextName);
}

std::vector<UIAction> UIActionRegistry::GetContextActions(const std::string& contextName) const {
    auto it = m_contextProviders.find(contextName);
    if (it != m_contextProviders.end() && it->second) {
        std::vector<UIAction> actions = it->second();
        std::stable_sort(actions.begin(), actions.end(), [](const UIAction& a, const UIAction& b) {
            return a.order < b.order;
        });
        return actions;
    }
    return {};
}

void UIActionRegistry::Clear() {
    m_actions.clear();
    m_shortcutMap.clear();
    m_contextProviders.clear();
}

} // namespace Folio
