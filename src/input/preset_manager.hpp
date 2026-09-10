#pragma once
#include "imgui.h"
#include "input/pen_palette.hpp"
#include "app/settings_manager.hpp"
#include <string>
#include <vector>
#include <algorithm>

using ToolType = PenType;

/**
 * @brief Manages pen inking presets (Pen, Fountain, Pencil, Brush, Highlighter, Laser Pointer).
 * 
 * Provides functionality to retrieve, apply, create, delete, and reorder presets.
 * Operates directly on the reference arrays stored within SettingsManager for persistent storage.
 */
class PresetManager {
public:
    /// Reference to the global inking presets stored in SettingsManager
    std::vector<PenPreset>& presets;
    /// Reference to the currently active preset ID stored in SettingsManager
    std::string& activePresetId;
    /// Counter used to generate unique IDs for custom user-created presets
    int nextCustomId = 1;

    /**
     * @brief Constructs PresetManager, binding to SettingsManager singletons.
     * Initializes default pen presets if the loaded preset array is empty.
     */
    PresetManager()
        : presets(SettingsManager::Instance().inkingPresets),
          activePresetId(SettingsManager::Instance().activePresetId)
    {
        if (presets.empty()) {
            presets = SettingsManager::GetDefaultPenPresets();
            SettingsManager::Instance().Save();
        }
    }

    /**
     * @brief Loads preset configuration from settings (noop placeholder for interface symmetry).
     * @return Always true.
     */
    bool LoadFromSettings() {
        return true;
    }

    /**
     * @brief Persists the current preset state to disk via SettingsManager.
     */
    void SaveToSettings() {
        SettingsManager::Instance().Save();
    }

    /**
     * @brief Retrieves a pointer to the currently selected active PenPreset.
     * @return Pointer to active PenPreset, or the first preset if active ID not found, or nullptr if empty.
     */
    PenPreset* GetActivePreset() {
        for (auto& p : presets) {
            if (p.id == activePresetId) return &p;
        }
        if (!presets.empty()) return &presets[0];
        return nullptr;
    }

    /**
     * @brief Searches for a preset by its unique string ID.
     * @param id Unique identifier string of the target preset.
     * @return Pointer to matching PenPreset if found, or nullptr otherwise.
     */
    PenPreset* FindPreset(const std::string& id) {
        for (auto& p : presets) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    /**
     * @brief Applies a PenPreset's attributes onto an active PenTool state.
     * 
     * Configures pen type, base thickness, opacity, color (converted from normalized RGBA float
     * to Blend2D BLRgba32), cap type, blend mode, pressure/tilt sensitivity, flow simulation,
     * and stroke smoothing.
     * 
     * @param p The source preset to apply.
     * @param activePen The destination PenTool instance to mutate.
     */
    void ApplyPreset(const PenPreset& p, PenTool& activePen) {
        activePresetId = p.id;
        activePen.penType       = p.type;
        activePen.baseSize      = p.thicknessMm;
        activePen.opacity       = p.opacity;
        activePen.strokePattern = p.strokePattern;

        // Convert ImVec4 normalized (0.0 - 1.0) RGBA color values to Blend2D 8-bit unsigned integer channels
        uint8_t cr = static_cast<uint8_t>(std::clamp(p.color.x * 255.0f, 0.0f, 255.0f));
        uint8_t cg = static_cast<uint8_t>(std::clamp(p.color.y * 255.0f, 0.0f, 255.0f));
        uint8_t cb = static_cast<uint8_t>(std::clamp(p.color.z * 255.0f, 0.0f, 255.0f));
        uint8_t ca = static_cast<uint8_t>(std::clamp(p.color.w * 255.0f, 0.0f, 255.0f));
        activePen.color = BLRgba32(cr, cg, cb, ca);

        // Configure tool behavior defaults based on pen category
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

    /**
     * @brief Creates and appends a new custom preset of the specified PenType.
     * 
     * Auto-increments custom ID counters to prevent string ID collisions, applies default attributes
     * (name, color, thickness, opacity, stroke pattern), activates the newly created preset, and persists to disk.
     * 
     * @param type The type category of pen for the new preset.
     * @return String containing the unique ID assigned to the new preset.
     */
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

    /**
     * @brief Deletes a preset identified by string ID.
     * Guarantees at least one preset remains in the list. Updates the active preset ID if the deleted preset was active.
     * 
     * @param id Unique identifier of the preset to delete.
     */
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

    /**
     * @brief Reorders a preset from one index position to another in the preset array.
     * 
     * @param fromIdx Source zero-based index.
     * @param toIdx Destination zero-based index.
     */
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

