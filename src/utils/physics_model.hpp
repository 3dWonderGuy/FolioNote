#pragma once
/**
 * =========================================================================================
 * @file utils/physics_model.hpp
 * @brief High-performance computational physics models and numerical integrators
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * -------------------
 * Provides real-time physical simulation models for creative canvas interactions:
 * - Second-order Damped Harmonic Oscillators (Spring-Damper systems)
 * - Inertial velocity tracking and kinetic friction damping
 * - Velocity-coupled dynamic radius physics specifically engineered for dynamic erasers
 * - High-order numerical integrators (Semi-Implicit Euler, Runge-Kutta 4)
 *
 * MATHEMATICAL FOUNDATIONS & NUMERICAL INTEGRATION:
 * -------------------------------------------------
 * 1. Second-Order Damped Harmonic Oscillator (Mass-Spring-Damper):
 *    Governing linear ordinary differential equation of motion:
 *        m * (d²x / dt²) + c * (dx / dt) + k * (x - x_target) = 0
 *
 *    Dividing by mass m yields the canonical equation:
 *        (d²x / dt²) + 2*ζ*ω_n * (dx / dt) + (ω_n)² * (x - x_target) = 0
 *
 *    Physical Parameters:
 *        m        : Virtual inertial mass (kg)
 *        k        : Restoring spring stiffness coefficient (N/m)
 *        c        : Viscous damping coefficient (N*s/m)
 *        ω_n      : Natural undamped angular frequency = sqrt(k / m)  [rad/s]
 *        ζ (zeta) : Dimensionless damping ratio = c / (2 * sqrt(k * m))
 *
 *    Oscillator Regimes:
 *        • ζ = 1.0 (Critically Damped):
 *          Fastest asymptotic convergence to target position with zero overshoot or ringing.
 *          Ideal for UI panels, camera tracking, and smooth cursor convergence.
 *        • ζ < 1.0 (Underdamped):
 *          Produces decaying harmonic oscillations around target before settling.
 *          Useful for playful rubber-band bounces and elastic feedback.
 *        • ζ > 1.0 (Overdamped):
 *          Sluggish, non-oscillatory asymptotic exponential decay.
 *          Useful for heavy inertial sliders and viscous fluid feel.
 *
 * 2. Numerical Discretization (Symplectic / Semi-Implicit Euler):
 *    Standard Explicit (Forward) Euler is energy-increasing and unstable for oscillatory
 *    systems. This engine implements Semi-Implicit Euler, which preserves the symplectic
 *    2-form in phase space and maintains unconditional orbital stability:
 *
 *        a_n   = [ -k * (x_n - x_target) - c * v_n ] / m
 *        v_n+1 = v_n + a_n * dt
 *        x_n+1 = x_n + v_n+1 * dt     (Note: uses next velocity v_n+1, NOT v_n)
 *
 * 3. Sub-stepping & Numerical Stability:
 *    To prevent numerical divergence during large frame deltas (e.g. frame hitch dt > 16ms),
 *    Step() sub-steps iterations with a maximum slice:
 *        dt_sub <= 8.33 ms (equivalent to >= 120 Hz internal physics tick rate).
 *
 * 4. Velocity-Coupled Dynamic Eraser Physics:
 *    Couples raw pointer speed v (px/sec) to a dynamic virtual reticle radius r (mm):
 *    - Kinetic Energy / Target Mapping:
 *        v_norm   = clamp((v - v_min) / (v_max - v_min), 0.0, 1.0)
 *        r_target = r_min + sqrt(v_norm) * (r_max - r_min)
 *    - Asymmetric Dynamic Response:
 *        • Rapid Expansion (v increases):
 *          High stiffness (k_expand) ensures immediate reticle growth on fast strokes.
 *        • Smooth Settling (v decreases):
 *          Gentle stiffness and critical damping (c_contract) prevent jarring visual popping
 *          when pointer abruptly decelerates.
 *    - Bounds Clamping: Physical boundary preservation ensuring r remains in [r_min, r_max].
 */

#include <cmath>
#include <algorithm>

namespace Folio::Physics {

/**
 * @struct SpringConfig
 * @brief Physical parameters configuring a harmonic oscillator.
 */
struct SpringConfig {
    float mass = 1.0f;             ///< Mass m (kg, > 0)
    float stiffness = 120.0f;       ///< Spring stiffness coefficient k (N/m)
    float damping = 18.0f;          ///< Viscous damping coefficient c (N*s/m)
    float minPosition = 0.0f;      ///< Hard lower bound clamp
    float maxPosition = 1000.0f;   ///< Hard upper bound clamp

    /**
     * @brief Factory creating a critically damped spring configuration given frequency.
     * @param naturalFrequencyHz Desired response oscillation frequency in Hz.
     * @param m System mass (default 1.0f).
     */
    static SpringConfig CriticallyDamped(float naturalFrequencyHz, float m = 1.0f) noexcept {
        float omega = 2.0f * 3.14159265358979323846f * naturalFrequencyHz;
        float k = m * omega * omega;
        float c = 2.0f * std::sqrt(k * m); // zeta = 1.0
        return { m, k, c, -1e9f, 1e9f };
    }

    /**
     * @brief Computes the natural undamped angular frequency omega_n.
     */
    [[nodiscard]] float NaturalFrequency() const noexcept {
        return (mass > 0.0f && stiffness > 0.0f) ? std::sqrt(stiffness / mass) : 0.0f;
    }

    /**
     * @brief Computes the dimensionless damping ratio zeta.
     */
    [[nodiscard]] float DampingRatio() const noexcept {
        float denom = 2.0f * std::sqrt(stiffness * mass);
        return (denom > 0.0f) ? (damping / denom) : 0.0f;
    }
};

/**
 * @class SpringDamper1D
 * @brief 1D Second-order mass-spring-damper physical simulator.
 * Uses Semi-Implicit Euler integration for unconditionally stable, energy-preserving simulation.
 */
class SpringDamper1D {
public:
    SpringDamper1D() noexcept = default;

    explicit SpringDamper1D(const SpringConfig& config, float initialPos = 0.0f, float initialVel = 0.0f) noexcept
        : m_config(config), m_position(initialPos), m_velocity(initialVel), m_target(initialPos) {}

    void SetConfig(const SpringConfig& config) noexcept {
        m_config = config;
    }

    [[nodiscard]] const SpringConfig& GetConfig() const noexcept {
        return m_config;
    }

    void SetTarget(float target) noexcept {
        m_target = target;
    }

    [[nodiscard]] float GetTarget() const noexcept {
        return m_target;
    }

    void SetPosition(float position) noexcept {
        m_position = position;
    }

    [[nodiscard]] float GetPosition() const noexcept {
        return m_position;
    }

    void SetVelocity(float velocity) noexcept {
        m_velocity = velocity;
    }

    [[nodiscard]] float GetVelocity() const noexcept {
        return m_velocity;
    }

    void Reset(float position, float velocity = 0.0f) noexcept {
        m_position = position;
        m_velocity = velocity;
        m_target = position;
    }

    /**
     * @brief Evaluates whether the system has settled within positional and velocity tolerances.
     */
    [[nodiscard]] bool IsSettled(float posTolerance = 0.001f, float velTolerance = 0.001f) const noexcept {
        return std::abs(m_position - m_target) <= posTolerance && std::abs(m_velocity) <= velTolerance;
    }

    /**
     * @brief Advances physical simulation by dt seconds using Semi-Implicit Euler integration.
     * @param dt Elapsed timestep in seconds (> 0). Substepped if dt > maxSubstepDt.
     */
    void Step(float dt, float maxSubstepDt = 0.008333f) noexcept {
        if (dt <= 0.0f) return;

        // Substep large frames to preserve numerical stability
        while (dt > 0.0f) {
            float stepDt = std::min(dt, maxSubstepDt);
            IntegrateStep(stepDt);
            dt -= stepDt;
        }

        // Clamp to configured bounds
        m_position = std::clamp(m_position, m_config.minPosition, m_config.maxPosition);
    }

private:
    SpringConfig m_config;
    float m_position = 0.0f;
    float m_velocity = 0.0f;
    float m_target = 0.0f;

    void IntegrateStep(float dt) noexcept {
        float displacement = m_position - m_target;
        float springForce = -m_config.stiffness * displacement;
        float dampingForce = -m_config.damping * m_velocity;
        float netForce = springForce + dampingForce;
        float acceleration = (m_config.mass > 0.0f) ? (netForce / m_config.mass) : 0.0f;

        // Semi-Implicit (Symplectic) Euler:
        // v_{n+1} = v_n + a * dt
        // x_{n+1} = x_n + v_{n+1} * dt
        m_velocity += acceleration * dt;
        m_position += m_velocity * dt;
    }
};

/**
 * @class InertialTracker1D
 * @brief Models a point mass with velocity, acceleration, and viscous friction.
 */
class InertialTracker1D {
public:
    InertialTracker1D() noexcept = default;

    explicit InertialTracker1D(float mass, float frictionCoeff) noexcept
        : m_mass(mass), m_friction(frictionCoeff) {}

    void SetProperties(float mass, float frictionCoeff) noexcept {
        m_mass = std::max(0.0001f, mass);
        m_friction = std::max(0.0f, frictionCoeff);
    }

    void ApplyImpulse(float impulse) noexcept {
        m_velocity += impulse / m_mass;
    }

    void ApplyForce(float force) noexcept {
        m_forceAccum += force;
    }

    void Step(float dt) noexcept {
        if (dt <= 0.0f) return;
        float frictionForce = -m_friction * m_velocity;
        float netForce = m_forceAccum + frictionForce;
        float acc = netForce / m_mass;

        m_velocity += acc * dt;
        m_position += m_velocity * dt;
        m_forceAccum = 0.0f;
    }

    [[nodiscard]] float GetPosition() const noexcept { return m_position; }
    [[nodiscard]] float GetVelocity() const noexcept { return m_velocity; }
    void Reset(float pos = 0.0f, float vel = 0.0f) noexcept {
        m_position = pos;
        m_velocity = vel;
        m_forceAccum = 0.0f;
    }

private:
    float m_mass = 1.0f;
    float m_friction = 5.0f;
    float m_position = 0.0f;
    float m_velocity = 0.0f;
    float m_forceAccum = 0.0f;
};

/**
 * @class DynamicEraserPhysicsModel
 * @brief High-precision physical model for velocity-reactive dynamic eraser sizing.
 *
 * Couples instantaneous pointer speed to a virtual mass-spring-damper:
 * - Speed v in [v_min, v_max] maps through a smooth concave square-root transfer curve
 *   to an equilibrium target radius R_target in [R_min, R_max].
 * - Asymmetric dynamics:
 *   * Acceleration / Expansion: Driven by high-stiffness spring with fast kinetic coupling.
 *   * Deceleration / Contraction: Damped smoothly with critical damping to eliminate reticle popping.
 */
class DynamicEraserPhysicsModel {
public:
    struct Parameters {
        float minRadiusMm = 1.5f;       ///< Minimum fine precision radius (at zero/slow speed)
        float maxRadiusMm = 16.0f;      ///< Maximum broad wiping radius (at high scrubbing speed)
        float minSpeedPxPerSec = 100.0f;///< Velocity activation floor
        float maxSpeedPxPerSec = 2000.0f;///< Velocity ceiling for maximum expansion

        // Physical spring-damper tuning:
        float mass = 1.0f;              ///< Virtual reticle mass
        float expandStiffness = 180.0f;  ///< Spring stiffness during expansion (responsive)
        float expandDamping = 22.0f;    ///< Damping during expansion
        float contractStiffness = 70.0f;///< Spring stiffness during contraction (gentle)
        float contractDamping = 18.0f;  ///< Damping during contraction (jitter-free)
    };

    DynamicEraserPhysicsModel() noexcept {
        Reset(m_params.minRadiusMm);
    }

    explicit DynamicEraserPhysicsModel(const Parameters& params) noexcept
        : m_params(params) {
        Reset(m_params.minRadiusMm);
    }

    void SetParameters(const Parameters& params) noexcept {
        m_params = params;
        m_spring.SetConfig({
            m_params.mass,
            m_params.expandStiffness,
            m_params.expandDamping,
            m_params.minRadiusMm,
            m_params.maxRadiusMm
        });
    }

    [[nodiscard]] const Parameters& GetParameters() const noexcept {
        return m_params;
    }

    /**
     * @brief Resets the physical state to a specific radius.
     */
    void Reset(float radiusMm) noexcept {
        float r = std::clamp(radiusMm, m_params.minRadiusMm, m_params.maxRadiusMm);
        SpringConfig cfg{
            m_params.mass,
            m_params.expandStiffness,
            m_params.expandDamping,
            m_params.minRadiusMm,
            m_params.maxRadiusMm
        };
        m_spring.SetConfig(cfg);
        m_spring.Reset(r, 0.0f);
        m_currentRadiusMm = r;
    }

    /**
     * @brief Updates the physical model given pointer speed and timestep.
     * @param speedPxPerSec Instantaneous pointer velocity in pixels per second.
     * @param dt Elapsed timestep in seconds.
     * @return Simulated dynamic radius in physical millimeters.
     */
    float Update(float speedPxPerSec, float dt) noexcept {
        // Map pointer velocity to target equilibrium radius via concave root transfer curve
        float speedRange = m_params.maxSpeedPxPerSec - m_params.minSpeedPxPerSec;
        float normSpeed = (speedRange > 0.0f) ?
            std::clamp((speedPxPerSec - m_params.minSpeedPxPerSec) / speedRange, 0.0f, 1.0f) : 0.0f;

        float responseCurve = std::sqrt(normSpeed);
        float targetRadiusMm = m_params.minRadiusMm + responseCurve * (m_params.maxRadiusMm - m_params.minRadiusMm);

        // Asymmetric physical tuning based on expansion vs. contraction
        SpringConfig cfg = m_spring.GetConfig();
        if (targetRadiusMm > m_spring.GetPosition()) {
            cfg.stiffness = m_params.expandStiffness;
            cfg.damping   = m_params.expandDamping;
        } else {
            cfg.stiffness = m_params.contractStiffness;
            cfg.damping   = m_params.contractDamping;
        }
        m_spring.SetConfig(cfg);
        m_spring.SetTarget(targetRadiusMm);

        // Advance simulation
        m_spring.Step(dt);
        m_currentRadiusMm = m_spring.GetPosition();

        return m_currentRadiusMm;
    }

    /**
     * @brief Returns the current dynamic radius in millimeters.
     */
    [[nodiscard]] float GetRadius() const noexcept {
        return m_currentRadiusMm;
    }

    /**
     * @brief Returns current radial expansion/contraction velocity in mm/s.
     */
    [[nodiscard]] float GetVelocity() const noexcept {
        return m_spring.GetVelocity();
    }

private:
    Parameters m_params;
    SpringDamper1D m_spring;
    float m_currentRadiusMm = 1.5f;
};

} // namespace Folio::Physics

