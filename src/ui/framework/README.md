# Modern UI Component Framework (`src/ui/framework`)

A modern, declarative, physics-animated UI component layer built on top of **Dear ImGui** for FolioNote. It eliminates ad-hoc styling and duplicated layout code across views by providing standardized tokens, smooth spring-damper animations, glassmorphic shape rendering, fluent widget builders, a responsive sidebar, and an overlay/toast host.

---

## Architecture Overview

```mermaid
graph TD
    App[Application::Run Loop] --> NewFrame[ImGui::NewFrame]
    NewFrame --> AnimUpdate[UIAnimationManager::Update(dt)]
    AnimUpdate --> Views[Views & Overlays]
    
    subgraph UI Framework Layer
        Tokens[UI Tokens<br/>Spacing, Radii, Colors]
        AnimMgr[Animation Engine<br/>Spring-Damper & Exp Decay]
        ShapeMgr[Shape & Surface Primitives<br/>Shadows, Acrylic, Glow]
        Builder[UI Builders<br/>Button, Switch, Card, Segmented]
        Sidebar[UISidebar<br/>Animated Dockable Rail]
        OverlayHost[UIOverlayHost<br/>Toasts & Modals]
    end
    
    Views --> Builder
    Views --> Sidebar
    Views --> OverlayHost
    Builder --> ShapeMgr
    Builder --> AnimMgr
    Sidebar --> AnimMgr
    Sidebar --> ShapeMgr
    OverlayHost --> ShapeMgr
    OverlayHost --> AnimMgr
    
    Views --> OverlayRender[UIOverlayHost::Render(dt)]
    OverlayRender --> ImGuiRender[ImGui::Render]
    ImGuiRender --> SDLRender[SDL3 / Metal / DX11 Renderer]
```

---

## 1. Design Tokens (`ui_tokens.hpp`)

Centralized spacing, rounding, elevations, and color manipulation utilities ensuring visual consistency across all viewports and DPI scales.

### Tokens
* **Spacing**: `xs` (4px), `sm` (8px), `md` (12px), `lg` (16px), `xl` (24px), `xxl` (32px).
* **Radii**: `none` (0px), `xs` (3px), `sm` (6px), `md` (10px), `lg` (16px), `pill` (9999px).
* **Elevation**:
  * `Flat`: No shadow.
  * `Raised`: `offsetY = 2`, `blur = 4`, `spread = 0`, opacity $12\%$.
  * `Floating`: `offsetY = 6`, `blur = 16`, `spread = 2`, opacity $18\%$.
  * `Modal`: `offsetY = 16`, `blur = 36`, `spread = 4`, opacity $28\%$.
* **ColorUtils**:
  * `Lerp(c1, c2, t)`: Smooth linear interpolation between two 32-bit packed colors (`ImU32`).
  * `WithAlpha(col, a)`: Replaces or modulates the alpha channel of an existing `ImU32` color.

---

## 2. Animation & Transition Engine (`ui_animation_manager.hpp`)

Provides frame-rate independent animations without requiring manual delta-time accumulators in every widget.

### Mathematical Foundations

#### Frame-rate Independent Exponential Decay
Used for smooth hover, focus, and continuous parameter transitions:
$$x(t + \Delta t) = x_{\text{target}} + (x(t) - x_{\text{target}}) \cdot e^{-\lambda \cdot \Delta t}$$
Where $\lambda$ is the responsiveness parameter (default $\lambda = 16.0\,\text{s}^{-1}$).

#### Damped Harmonic Oscillator (Spring Physics)
Used for physical UI interactions (e.g. sidebar collapse/expand, modal bounce, toggle switches):
$$m \ddot{x} + c \dot{x} + k (x - x_{\text{target}}) = 0$$
Normalized with mass $m = 1$:
$$a = -k (x - x_{\text{target}}) - c v$$
Integrated using semi-implicit Euler integration:
$$v_{t+\Delta t} = v_t + a \cdot \Delta t$$
$$x_{t+\Delta t} = x_t + v_{t+\Delta t} \cdot \Delta t$$

#### Cubic Easing Formulations
* **Ease-In-Out**: $3t^2 - 2t^3$ (Smooth hermite interpolation).
* **Ease-Out-Back**: $1 + (s + 1)(t - 1)^3 + s(t - 1)^2$ with overshoot $s = 1.70158$.

---

## 3. Shape & Surface Primitives (`ui_shape_manager.hpp`)

Direct `ImDrawList` primitives for modern UI depth and styling:
* **Multi-Pass Ambient Occlusion Shadows (`DrawShadow`)**: Simulates diffuse drop shadows by rendering concentric rounded rectangles with exponentially decaying alpha.
* **Glassmorphic Acrylic Surfaces (`DrawAcrylicPanel`)**: Simulates frosted glass using a translucent fill, an ambient border, and a subtle 1px specular top rim highlight.
* **Interactive Card Surfaces (`DrawInteractiveCard`)**: Automatically blends background tint, borders, and drop shadows based on hover state.
* **Capsule Status Badges (`DrawPillBadge`)**: Draws pill badges with customizable background tint, border, and colored indicator dots.
* **Soft Radial Glow (`DrawGlow`)**: Soft circular glows for active tools, stylus cursors, or notification badges.

---

## 4. Fluent Widget Builders (`ui_builder.hpp`)

Chainable, declarative builders that wrap ImGui item layout, hit-testing, and animated drawing.

### Button (`UI::Button`)
```cpp
if (UI::Button("Save Document")
        .Primary()
        .Icon(ICON_SAVE)
        .Tooltip("Save changes to disk (Ctrl+S)")
        .Build()) 
{
    SaveDocument();
}
```
* **Styles**: `Primary()`, `Secondary()`, `Ghost()`, `Danger()`, `Glass()`.
* **Features**: Animated hover/click states, optional loading spinner (`Loading(bool)`), tooltips, and width/height overrides.

### Toggle Switch (`UI::Switch`)
```cpp
static bool s_enableSnap = true;
if (UI::Switch("Snap to Grid", &s_enableSnap)
        .Description("Automatically snap strokes to nearest grid intersections")
        .Build()) 
{
    OnGridSnapToggled(s_enableSnap);
}
```
* **Features**: Smooth spring-animated sliding knob, click feedback, optional descriptive subtitle.

### Segmented Switch (`UI::SegmentedSwitch`)
```cpp
static int s_activeTool = 0;
std::vector<std::string> tools = {"Pen", "Pencil", "Highlighter", "Eraser"};
if (UI::SegmentedSwitch("##ToolSelect", tools, &s_activeTool).Build()) {
    SelectTool(s_activeTool);
}
```
* **Features**: Animated sliding pill indicator tracking the active selection index smoothly across segments.

### Styled Card Container (`UI::Card`)
```cpp
UI::Card card("Notebook Overview");
card.Subtitle("3 notebooks, 48 pages")
    .HeaderIcon(ICON_BOOK);
if (card.Begin()) {
    ImGui::Text("Card body contents...");
    card.End();
}
```

---

## 5. Reusable Animated Sidebar (`ui_sidebar.hpp`)

A collapsible navigation sidebar supporting smooth transition between a compact icon-only rail (52px) and a full flyout/drawer navigation list (260px).

```cpp
static UISidebar s_sidebar;

// During view rendering:
s_sidebar.SetItems({
    { "hub",      "Notebooks",  ICON_BOOK,     true, 0 },
    { "templates","Templates",  ICON_TEMPLATE, true, 0 },
    { "trash",    "Recycle Bin",ICON_TRASH,    false,3 }
});

s_sidebar.Render("##MainSidebar", s_activePage, [](const std::string& id) {
    NavigateToPage(id);
});
```

* **Features**:
  * Spring-animated width expansion and collapse.
  * Active item indicator with accent-colored left pill.
  * Notification badge counter for items.
  * Fixed footer with settings button and collapse toggle.

---

## 6. Overlay & Pop-up Host (`ui_overlay_host.hpp`)

A singleton manager rendering toast notifications and centered modal dialogs above all ImGui viewports.

### Showing Toasts
```cpp
UIOverlayHost::Instance().ShowToast(
    "File Exported", 
    "Notebook successfully exported to PDF.", 
    ToastType::Success, 
    4.0f
);
```

### Showing Modals
```cpp
UIOverlayHost::Instance().ShowModal(
    "Delete Section?",
    "Are you sure you want to delete this section? This action cannot be undone.",
    []() { DeleteSectionConfirmed(); },
    []() { /* Cancelled */ },
    "Delete",
    "Cancel",
    true // isDanger
);
```

* **Features**:
  * Bottom-right stacked toast queue with countdown time bars and spring slide-in transitions.
  * Modals with darkened backdrop dimming and spring entrance pop-in scale ($0.8 \rightarrow 1.0$).
  * Keyboard navigation (Esc to cancel, Enter to confirm).
