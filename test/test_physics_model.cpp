/**
 * =========================================================================================
 * @file test_physics_model.cpp
 * @brief Unit tests for computational physics models and dynamic eraser simulation
 * =========================================================================================
 */

#include <cassert>
#include <iostream>
#include <cmath>
#include "utils/physics_model.hpp"
#include "core/ink_engine/stroke_width_evaluator.hpp"

using namespace Folio::Physics;

void TestSpringDamperCriticallyDamped() {
    std::cout << "[Test] Running SpringDamper1D critically damped convergence test..." << std::endl;

    SpringConfig cfg = SpringConfig::CriticallyDamped(5.0f, 1.0f);
    assert(std::abs(cfg.DampingRatio() - 1.0f) < 0.001f);

    SpringDamper1D spring(cfg, 0.0f, 0.0f);
    spring.SetTarget(10.0f);

    // Simulate for 1.0 second at 120 FPS (dt ~ 0.00833s)
    constexpr float dt = 1.0f / 120.0f;
    for (int i = 0; i < 120; ++i) {
        spring.Step(dt);
    }

    // After 1.0 second, a 5Hz critically damped system should have reached near target (within 1%)
    assert(std::abs(spring.GetPosition() - 10.0f) < 0.1f);
    assert(spring.GetPosition() <= 10.05f); // No significant overshoot for zeta = 1.0

    std::cout << "[Test] SpringDamper1D convergence test passed! Final position: "
              << spring.GetPosition() << std::endl;
}

void TestDynamicEraserPhysicsModel() {
    std::cout << "[Test] Running DynamicEraserPhysicsModel velocity simulation test..." << std::endl;

    DynamicEraserPhysicsModel::Parameters params;
    params.minRadiusMm = 1.5f;
    params.maxRadiusMm = 16.0f;
    params.minSpeedPxPerSec = 100.0f;
    params.maxSpeedPxPerSec = 2000.0f;

    DynamicEraserPhysicsModel model(params);
    assert(std::abs(model.GetRadius() - 1.5f) < 0.001f);

    // 1. Stationary / slow pointer: stays at minimum radius
    constexpr float dt = 1.0f / 120.0f;
    for (int i = 0; i < 60; ++i) {
        model.Update(50.0f, dt); // Below 100 px/sec floor
    }
    assert(std::abs(model.GetRadius() - 1.5f) < 0.05f);

    // 2. High-speed scrubbing (2000 px/sec): expands toward 16.0mm
    for (int i = 0; i < 120; ++i) {
        model.Update(2000.0f, dt);
    }
    assert(model.GetRadius() > 14.0f);
    assert(model.GetRadius() <= 16.0f);

    // 3. Deceleration to 0 px/sec: smoothly contracts back toward 1.5mm
    for (int i = 0; i < 240; ++i) {
        model.Update(0.0f, dt);
    }
    assert(model.GetRadius() < 2.0f);
    assert(model.GetRadius() >= 1.5f);

    // 4. Hard bounds preservation
    for (int i = 0; i < 60; ++i) {
        model.Update(50000.0f, dt); // Extreme speed
    }
    assert(model.GetRadius() <= 16.0f);

    std::cout << "[Test] DynamicEraserPhysicsModel velocity simulation test passed!" << std::endl;
}

void TestStylusTiltDynamics() {
    std::cout << "[Test] Running Stylus Tilt & Pencil Shading Dynamics test..." << std::endl;

    Folio::InkingConfig config;
    config.useTilt = true;
    config.usePressure = false; // Isolate tilt
    config.useVelocity = false;
    config.dynamicWidthSmoothing = 1.0f; // Instant for unit test
    config.maxTiltWidthMultiplier = 2.5f;
    config.tiltDeadzoneRad = 0.20f;
    config.tiltMaxAngleRad = 1.10f;

    const float baseWidth = 0.7f; // 2B Pencil baseline

    // 1. Upright stylus (tilt = 0): no expansion
    float wUpright = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wUpright - baseWidth) < 1e-4f);

    // 2. Stylus within deadzone (tilt = 0.15 rad < 0.20 rad): stays at base width
    float wDeadzone = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 0.15f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wDeadzone - baseWidth) < 1e-4f);

    // 3. Stylus tilted for shading (tilt = 0.65 rad ~ mid-shading): smooth expansion
    float wMidShading = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 0.65f, 0.0f, -1.0f, 1.0f, config);
    assert(wMidShading > baseWidth);
    assert(wMidShading < baseWidth * config.maxTiltWidthMultiplier);

    // 4. Stylus at maximum tilt (tilt = 1.20 rad > 1.10 rad ceiling): clamped to maxTiltWidthMultiplier
    float wMaxTilt = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 1.20f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wMaxTilt - (baseWidth * config.maxTiltWidthMultiplier)) < 1e-4f);

    // 5. Stylus with useTilt disabled: tilt angle does not affect width
    config.useTilt = false;
    float wNoTilt = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 1.20f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wNoTilt - baseWidth) < 1e-4f);

    std::cout << "[Test] Stylus Tilt & Pencil Shading Dynamics test passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Folio Physics Model Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestSpringDamperCriticallyDamped();
    TestDynamicEraserPhysicsModel();
    TestStylusTiltDynamics();

    std::cout << "========================================" << std::endl;
    std::cout << "All Physics Model tests passed (100% OK)!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}

