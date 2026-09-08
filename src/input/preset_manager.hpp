#pragma once
#include "imgui.h"
#include "input/pen_palette.hpp"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

using ToolType = PenType;

struct PenPreset {
    std::string id;
    std::string name;
    PenType type;            // Pen, Fountain, Pencil, Brush, Highlighter, LaserPointer
    ImVec4 color;            // RGBA ink tint
    float thicknessMm;       // Baseline physical diameter (mm)
    float opacity;           // 0.0 - 1.0
    bool isCustomPreset;     // True if created via "+ Add"
};

class PresetManager {
public:
    std::vector<PenPreset> presets;
    std::string activePresetId = "pen_black";
    int nextCustomId = 1;

    PresetManager() {
        InitializeDefaults();
    }

    void InitializeDefaults() {
        presets.clear();

        // 1. Black Standard Pen (0.5mm)
        presets.push_back({
            "pen_black", "Black Pen", PenType::Pen,
            ImVec4(0.09f, 0.10f, 0.13f, 1.0f), 0.5f, 1.0f, false
        });

        // 2. Blue Gel Pen (0.5mm)
        presets.push_back({
            "pen_blue", "Blue Pen", PenType::Pen,
            ImVec4(0.10f, 0.34f, 0.86f, 1.0f), 0.5f, 1.0f, false
        });

        // 3. Red Annotation Pen (0.5mm)
        presets.push_back({
            "pen_red", "Red Pen", PenType::Pen,
            ImVec4(0.86f, 0.15f, 0.15f, 1.0f), 0.5f, 1.0f, false
        });

        // 4. 2B Textured Pencil (0.7mm)
        presets.push_back({
            "pencil_2b", "2B Pencil", PenType::Pencil,
            ImVec4(0.24f, 0.25f, 0.29f, 1.0f), 0.7f, 0.90f, false
        });

        // 5. Fountain Pen (1.5mm)
        presets.push_back({
            "cal_fountain", "Fountain Pen", PenType::Fountain,
            ImVec4(0.10f, 0.10f, 0.12f, 1.0f), 1.5f, 1.0f, false
        });

        // 6. Watercolor Brush (2.5mm)
        presets.push_back({
            "brush_purple", "Watercolor Brush", PenType::Brush,
            ImVec4(0.49f, 0.23f, 0.93f, 1.0f), 2.5f, 0.85f, false
        });

        // 7. Yellow Chisel Highlighter (4.5mm)
        presets.push_back({
            "high_yellow", "Yellow Highlighter", PenType::Highlighter,
            ImVec4(1.0f, 0.90f, 0.0f, 1.0f), 4.5f, 0.55f, false
        });

        // 8. Mint Green Highlighter (4.5mm)
        presets.push_back({
            "high_green", "Mint Highlighter", PenType::Highlighter,
            ImVec4(0.06f, 0.73f, 0.51f, 1.0f), 4.5f, 0.55f, false
        });

        // 9. Neon Red Laser Pointer (3.0mm)
        presets.push_back({
            "laser_pointer", "Laser Pointer", PenType::LaserPointer,
            ImVec4(1.0f, 0.15f, 0.25f, 1.0f), 3.0f, 0.95f, false
        });

        activePresetId = "pen_black";
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
        activePen.penType  = p.type;
        activePen.baseSize = p.thicknessMm;
        activePen.opacity  = p.opacity;

        uint8_t cr = static_cast<uint8_t>(std::clamp(p.color.x * 255.0f, 0.0f, 255.0f));
        uint8_t cg = static_cast<uint8_t>(std::clamp(p.color.y * 255.0f, 0.0f, 255.0f));
        uint8_t cb = static_cast<uint8_t>(std::clamp(p.color.z * 255.0f, 0.0f, 255.0f));
        uint8_t ca = static_cast<uint8_t>(std::clamp(p.color.w * 255.0f, 0.0f, 255.0f));
        activePen.color = BLRgba32(cr, cg, cb, ca);

        switch (p.type) {
        case PenType::Pen:
            activePen.capType = CapType::Round;
            activePen.strokePattern = StrokePattern::Solid;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = false;
            activePen.isFlowSimEnabled = false;
            activePen.smoothing = 0.5f;
            break;

        case PenType::Fountain:
            activePen.capType = CapType::Chisel;
            activePen.strokePattern = StrokePattern::Solid;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = true;
            activePen.isFlowSimEnabled = true;
            activePen.smoothing = 0.6f;
            break;

        case PenType::Pencil:
            activePen.capType = CapType::Round;
            activePen.strokePattern = StrokePattern::TexturedPencil;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = true;
            activePen.isFlowSimEnabled = false;
            activePen.smoothing = 0.35f;
            break;

        case PenType::Brush:
            activePen.capType = CapType::Round;
            activePen.strokePattern = StrokePattern::Solid;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = true;
            activePen.isTiltEnabled = true;
            activePen.isFlowSimEnabled = true;
            activePen.smoothing = 0.7f;
            break;

        case PenType::Highlighter:
            activePen.capType = CapType::Chisel;
            activePen.strokePattern = StrokePattern::Solid;
            activePen.blendMode = BlendMode::Multiply;
            activePen.isPressureEnabled = false;
            activePen.isTiltEnabled = false;
            activePen.isFlowSimEnabled = false;
            activePen.smoothing = 0.5f;
            break;

        case PenType::LaserPointer:
            activePen.capType = CapType::Round;
            activePen.strokePattern = StrokePattern::Solid;
            activePen.blendMode = BlendMode::Normal;
            activePen.isPressureEnabled = false;
            activePen.isTiltEnabled = false;
            activePen.isFlowSimEnabled = false;
            activePen.smoothing = 0.85f;
            break;
        }
    }

    std::string AddPreset(PenType type) {
        std::string newId = "custom_tool_" + std::to_string(nextCustomId++);
        PenPreset p;
        p.id = newId;
        p.type = type;
        p.isCustomPreset = true;

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
    }

    void ReorderPreset(int fromIdx, int toIdx) {
        if (fromIdx < 0 || fromIdx >= static_cast<int>(presets.size()) ||
            toIdx < 0 || toIdx >= static_cast<int>(presets.size()) || fromIdx == toIdx) {
            return;
        }
        PenPreset item = presets[fromIdx];
        presets.erase(presets.begin() + fromIdx);
        presets.insert(presets.begin() + toIdx, item);
    }
};
