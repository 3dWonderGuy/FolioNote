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

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Folio Physics Model Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestSpringDamperCriticallyDamped();
    TestDynamicEraserPhysicsModel();

    std::cout << "========================================" << std::endl;
    std::cout << "All Physics Model tests passed (100% OK)!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}

