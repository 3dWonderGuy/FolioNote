#pragma once

#include "imgui.h"
#include "app/theme_manager.hpp"
#include "core/ink_engine/ink_engine.hpp"
#include "app/settings_manager.hpp"
#include <algorithm>
#include <cmath>

#define g_InkingConfig (SettingsManager::Instance().inkingConfig)

class InkingTuningOverlay {
public:
  bool isVisible = false;

  void Render(const ThemeManager& theme, Folio::InkEngine* inkEngine = nullptr) {
    if (!isVisible)
      return;

    ImGui::SetNextWindowSize(ImVec2(740, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(500, 100), ImGuiCond_FirstUseEver);

    OverlayThemeScope overlayScope(theme);
    ImGui::Begin("Handwriting Pipeline Studio (Millimeters) [F5]", &isVisible);

    if (ImGui::BeginTabBar("InkingPipelineStagesTabs",
                           ImGuiTabBarFlags_Reorderable)) {
      // TAB: GOOGLE INK STROKE MODELER
      if (ImGui::BeginTabItem("Google Ink Model")) {
        ImGui::TextColored(theme.colorPrimary,
                           "GOOGLE INK STROKE MODELER (PHYSICS ENGINE)");
        ImGui::Separator();

        ImGui::Text("Position Modeler");
        // spring_mass_constant defaults to 11 / 32400 ~= 0.000339
        ImGui::SliderFloat("Spring Mass Constant",
                           &g_InkingConfig.google_spring_mass_constant, 0.0001f,
                           0.005f, "%.6f");
        ImGui::SliderFloat("Drag Constant (Friction)",
                           &g_InkingConfig.google_drag_constant, 10.0f, 300.0f,
                           "%.1f");

        ImGui::Separator();
        ImGui::SeparatorText("Adaptive Sampling & Rotational Velocity");
        ImGui::SliderFloat("Baseline Output Rate",
                           &g_InkingConfig.google_min_output_rate, 60.0f,
                           600.0f, "%.0f Hz");
        ImGui::Checkbox("Enable Rotational Angle Upsampling",
                        &g_InkingConfig.google_enable_angle_upsampling);
        ImGui::BeginDisabled(!g_InkingConfig.google_enable_angle_upsampling);
        ImGui::SliderFloat("Max Turn Angle / Step",
                           &g_InkingConfig.google_max_estimated_angle_rad,
                           0.01f, 0.30f, "%.3f rad");
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::SeparatorText("Loop Contraction Mitigation (Speed Curve Accuracy)");
        ImGui::Checkbox("Enable Loop Contraction Mitigation",
                        &g_InkingConfig.google_loop_mitigation_enable);
        ImGui::BeginDisabled(!g_InkingConfig.google_loop_mitigation_enable);
        ImGui::SliderFloat("Slow Speed Bound",
                           &g_InkingConfig.google_loop_speed_lower_bound, 0.0f,
                           200.0f, "%.1f mm/s");
        ImGui::SliderFloat("Fast Speed Bound",
                           &g_InkingConfig.google_loop_speed_upper_bound, 50.0f,
                           1500.0f, "%.1f mm/s");
        ImGui::SliderFloat("Spring Weight @ Slow",
                           &g_InkingConfig.google_loop_strength_at_lower_speed,
                           0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Spring Weight @ Fast",
                           &g_InkingConfig.google_loop_strength_at_upper_speed,
                           0.0f, 1.0f, "%.2f (Lower = Hugs Input)");
        ImGui::SliderFloat("Speed Window",
                           &g_InkingConfig.google_loop_min_speed_window_s,
                           0.005f, 0.20f, "%.3f s");
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::SeparatorText("Stroke Prediction & Projection");
        ImGui::Checkbox("Enable Google Modeler Prediction",
                        &g_InkingConfig.google_enable_prediction);
        ImGui::Checkbox("Use Stroke Normal Projection",
                        &g_InkingConfig.google_use_stroke_normal_projection);

        ImGui::Separator();
        ImGui::SeparatorText("Wobble Smoother (Noise Filter)");
        ImGui::Checkbox("Enable Wobble Smoothing",
                        &g_InkingConfig.google_wobble_enable);
        ImGui::BeginDisabled(!g_InkingConfig.google_wobble_enable);
        ImGui::SliderFloat("Timeout", &g_InkingConfig.google_wobble_timeout_s,
                           0.01f, 0.2f, "%.3f s");
        ImGui::SliderFloat("Speed Floor",
                           &g_InkingConfig.google_wobble_speed_floor, 0.1f,
                           5.0f, "%.2f mm/s");
        ImGui::SliderFloat("Speed Ceiling",
                           &g_InkingConfig.google_wobble_speed_ceiling, 0.2f,
                           10.0f, "%.2f mm/s");
        ImGui::EndDisabled();

        ImGui::SeparatorText("Pressure Curve & Calibration");
        ImGui::Checkbox("Enable Pressure Curve",
                        &g_InkingConfig.enablePressureCurve);
        ImGui::BeginDisabled(!g_InkingConfig.enablePressureCurve);
        ImGui::SliderFloat("Pressure Feel (Gamma)",
                           &g_InkingConfig.pressureGamma, 0.3f, 3.0f,
                           "%.2f (1.0=Linear, <1=Firm, >1=Soft)");
        ImGui::SliderFloat("Pressure Deadzone",
                           &g_InkingConfig.pressureDeadzoneMin, 0.0f, 0.20f,
                           "%.3f");
        ImGui::SliderFloat("Pressure Saturation",
                           &g_InkingConfig.pressureSaturationMax, 0.50f, 1.0f,
                           "%.2f");
        ImGui::SliderFloat("Min Contact Floor",
                           &g_InkingConfig.minRelativeThicknessFraction, 0.01f, 0.50f,
                           "%.2f");
        ImGui::EndDisabled();

        ImGui::SeparatorText("Dynamic Width Mapping");
        ImGui::Checkbox("Map Pressure to Width",
                        &g_InkingConfig.usePressure);
        ImGui::Checkbox("Map Velocity to Width (Fountain Pen)",
                        &g_InkingConfig.useVelocity);
        ImGui::BeginDisabled(!g_InkingConfig.usePressure &&
                             !g_InkingConfig.useVelocity);
        ImGui::SliderFloat("Width Smoothing",
                           &g_InkingConfig.dynamicWidthSmoothing,
                           0.01f, 1.0f, "%.2f (Lower = Smoother)");
        ImGui::SliderFloat("Min Width %",
                           &g_InkingConfig.minWidthMultiplier, 0.05f,
                           1.0f, "%.2fx");
        ImGui::SliderFloat("Max Width %",
                           &g_InkingConfig.maxWidthMultiplier, 1.0f,
                           4.0f, "%.2fx");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!g_InkingConfig.useVelocity);
        ImGui::SliderFloat("Max Velocity Thinning",
                           &g_InkingConfig.velocityThinningMaxSpeed,
                           10.0f, 2000.0f, "%.1f mm/s");
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextColored(theme.colorPrimary, "PHYSICS PRESETS");
        if (ImGui::Button("1. Fast & Snappy (Less Lag)")) {
          g_InkingConfig.google_spring_mass_constant = 0.005f; // Tighter spring
          g_InkingConfig.google_drag_constant = 40.0f;         // Less friction
          g_InkingConfig.google_wobble_enable = true;
          g_InkingConfig.google_wobble_timeout_s = 0.02f;
        }
        ImGui::SameLine();
        if (ImGui::Button("2. Heavy & Smooth (More Lag)")) {
          g_InkingConfig.google_spring_mass_constant = 0.0001f; // Looser spring
          g_InkingConfig.google_drag_constant = 150.0f;         // High friction
          g_InkingConfig.google_wobble_enable = true;
          g_InkingConfig.google_wobble_timeout_s = 0.06f;
        }
        ImGui::SameLine();
        if (ImGui::Button("3. Balanced Default")) {
          g_InkingConfig.google_spring_mass_constant =
              11.0f / 32400.0f; // Default ~0.000339
          g_InkingConfig.google_drag_constant = 72.0f;
          g_InkingConfig.google_wobble_enable = true;
          g_InkingConfig.google_wobble_timeout_s = 0.04f;
        }
        ImGui::SameLine();
        if (ImGui::Button("4. Ultra-Snappy (Raw Feel)")) {
          g_InkingConfig.google_spring_mass_constant = 0.0001f; // Stable lower bound
          g_InkingConfig.google_drag_constant = 120.0f;           // High friction to prevent oscillation
          g_InkingConfig.google_wobble_enable = false;
          g_InkingConfig.google_enable_prediction = false;
        }

        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    if (inkEngine) {
      inkEngine->GetConfig() = g_InkingConfig;
    }

    ImGui::End();
  }
};