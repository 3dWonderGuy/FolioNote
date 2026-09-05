#pragma once

#include "imgui.h"
#include <algorithm>
#include <cmath>

enum class SplineMode {
  QuadraticMidpoint,
  CentripetalCatmullRom,
  LinearPolylines
};

enum class StylusActionBinding {
  EraserTool,
  LassoTool,
  PanCanvas,
  UndoAction,
  RedoAction,
  None
};

struct InkingTuningConfig {
  // -------------------------------------------------------------
  // PRESSURE CONFIGURATION
  // -------------------------------------------------------------
  bool enablePressureCurve = true;
  float pressureGamma = 1.0f;
  float pressureDeadzoneMin = 0.02f;
  float pressureSaturationMax = 0.95f;
  float minRelativeThicknessFraction = 0.15f;

  // -------------------------------------------------------------
  // HARDWARE BINDINGS
  // -------------------------------------------------------------
  float manualOffsetXMm = 0.0f;
  float manualOffsetYMm = 0.0f;

  StylusActionBinding barrelButton1 = StylusActionBinding::EraserTool;
  StylusActionBinding barrelButton2 = StylusActionBinding::LassoTool;
  StylusActionBinding tailEraserTip = StylusActionBinding::EraserTool;

  // -------------------------------------------------------------
  // GOOGLE INK STROKE MODELER PARAMS
  // -------------------------------------------------------------
  float google_spring_mass_constant = 11.0f / 32400.0f; // ~0.000339
  float google_drag_constant = 72.0f;
  bool google_wobble_enable = true;
  float google_wobble_timeout_s = 0.04f;
  float google_wobble_speed_floor = 1.31f;
  float google_wobble_speed_ceiling = 1.44f;
  bool google_enable_prediction = true;

  // Google Dynamic Width Params
  bool google_use_pressure = true;
  bool google_use_velocity = false;
  float google_min_width_multiplier = 0.2f;
  float google_max_width_multiplier = 1.5f;
  float google_velocity_thinning_max_speed = 1500.0f; // mm/s
  float google_dynamic_width_smoothing = 0.6f;      // EMA factor (0.0 to 1.0)

  float EvaluatePressure(float rawPressure) const {
    if (!enablePressureCurve)
      return 1.0f;
    if (rawPressure <= pressureDeadzoneMin)
      return minRelativeThicknessFraction;
    if (rawPressure >= pressureSaturationMax)
      return 1.0f;

    float norm = (rawPressure - pressureDeadzoneMin) /
                 (pressureSaturationMax - pressureDeadzoneMin);
    float curved = std::pow(norm, pressureGamma);
    return minRelativeThicknessFraction +
           (1.0f - minRelativeThicknessFraction) *
               std::clamp(curved, 0.0f, 1.0f);
  }
};

inline InkingTuningConfig g_InkingConfig;

class InkingTuningOverlay {
public:
  bool isVisible = true;

  void Render() {
    if (!isVisible)
      return;

    ImGui::SetNextWindowSize(ImVec2(740, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(500, 100), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
    ImGui::Begin("Handwriting Pipeline Studio (Millimeters) [F5]", &isVisible);

    if (ImGui::BeginTabBar("InkingPipelineStagesTabs",
                           ImGuiTabBarFlags_Reorderable)) {
      // TAB: GOOGLE INK STROKE MODELER
      if (ImGui::BeginTabItem("Google Ink Model")) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f),
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
        ImGui::SeparatorText("Stroke Prediction");
        ImGui::Checkbox("Enable Google Modeler Prediction",
                        &g_InkingConfig.google_enable_prediction);

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

        ImGui::SeparatorText("Dynamic Width Mapping");
        ImGui::Checkbox("Map Pressure to Width",
                        &g_InkingConfig.google_use_pressure);
        ImGui::Checkbox("Map Velocity to Width (Fountain Pen)",
                        &g_InkingConfig.google_use_velocity);
        ImGui::BeginDisabled(!g_InkingConfig.google_use_pressure &&
                             !g_InkingConfig.google_use_velocity);
        ImGui::SliderFloat("Width Smoothing",
                           &g_InkingConfig.google_dynamic_width_smoothing,
                           0.01f, 1.0f, "%.2f (Lower = Smoother)");
        ImGui::SliderFloat("Min Width %",
                           &g_InkingConfig.google_min_width_multiplier, 0.05f,
                           1.0f, "%.2fx");
        ImGui::SliderFloat("Max Width %",
                           &g_InkingConfig.google_max_width_multiplier, 1.0f,
                           4.0f, "%.2fx");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!g_InkingConfig.google_use_velocity);
        ImGui::SliderFloat("Max Velocity Thinning",
                           &g_InkingConfig.google_velocity_thinning_max_speed,
                           10.0f, 2000.0f, "%.1f mm/s");
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "PHYSICS PRESETS");
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
    ImGui::End();
    ImGui::PopStyleColor();
  }
};