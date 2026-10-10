#pragma once
/**
 * =========================================================================================
 * @file ui/framework/ui_builder.hpp
 * @brief Fluent Widget Factory & Button Generation for FolioNote Modern UI
 * =========================================================================================
 *
 * GENERAL ARCHITECTURE:
 * ---------------------
 * UIBuilder replaces repetitive raw ImGui code with a fluent, declarative component builder.
 * Every widget integrates with UIAnimationManager for smooth hover/press transitions and
 * UIShapeManager for multi-pass shadows, acrylic surfaces, and rounded squircles.
 */

#include "imgui.h"
#include "ui/framework/ui_tokens.hpp"
#include "ui/framework/ui_animation_manager.hpp"
#include "ui/framework/ui_shape_manager.hpp"
#include <string>
#include <vector>
#include <functional>

namespace Folio::UI {

/**
 * @enum UIButtonVariant
 * @brief Visual styling variant for buttons.
 */
enum class UIButtonVariant {
    Primary,      ///< Accent-filled hero action (e.g. Save, Create, Next)
    Secondary,    ///< Elevated neutral card button with subtle border
    Ghost,        ///< Borderless transparent button; fades in on hover
    Danger,       ///< Red-tinted destructive action (e.g. Delete, Reset)
    Glassmorphic  ///< Translucent acrylic background with top-edge specular line
};

/**
 * @enum UIButtonSize
 * @brief Standardized button sizing tiers.
 */
enum class UIButtonSize {
    Small,   ///< Height 26px, padding 8px
    Medium,  ///< Height 34px, padding 12px
    Large    ///< Height 42px, padding 18px
};

/**
 * @class ButtonBuilder
 * @brief Fluent chainable builder for modern animated buttons.
 */
class ButtonBuilder {
public:
    explicit ButtonBuilder(const std::string& label);

    ButtonBuilder& Variant(UIButtonVariant variant);
    ButtonBuilder& Size(UIButtonSize size);
    ButtonBuilder& Icon(const std::string& iconText);
    ButtonBuilder& Badge(const std::string& badgeText);
    ButtonBuilder& Tooltip(const std::string& tooltip);
    ButtonBuilder& Width(float width);
    ButtonBuilder& Disabled(bool disabled = true);
    ButtonBuilder& Selected(bool selected = true);

    /**
     * @brief Renders the button into the active ImGui window.
     * @return true if the button was clicked this frame.
     */
    bool Render();

private:
    std::string label_;
    std::string iconText_;
    std::string badgeText_;
    std::string tooltip_;
    UIButtonVariant variant_ = UIButtonVariant::Secondary;
    UIButtonSize size_ = UIButtonSize::Medium;
    float customWidth_ = 0.0f;
    bool isDisabled_ = false;
    bool isSelected_ = false;
};

/**
 * @class SwitchBuilder
 * @brief Fluent chainable builder for modern toggle switches with sliding knobs.
 */
class SwitchBuilder {
public:
    SwitchBuilder(const std::string& label, bool* value);

    SwitchBuilder& Description(const std::string& desc);
    SwitchBuilder& Disabled(bool disabled = true);

    /**
     * @brief Renders the switch into the active ImGui window.
     * @return true if the toggle state changed this frame.
     */
    bool Render();

private:
    std::string label_;
    std::string description_;
    bool* value_;
    bool isDisabled_ = false;
};

/**
 * @class SegmentedSwitchBuilder
 * @brief Segmented tab / option selector with an animated sliding indicator pill.
 */
class SegmentedSwitchBuilder {
public:
    SegmentedSwitchBuilder(const std::string& id, const std::vector<std::string>& options, int* selectedIndex);

    SegmentedSwitchBuilder& Width(float totalWidth);

    /**
     * @brief Renders the segmented switch into the active ImGui window.
     * @return true if the selected index changed this frame.
     */
    bool Render();

private:
    std::string id_;
    std::vector<std::string> options_;
    int* selectedIndex_;
    float customWidth_ = 0.0f;
};

/**
 * @class CardBuilder
 * @brief Fluent builder for content cards and grouped settings panels.
 */
class CardBuilder {
public:
    explicit CardBuilder(const std::string& title);

    CardBuilder& Subtitle(const std::string& subtitle);
    CardBuilder& HeaderIcon(const std::string& iconText);
    CardBuilder& Elevation(float elevation);
    CardBuilder& WithBody(std::function<void()> bodyFunc);

    void Render();

private:
    std::string title_;
    std::string subtitle_;
    std::string headerIcon_;
    float elevation_ = Elevation::low;
    std::function<void()> bodyFunc_;
};

/**
 * @brief Global UI component entry point.
 */
class UI {
public:
    static ButtonBuilder Button(const std::string& label) {
        return ButtonBuilder(label);
    }

    static SwitchBuilder Switch(const std::string& label, bool* value) {
        return SwitchBuilder(label, value);
    }

    static SegmentedSwitchBuilder SegmentedSwitch(const std::string& id, const std::vector<std::string>& options, int* selectedIndex) {
        return SegmentedSwitchBuilder(id, options, selectedIndex);
    }

    static CardBuilder Card(const std::string& title) {
        return CardBuilder(title);
    }
};

} // namespace Folio::UI
