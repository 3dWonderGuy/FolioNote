#pragma once
#include "imgui.h"
#include "ui/imgui_theme.hpp"

struct NotebookHubView {
    void Render(float x, float y, float width, float height, AppViewMode& outViewMode) {
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));

        ImGui::Begin("##NotebookHubWindow", nullptr, 
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        // 1. Back Navigation Button
        if (ImGui::Button("< Back to Canvas", ImVec2(180, 44))) {
            outViewMode = AppViewMode::CanvasWorkspace;
        }

        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        // 2. Hub Header Typography
        ImGui::TextDisabled("FOLIONOTE NOTEBOOK MANAGER");
        
        ImGui::PushFont(FolioTheme::FontRibbonBoldLarge); // Updated identifier
        ImGui::Text("My Notebooks");
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        // 3. Main Action Buttons
        if (ImGui::Button("+ New Notebook", ImVec2(220, 48))) {
            // Future database create trigger
        }
        ImGui::SameLine(0, 16.0f);
        if (ImGui::Button("Open Existing (.uade)", ImVec2(220, 48))) {
            // Future file dialog trigger
        }

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));

        // 4. Notebook Selection Grid
        const char* sampleNotebooks[] = {
            "ELE 210 Electronics - Sem I",
            "MATH 363 Differential Equations",
            "PHYS 283 Quantum Mechanics",
            "Embedded Systems & Robotics"
        };

        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            if (ImGui::Button(sampleNotebooks[i], ImVec2(360, 120))) {
                outViewMode = AppViewMode::CanvasWorkspace;
            }

            // Two-column responsive card row
            if (i % 2 == 0) {
                ImGui::SameLine(0, 24.0f);
            } else {
                ImGui::Dummy(ImVec2(0.0f, 12.0f));
            }

            ImGui::PopID();
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
};