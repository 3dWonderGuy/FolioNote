#pragma once
/**
 * =========================================================================================
 * @file ui/framework/ui_tokens.hpp
 * @brief Centralized Design System Tokens for FolioNote Modern UI Framework
 * =========================================================================================
 *
 * GENERAL ARCHITECTURE & DESIGN PHILOSOPHY:
 * -----------------------------------------
 * Design tokens are the foundational visual atoms of the user interface. By standardizing
 * metrics (spacing, corner radiuses, elevations, typography scales, animation timings,
 * and semantic color abstractions), this system ensures that:
 * 1. UI components (buttons, cards, sidebars, popups, inputs) feel cohesive and premium.
 * 2. Visual rhythm is consistent across all views (Notebook Hub, Ribbon Bar, Settings, Modals).
 * 3. Theme transitions (Dark Mode vs Light Mode, High Contrast) remain seamless and decoupled
 *    from specific pixel values.
 */

#include "imgui.h"
#include <cstdint>

namespace Folio::UI {

/**
 * @struct Spacing
 * @brief Standardized 4pt/8pt grid spacing scale.
 *
 * Used for component padding, margins, gaps, and item separations.
 */
struct Spacing {
    static constexpr float xxs = 2.0f;  ///< Micro adjustments, icon-to-badge gaps
    static constexpr float xs  = 4.0f;  ///< Compact gaps, tight list item paddings
    static constexpr float sm  = 8.0f;  ///< Standard button inner padding, chip margins
    static constexpr float md  = 12.0f; ///< Standard card padding, toolbar section spacing
    static constexpr float lg  = 16.0f; ///< Modal dialog padding, sidebar margins
    static constexpr float xl  = 24.0f; ///< Page headers, major section gutters
    static constexpr float xxl = 32.0f; ///< Hub landing page gutters, empty state offsets
};

/**
 * @struct Radii
 * @brief Standardized corner radiuses for geometric contours and squircles.
 */
struct Radii {
    static constexpr float none = 0.0f;   ///< Sharp rectangular corners
    static constexpr float xs   = 4.0f;   ///< Tooltip cards, mini badges, keyboard shortcut chips
    static constexpr float sm   = 6.0f;   ///< Standard buttons, input text fields, dropdown items
    static constexpr float md   = 10.0f;  ///< Content cards, popovers, flyout menus
    static constexpr float lg   = 16.0f;  ///< Modal dialog windows, floating action panels
    static constexpr float pill = 999.0f; ///< Full capsule pills, status indicators, segmented tabs
};

/**
 * @struct Elevation
 * @brief Virtual depth layers and multi-pass drop shadow offsets.
 *
 * Higher elevations cast larger, softer ambient shadows and higher alpha specular borders.
 */
struct Elevation {
    static constexpr float none   = 0.0f;  ///< Flat on background
    static constexpr float low    = 2.0f;  ///< Resting cards, subtle list item hovers
    static constexpr float medium = 6.0f;  ///< Floating toolbars, active button elevation, dropdowns
    static constexpr float high   = 14.0f; ///< Detached popovers, context menus
    static constexpr float modal  = 24.0f; ///< Modal dialogs with dark backdrop dimming
};

/**
 * @struct Duration
 * @brief Standard animation durations in seconds.
 */
struct Duration {
    static constexpr float instant = 0.08f; ///< Micro-interactions: button down, check toggles
    static constexpr float fast    = 0.16f; ///< Hover glow fades, tooltip appearances
    static constexpr float normal  = 0.25f; ///< Dropdown expansions, tab slider movements
    static constexpr float slow    = 0.38f; ///< Sidebar drawer slide, modal entrance/exit
};

/**
 * @struct ColorUtils
 * @brief Color conversion and alpha blending utilities for Dear ImGui.
 */
struct ColorUtils {
    /**
     * @brief Linearly interpolates two 32-bit packed ImU32 RGBA colors.
     * @param a Starting color.
     * @param b Ending color.
     * @param t Interpolation parameter in [0.0, 1.0].
     * @return Interpolated packed ImU32 color.
     */
    static inline ImU32 Lerp(ImU32 a, ImU32 b, float t) noexcept {
        if (t <= 0.0f) return a;
        if (t >= 1.0f) return b;

        uint32_t aR = (a >> IM_COL32_R_SHIFT) & 0xFF;
        uint32_t aG = (a >> IM_COL32_G_SHIFT) & 0xFF;
        uint32_t aB = (a >> IM_COL32_B_SHIFT) & 0xFF;
        uint32_t aA = (a >> IM_COL32_A_SHIFT) & 0xFF;

        uint32_t bR = (b >> IM_COL32_R_SHIFT) & 0xFF;
        uint32_t bG = (b >> IM_COL32_G_SHIFT) & 0xFF;
        uint32_t bB = (b >> IM_COL32_B_SHIFT) & 0xFF;
        uint32_t bA = (b >> IM_COL32_A_SHIFT) & 0xFF;

        uint32_t r = static_cast<uint32_t>(aR + t * (static_cast<float>(bR) - aR));
        uint32_t g = static_cast<uint32_t>(aG + t * (static_cast<float>(bG) - aG));
        uint32_t bVal = static_cast<uint32_t>(aB + t * (static_cast<float>(bB) - aB));
        uint32_t aVal = static_cast<uint32_t>(aA + t * (static_cast<float>(bA) - aA));

        return IM_COL32(r, g, bVal, aVal);
    }

    /**
     * @brief Adjusts the alpha channel of a packed ImU32 color.
     * @param col Base color.
     * @param alpha Multiplier in [0.0, 1.0].
     * @return Color with adjusted alpha.
     */
    static inline ImU32 WithAlpha(ImU32 col, float alpha) noexcept {
        uint32_t r = (col >> IM_COL32_R_SHIFT) & 0xFF;
        uint32_t g = (col >> IM_COL32_G_SHIFT) & 0xFF;
        uint32_t b = (col >> IM_COL32_B_SHIFT) & 0xFF;
        uint32_t a = (col >> IM_COL32_A_SHIFT) & 0xFF;
        uint32_t newA = static_cast<uint32_t>(static_cast<float>(a) * std::clamp(alpha, 0.0f, 1.0f));
        return IM_COL32(r, g, b, newA);
    }
};

} // namespace Folio::UI
