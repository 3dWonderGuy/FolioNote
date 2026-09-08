#pragma once
#include "imgui.h"
#include "app/theme_manager.hpp"
#include "ui/imgui_theme.hpp"
#include "ui/icon_manager.hpp"
#include "input/preset_manager.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace FolioUI {

// ============================================================================
// 1. TOOLBAR ENUMS & TYPES
// ============================================================================

enum class ToolbarButtonSize {
    Large,    // Classic ribbon format (~54x58px): icon top, label bottom
    Medium,   // Compact horizontal format (~84x28px): icon left, label right
    Small,    // Compact square format (~28x28px): icon only, stackable 2-high
    Pill,     // Rounded capsule format (~68x50px): icon + active color indicator
    Circle,   // Circular button format (~38x38px): color swatch or tip preview
    Nib,      // Procedural pen/brush nib preview (~40x58px)
    Custom    // User-specified dimensions
};

enum class ToolbarItemKind {
    Action,       // Momentary action on click
    Toggle,       // On/off toggle reflecting active state
    Split,        // Dual-zone button: primary action + dropdown chevron for submenu
    Menu,         // Clicking opens a dropdown flyout submenu
    Settings,     // Clicking opens a settings popover dialog
    NibPreset,    // Pen/brush nib preset selector + configurator
    CustomWidget  // Embedded custom ImGui widget (slider, combo, color picker)
};

// ============================================================================
// 2. FLYOUT SUBMENU BUILDER
// ============================================================================

class FlyoutMenuBuilder {
public:
    const ThemeManager& theme;
    bool hasItems = false;

    explicit FlyoutMenuBuilder(const ThemeManager& t);

    void AddHeader(const char* title);
    void AddSeparator();

    bool AddItem(
        const char* label,
        GLuint iconTex = 0,
        const char* shortcut = nullptr,
        bool isSelected = false,
        std::function<void()> onSelect = nullptr
    );

    bool AddItem(
        const char* label,
        GLuint iconTex,
        const char* shortcut,
        std::function<void()> onSelect,
        bool isSelected = false
    );

    bool AddItem(
        const char* label,
        std::function<void()> onSelect,
        bool isSelected = false
    );

    bool AddCheckItem(
        const char* label,
        bool isChecked,
        std::function<void(bool)> onToggle = nullptr
    );

    void AddCustom(const std::function<void()>& drawFunc);
};

// ============================================================================
// 3. SETTINGS POPOVER BUILDER
// ============================================================================

class SettingsPopoverBuilder {
public:
    const ThemeManager& theme;

    explicit SettingsPopoverBuilder(const ThemeManager& t);

    void AddTitle(const char* title);
    bool AddSlider(const char* label, float* value, float minVal, float maxVal, const char* format = "%.1f");
    bool AddCheckbox(const char* label, bool* value);
    bool AddColorPicker(const char* label, ImVec4* color);
    bool AddButton(const char* label, ImVec2 size = ImVec2(0, 0));
    void AddCustom(const std::function<void()>& drawFunc);
};

// ============================================================================
// 4. LOW-LEVEL TOOLBAR CONTROLS & PRIMITIVES
// ============================================================================

class ToolbarControls {
public:
    static bool RenderLargeButton(
        const char* strId,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        const ThemeManager& theme,
        bool isActive = false,
        bool flipH = false,
        ImVec2 size = ImVec2(54.0f, 58.0f)
    );

    static bool RenderSmallButton(
        const char* strId,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        const ThemeManager& theme,
        bool isActive = false,
        bool flipH = false,
        ImVec2 size = ImVec2(34.0f, 27.0f)
    );

    static bool RenderSplitButton(
        const char* strId,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        const ThemeManager& theme,
        bool isActive,
        std::function<void()> onAction,
        std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
        bool flipH = false,
        ImVec2 totalSize = ImVec2(68.0f, 58.0f)
    );

    static bool RenderPillButton(
        const char* strId,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        const ThemeManager& theme,
        bool isActive,
        ImVec4 accentColor,
        std::function<void()> onClick,
        std::function<void(SettingsPopoverBuilder&)> onSettings = nullptr,
        ImVec2 size = ImVec2(66.0f, 58.0f)
    );

    static bool RenderCircleButton(
        const char* strId,
        ImVec4 fillColor,
        const char* tooltip,
        const ThemeManager& theme,
        bool isActive = false,
        float diameter = 36.0f
    );

    static bool RenderPenNibControl(
        const char* strId,
        PenPreset& preset,
        bool isActive,
        const ThemeManager& theme,
        std::function<void()> onSelect,
        std::function<void(PenPreset&)> onCustomChange = nullptr,
        std::function<void(const std::string&)> onDelete = nullptr,
        ImVec2 size = ImVec2(42.0f, 58.0f),
        int presetIndex = -1,
        int totalPresets = 0,
        std::function<void(int fromIdx, int toIdx)> onReorder = nullptr
    );

    static void RenderZoomCompound(
        const char* strId,
        const ThemeManager& theme,
        std::function<void()> onZoomIn,
        std::function<void()> onZoomOut,
        std::function<void()> onZoomReset,
        std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
        const char* zoomPercentLabel = "100%",
        ImVec2 totalSize = ImVec2(118.0f, 58.0f)
    );
};

// ============================================================================
// 5. TOOLBAR SUBSECTION BUILDER ("The Machine")
// ============================================================================

class ToolbarSectionBuilder {
private:
    const char* sectionId;
    const char* sectionTitle;
    const ThemeManager& theme;
    bool inStack = false;
    int stackItemCount = 0;
    ImVec2 stackStartPos;
    float stackMaxW = 0.0f;
    bool hasItems = false;
    bool isMiniMode = false;

public:
    ToolbarSectionBuilder(const char* id, const char* title, const ThemeManager& t, bool mini = false);
    ToolbarSectionBuilder& SetMiniMode(bool mini) { isMiniMode = mini; return *this; }
    [[nodiscard]] bool GetMiniMode() const { return isMiniMode; }

    void FlowNextItem(float spacing = 6.0f);
    ToolbarSectionBuilder& BeginStack();
    ToolbarSectionBuilder& EndStack();

    ToolbarSectionBuilder& AddLargeButton(
        const char* id,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        bool isActive,
        std::function<void()> onClick,
        bool flipH = false,
        ImVec2 size = ImVec2(54.0f, 58.0f)
    );

    ToolbarSectionBuilder& AddSmallButton(
        const char* id,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        bool isActive,
        std::function<void()> onClick,
        bool flipH = false,
        ImVec2 size = ImVec2(34.0f, 27.0f)
    );

    ToolbarSectionBuilder& AddSplitButton(
        const char* id,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        bool isActive,
        std::function<void()> onAction,
        std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
        bool flipH = false,
        ImVec2 size = ImVec2(68.0f, 58.0f)
    );

    ToolbarSectionBuilder& AddPillButton(
        const char* id,
        GLuint iconTex,
        const char* label,
        const char* tooltip,
        bool isActive,
        ImVec4 accentColor,
        std::function<void()> onClick,
        std::function<void(SettingsPopoverBuilder&)> onSettings = nullptr,
        ImVec2 size = ImVec2(66.0f, 58.0f)
    );

    ToolbarSectionBuilder& AddCircleButton(
        const char* id,
        ImVec4 fillColor,
        const char* tooltip,
        bool isActive,
        std::function<void()> onClick,
        float diameter = 36.0f
    );

    ToolbarSectionBuilder& AddPenNibControl(
        const char* id,
        PenPreset& preset,
        bool isActive,
        std::function<void()> onSelect,
        std::function<void(PenPreset&)> onCustomChange = nullptr,
        std::function<void(const std::string&)> onDelete = nullptr,
        ImVec2 size = ImVec2(42.0f, 58.0f),
        int presetIndex = -1,
        int totalPresets = 0,
        std::function<void(int fromIdx, int toIdx)> onReorder = nullptr
    );

    ToolbarSectionBuilder& AddZoomCompound(
        const char* id,
        std::function<void()> onZoomIn,
        std::function<void()> onZoomOut,
        std::function<void()> onZoomReset,
        std::function<void(FlyoutMenuBuilder&)> onBuildMenu,
        const char* zoomPercentLabel = "100%",
        ImVec2 size = ImVec2(118.0f, 58.0f)
    );

    ToolbarSectionBuilder& AddWidget(const std::function<void()>& widgetFunc, float spacing = 8.0f);

    void Render(float sectionSpacing = 18.0f);
};

// ============================================================================
// 6. FUTURE-PROOF DATA-DRIVEN CONFIGURATION STRUCTS
// ============================================================================

struct ToolbarItemDef {
    std::string id;
    std::string label;
    std::string tooltip;
    std::string iconKey;
    ToolbarButtonSize size = ToolbarButtonSize::Large;
    ToolbarItemKind kind   = ToolbarItemKind::Action;
    bool isVisible         = true;
    bool isEnabled         = true;
    bool flipH             = false;
    ImVec4 accentColor     = ImVec4(1, 1, 1, 1);

    // Callbacks
    std::function<bool()> isChecked;
    std::function<void()> onAction;
    std::function<void(FlyoutMenuBuilder&)> onMenu;
    std::function<void(SettingsPopoverBuilder&)> onSettings;
};

struct ToolbarSectionDef {
    std::string id;
    std::string title;
    bool isVisible = true;
    std::vector<ToolbarItemDef> items;
};

} // namespace FolioUI
