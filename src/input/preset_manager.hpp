#pragma once
#include "imgui.h"
#include "input/pen_palette.hpp"
#include "app/settings_manager.hpp"
#include <string>
#include <vector>
#include <algorithm>

using ToolType = PenType;

class PresetManager {
public:
    // Directly references SettingsManager's loaded inking presets and active preset ID
    std::vector<PenPreset>& presets;
    std::string& activePresetId;
    int nextCustomId = 1;

    PresetManager()
        : presets(SettingsManager::Instance().inkingPresets),
          activePresetId(SettingsManager::Instance().activePresetId)
    {
        if (presets.empty()) {
            presets = SettingsManager::GetDefaultPenPresets();
            SettingsManager::Instance().Save();
        }
    }

    bool LoadFromSettings() {
        return true;
    }

    void SaveToSettings() {
        SettingsManager::Instance().Save();
    }

    PenPreset* GetActivePreset() {
        for (auto& p : presets) {
            if (p.id == activePresetId) return &p;
        }
        if (!presets.empty()) return &presets[0];
        return nullptr;
    }

    PenPreset* FindPreset(const std::string& id) {
        for (auto& p : presets) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    void ApplyPreset(const PenPreset& p, PenTool& activePen) {
        activePresetId = p.id;
        activePen.penType       = p.type;
        activePen.baseSize      = p.thicknessMm;
        activePen.opacity       = p.opacity;
        activePen.strokePattern = p.strokePattern;

        uint8_t cr = static_cast<uint8_t>(std::clamp(p.color.x * 255.0f, 0.0f, 255.0f));
        uint8_t cg = static_cast<uint8_t>(std::clamp(p.color.y * 255.0f, 0.0f, 255.0f));
        uint8_t cb = static_cast<uint8_t>(std::clamp(p.color.z * 255.0f, 0.0f, 255.0f));
        uint8_t ca = static_cast<uint8_t>(std::clamp(p.color.w * 255.0f, 0.0f, 255.0f));
        activePen.color = BLRgba32(cr, cg, cb, ca);

        switch (p.type) {
        case PenType::Pen:
            activePen.capType = CapType::Round;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = false;
            activePen.isFlowSimEnabled = true;
            activePen.smoothing = 0.5f;
            break;

        case PenType::Fountain:
            activePen.capType = CapType::Chisel;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = true;
            activePen.isFlowSimEnabled = true;
            activePen.smoothing = 0.6f;
            break;

        case PenType::Pencil:
            activePen.capType = CapType::Round;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = true;
            activePen.isFlowSimEnabled = false;
            activePen.smoothing = 0.35f;
            break;

        case PenType::Brush:
            activePen.capType = CapType::Round;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = true;
            activePen.isFlowSimEnabled = true;
            activePen.smoothing = 0.7f;
            break;

        case PenType::Highlighter:
            activePen.capType = CapType::Chisel;
            activePen.blendMode = BlendMode::Multiply;
            activePen.isPressureEnabled = false;
            activePen.isTiltEnabled = false;
            activePen.isFlowSimEnabled = false;
            activePen.smoothing = 0.5f;
            break;

        case PenType::LaserPointer:
            activePen.capType = CapType::Round;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = false;
            activePen.isTiltEnabled = false;
            activePen.isFlowSimEnabled = false;
            activePen.smoothing = 0.85f;
            break;
        }

        SettingsManager::Instance().Save();
    }

    std::string AddPreset(PenType type) {
        int maxId = nextCustomId;
        for (const auto& p : presets) {
            if (p.id.rfind("custom_tool_", 0) == 0) {
                try {
                    int idNum = std::stoi(p.id.substr(12));
                    if (idNum >= maxId) maxId = idNum + 1;
                } catch (...) {}
            }
        }
        nextCustomId = maxId;

        std::string newId = "custom_tool_" + std::to_string(nextCustomId++);
        PenPreset p;
        p.id = newId;
        p.type = type;
        p.isCustomPreset = true;
        p.strokePattern = (type == PenType::Pencil) ? StrokePattern::TexturedPencil : StrokePattern::Solid;

        switch (type) {
        case PenType::Pen:
            p.name = "Custom Pen";
            p.color = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
            p.thicknessMm = 0.5f;
            p.opacity = 1.0f;
            break;
        case PenType::Fountain:
            p.name = "Fountain Pen";
            p.color = ImVec4(0.10f, 0.10f, 0.14f, 1.0f);
            p.thicknessMm = 1.8f;
            p.opacity = 1.0f;
            break;
        case PenType::Pencil:
            p.name = "Custom Pencil";
            p.color = ImVec4(0.30f, 0.30f, 0.35f, 1.0f);
            p.thicknessMm = 0.7f;
            p.opacity = 0.90f;
            break;
        case PenType::Brush:
            p.name = "Paintbrush";
            p.color = ImVec4(0.95f, 0.40f, 0.15f, 1.0f);
            p.thicknessMm = 3.0f;
            p.opacity = 0.85f;
            break;
        case PenType::Highlighter:
            p.name = "Custom High";
            p.color = ImVec4(1.0f, 0.45f, 0.75f, 1.0f); // Neon Pink
            p.thicknessMm = 5.0f;
            p.opacity = 0.55f;
            break;
        case PenType::LaserPointer:
            p.name = "Laser Pointer";
            p.color = ImVec4(1.0f, 0.15f, 0.25f, 1.0f);
            p.thicknessMm = 3.0f;
            p.opacity = 0.95f;
            break;
        }

        presets.push_back(p);
        activePresetId = newId;
        SettingsManager::Instance().Save();
        return newId;
    }

    void DeletePreset(const std::string& id) {
        if (presets.size() <= 1) {
            // Keep at least one pen preset active
            return;
        }
        presets.erase(
            std::remove_if(presets.begin(), presets.end(), [&](const PenPreset& p) {
                return p.id == id;
            }),
            presets.end()
        );
        if (activePresetId == id) {
            activePresetId = presets.empty() ? "" : presets[0].id;
        }
        SettingsManager::Instance().Save();
    }

    void ReorderPreset(int fromIdx, int toIdx) {
        if (fromIdx < 0 || fromIdx >= static_cast<int>(presets.size()) ||
            toIdx < 0 || toIdx >= static_cast<int>(presets.size()) || fromIdx == toIdx) {
            return;
        }
        PenPreset item = presets[fromIdx];
        presets.erase(presets.begin() + fromIdx);
        presets.insert(presets.begin() + toIdx, item);
        SettingsManager::Instance().Save();
    }
};
