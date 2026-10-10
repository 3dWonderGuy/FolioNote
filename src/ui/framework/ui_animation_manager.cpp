/**
 * =========================================================================================
 * @file ui/framework/ui_animation_manager.cpp
 * @brief Implementation of the UI Animation & Physics Engine
 * =========================================================================================
 */

#include "ui/framework/ui_animation_manager.hpp"

namespace Folio::UI {

UIAnimationManager& UIAnimationManager::Instance() {
    static UIAnimationManager instance;
    return instance;
}

void UIAnimationManager::Update(float deltaTime) {
    // Clamp delta time to avoid physics explosion during debugging pauses or window freezes
    currentDeltaTime_ = std::clamp(deltaTime, 0.001f, 0.1f);
    currentFrameCount_++;

    // Prune stale widget interaction states older than 300 frames (~2.5 seconds @ 120Hz)
    if ((currentFrameCount_ % 120) == 0) {
        for (auto it = widgetStates_.begin(); it != widgetStates_.end(); ) {
            if (currentFrameCount_ - it->second.lastFrameActive > 300) {
                it = widgetStates_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

float UIAnimationManager::GetFloat(const std::string& key, float target, float speed) {
    auto& tw = tweens_[key];
    if (!tw.initialized) {
        tw.current = target;
        tw.target = target;
        tw.initialized = true;
        return target;
    }

    tw.target = target;

    // Frame-rate independent exponential decay:
    // x(t + dt) = target + (current - target) * exp(-speed * dt)
    float decayFactor = std::exp(-speed * currentDeltaTime_);
    tw.current = target + (tw.current - target) * decayFactor;

    // Snap if extremely close to prevent continuous micro-updates
    if (std::abs(tw.current - target) < 0.0005f) {
        tw.current = target;
    }

    return tw.current;
}

float UIAnimationManager::GetSpring(const std::string& key, float target, float stiffness, float damping) {
    auto& sp = springs_[key];
    if (!sp.initialized) {
        sp.position = target;
        sp.velocity = 0.0f;
        sp.target = target;
        sp.initialized = true;
        return target;
    }

    sp.target = target;

    // Sub-stepped semi-implicit Euler integration for numerical stability
    // 2 sub-steps ensure stability even if frame rate drops to 30-60 FPS
    const int subSteps = 2;
    float dt = currentDeltaTime_ / static_cast<float>(subSteps);

    for (int i = 0; i < subSteps; ++i) {
        float displacement = sp.position - sp.target;
        // Hooke's Law with damping: F = -k * x - c * v
        float springForce = -stiffness * displacement;
        float dampingForce = -damping * sp.velocity;
        float acceleration = springForce + dampingForce; // Assuming mass m = 1.0 kg

        sp.velocity += acceleration * dt;
        sp.position += sp.velocity * dt;
    }

    // Snap to rest when displacement and velocity are below perceptual threshold
    if (std::abs(sp.position - sp.target) < 0.001f && std::abs(sp.velocity) < 0.01f) {
        sp.position = sp.target;
        sp.velocity = 0.0f;
    }

    return sp.position;
}

void UIAnimationManager::GetInteractionAlphas(ImGuiID id, bool isHovered, bool isActive, float& outHoverAlpha, float& outActiveAlpha) {
    auto& state = widgetStates_[id];
    state.lastFrameActive = currentFrameCount_;

    // Asymmetric hover speed: fade in quickly (14/s), fade out slightly slower (9/s)
    float hoverSpeed = isHovered ? 14.0f : 9.0f;
    float targetHover = isHovered ? 1.0f : 0.0f;
    float hoverDecay = std::exp(-hoverSpeed * currentDeltaTime_);
    state.hoverAlpha = targetHover + (state.hoverAlpha - targetHover) * hoverDecay;

    // Asymmetric active speed: press in instantly (26/s), release smoothly (12/s)
    float activeSpeed = isActive ? 26.0f : 12.0f;
    float targetActive = isActive ? 1.0f : 0.0f;
    float activeDecay = std::exp(-activeSpeed * currentDeltaTime_);
    state.activeAlpha = targetActive + (state.activeAlpha - targetActive) * activeDecay;

    // Clamping to guaranteed [0.0, 1.0] range
    outHoverAlpha = std::clamp(state.hoverAlpha, 0.0f, 1.0f);
    outActiveAlpha = std::clamp(state.activeAlpha, 0.0f, 1.0f);
}

void UIAnimationManager::Snap(const std::string& key, float value) {
    if (tweens_.find(key) != tweens_.end()) {
        tweens_[key].current = value;
        tweens_[key].target = value;
    }
    if (springs_.find(key) != springs_.end()) {
        springs_[key].position = value;
        springs_[key].target = value;
        springs_[key].velocity = 0.0f;
    }
}

float UIAnimationManager::ApplyEasing(float t, EasingType type) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (type) {
        case EasingType::Linear:
            return t;
        case EasingType::EaseInQuad:
            return t * t;
        case EasingType::EaseOutQuad:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case EasingType::EaseInOutCubic:
            return (t < 0.5f) ? (4.0f * t * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f);
        case EasingType::EaseOutBack: {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        }
        default:
            return t;
    }
}

} // namespace Folio::UI
