#pragma once
/**
 * =========================================================================================
 * @file ui/overlays/command_palette.hpp
 * @brief Spotlight-Style Interactive Action Search & Command Palette Modal
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * --------------------------------
 * The ActionCommandPalette is a modern, keyboard-first modal popup (triggered via Ctrl+K,
 * Ctrl+P, or titlebar search) allowing users to discover, search, and execute any application
 * capability registered in the `UIActionRegistry`.
 *
 * WORKING PROCESS:
 * 1. Invocation:
 *    User presses `Ctrl+K` or clicks the Search icon. `Open()` is called, which initializes
 *    the search query buffer and sets focus to the search text input.
 * 2. Instant Query & Filtering:
 *    As the user types, the palette performs case-insensitive substring matching against
 *    action labels, categories, and keywords.
 * 3. Keyboard Navigation:
 *    - Up / Down arrow keys: navigate candidate list.
 *    - Enter key: dispatches the highlighted action immediately and closes the palette.
 *    - Escape key or click-outside: dismisses the palette without execution.
 * 4. Visual Layout:
 *    - Centered glassmorphic floating modal.
 *    - Top search bar with magnifying glass icon.
 *    - Scrollable results list with icon, category badge, and keyboard shortcut hint.
 */

#include <string>
#include <vector>
#include <imgui.h>
#include "app/actions/ui_action.hpp"
#include "app/theme_manager.hpp"

namespace Folio {

/**
 * @class ActionCommandPalette
 * @brief Floating spotlight command search palette.
 */
class ActionCommandPalette {
public:
    ActionCommandPalette() = default;
    ~ActionCommandPalette() = default;

    /**
     * @brief Opens the command palette and resets search state.
     */
    void Open();

    /**
     * @brief Closes the command palette.
     */
    void Close();

    /**
     * @brief Toggles open/closed state.
     */
    void Toggle();

    /**
     * @brief Checks if the palette is currently open.
     * @return true if visible.
     */
    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    /**
     * @brief Renders the spotlight command palette modal.
     * @param themeManager Active application ThemeManager reference.
     */
    void Render(const ThemeManager& themeManager);

private:
    bool m_isOpen = false;
    bool m_focusRequested = false;
    char m_searchQuery[128] = {0};
    int m_selectedIndex = 0;

    /**
     * @brief Performs case-insensitive substring search.
     * @param str Target string.
     * @param query Search query substring.
     * @return true if query matches.
     */
    static bool SubstringMatch(const std::string& str, const std::string& query);
};

} // namespace Folio
