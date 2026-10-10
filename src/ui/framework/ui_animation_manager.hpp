#pragma once
/**
 * =========================================================================================
 * @file ui/framework/ui_animation_manager.hpp
 * @brief Dynamic Transition & Physics-Based Animation Engine for FolioNote UI
 * =========================================================================================
 *
 * GENERAL ARCHITECTURE & WORKING PROCESS:
 * ---------------------------------------
 * Modern graphical user interfaces require fluid, responsive transitions to communicate
 * causality, depth, and spatial hierarchy. Raw ImGui evaluates immediately every frame,
 * which ordinarily produces abrupt snaps when elements change size, hover, or collapse.
 *
 * UIAnimationManager solves this by maintaining a frame-rate independent cache of active
 * tweens and spring oscillators:
 * 1. Interaction Alpha Tracking:
 *    Tracks continuous normalized values $t \in [0.0, 1.0]$ for widgets (buttons, tabs, inputs)
 *    during hover, press, and release states.
 * 2. Continuous Spring Simulation:
 *    Simulates a damped harmonic oscillator ($F = -k(x - x_0) - c v$) for elastic, tactile
 *    feel on button clicks, drawer snapping, and tab sliding badges.
 * 3. Exponential Decay & Cubic Easing:
 *    Uses frame-rate independent exponential decay ($x_{t+\Delta t} = x_{\text{target}} + (x_t - x_{\text{target}}) e^{-\lambda \Delta t}$)
 *    and Cubic Bezier easing for sliding panels, width expansions, and backdrop fades.
 */

#include "imgui.h"
#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace Folio::UI {

/**
 * @enum EasingType
 * @brief Mathematical curvature profile for animated parameter transitions.
 */
enum class EasingType {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutCubic,
    EaseOutBack
};

/**
 * @struct SpringState
 * @brief State descriptor for an active spring simulation.
 */
struct SpringState {
    float position = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;
    bool initialized = false;
};

/**
 * @struct TweenState
 * @brief State descriptor for an exponential decay or eased scalar value.
 */
struct TweenState {
    float current = 0.0f;
    float target = 0.0f;
    bool initialized = false;
};

/**
 * @struct WidgetInteractionState
 * @brief Normalized state tracking for a specific ImGui widget ID.
 */
struct WidgetInteractionState {
    float hoverAlpha = 0.0f;   ///< [0.0 = resting, 1.0 = fully hovered]
    float activeAlpha = 0.0f;  ///< [0.0 = unpressed, 1.0 = fully pressed down]
    uint64_t lastFrameActive = 0;
};

/**
 * @class UIAnimationManager
 * @brief Centralized singleton managing animations and smooth UI state transitions.
 */
class UIAnimationManager {
public:
    static UIAnimationManager& Instance();

    /**
     * @brief Advances simulation time by deltaTime seconds.
     * Evaluates active springs and garbage-collects dormant widget states.
     *
     * @param deltaTime Elapsed frame time in seconds (typically ImGui::GetIO().DeltaTime).
     */
    void Update(float deltaTime);

    /**
     * @brief Computes or retrieves a smooth frame-rate independent exponential decay value.
     *
     * MATHEMATICAL PROCESS:
     * Evaluates $x_{t+\Delta t} = x_{\text{target}} + (x_t - x_{\text{target}}) \cdot e^{-\lambda \Delta t}$.
     * Unlike naive linear interpolation ($x + (T - x) \cdot \alpha$), exponential decay
     * produces strictly identical motion regardless of frame rate (60 FPS vs 120 FPS vs 240 FPS).
     *
     * @param key Unique identifier for this animated property.
     * @param target Target destination value.
     * @param speed Decay rate coefficient $\lambda$ (e.g. 12.0f for snappy, 6.0f for gentle).
     * @return Current interpolated value for the active frame.
     */
    float GetFloat(const std::string& key, float target, float speed = 14.0f);

    /**
     * @brief Computes or retrieves a physically simulated damped spring position.
     *
     * MATHEMATICAL PROCESS:
     * Integrates Newton's second law for a damped harmonic oscillator:
     *   $a = -\frac{k}{m} (x - x_{\text{target}}) - \frac{c}{m} v$
     *   $v_{t+\Delta t} = v_t + a \Delta t$
     *   $x_{t+\Delta t} = x_t + v_{t+\Delta t} \Delta t$
     *
     * @param key Unique identifier for this spring.
     * @param target Equilibrium target position.
     * @param stiffness Spring stiffness constant $k$ (default 260.0f).
     * @param damping Damping coefficient $c$ (default 24.0f).
     * @return Current spring displacement.
     */
    float GetSpring(const std::string& key, float target, float stiffness = 260.0f, float damping = 24.0f);

    /**
     * @brief Evaluates normalized interaction state alphas for an individual ImGui widget.
     *
     * Smoothly transitions hoverAlpha and activeAlpha across frames:
     * - Hover fades in at ~14.0/s, fades out at ~10.0/s.
     * - Active (press) snaps in at ~28.0/s, releases smoothly at ~16.0/s.
     *
     * @param id Unique ImGui widget ID.
     * @param isHovered True if cursor currently hovers over widget bounds.
     * @param isActive True if widget is currently pressed/held by mouse or stylus.
     * @param outHoverAlpha Output normalized hover transition in [0.0, 1.0].
     * @param outActiveAlpha Output normalized press transition in [0.0, 1.0].
     */
    void GetInteractionAlphas(ImGuiID id, bool isHovered, bool isActive, float& outHoverAlpha, float& outActiveAlpha);

    /**
     * @brief Resets or teleports an animated property immediately without transition.
     *
     * @param key Unique property identifier.
     * @param value Exact value to snap to.
     */
    void Snap(const std::string& key, float value);

    /**
     * @brief Applies cubic easing to a normalized parameter $t \in [0.0, 1.0]$.
     */
    static float ApplyEasing(float t, EasingType type) noexcept;

private:
    UIAnimationManager() = default;
    ~UIAnimationManager() = default;

    float currentDeltaTime_ = 0.016f;
    uint64_t currentFrameCount_ = 0;

    std::unordered_map<std::string, TweenState> tweens_;
    std::unordered_map<std::string, SpringState> springs_;
    std::unordered_map<ImGuiID, WidgetInteractionState> widgetStates_;
};

} // namespace Folio::UI
