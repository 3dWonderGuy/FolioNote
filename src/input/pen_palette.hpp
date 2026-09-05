#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <blend2d/blend2d.h>

enum class BlendMode : uint8_t {
    Normal = 0,
    Multiply,
    Additive
};

enum class CapType : uint8_t {
    Round = 0,
    Flat,
    Chisel,
    Square
};

enum class StrokePattern : uint8_t {
    Solid = 0,
    Dashed,
    Dotted,
    TexturedPencil
};

enum class PenType : uint8_t {
    Pen = 0,
    Fountain,
    Pencil,
    Brush,
    Highlighter,
    LaserPointer
};

struct PenTool {
    PenType       penType           = PenType::Pen;
    CapType       capType           = CapType::Round;
    StrokePattern strokePattern     = StrokePattern::Solid;
    BlendMode     blendMode         = BlendMode::Normal;
    float     baseSize          = 0.5f; // Baseline in physical millimeters (0.5 mm)
    float     minSizeFactor     = 0.2f;
    float     maxSizeFactor     = 1.8f;
    
    BLRgba32  color             = BLRgba32(0xFF, 0xFF, 0xFF, 0xFF);
    float     opacity           = 1.0f;
    float     smoothing         = 0.5f;
    bool      isPressureEnabled = true;
    bool      isTiltEnabled     = true;
    bool      isFlowSimEnabled  = false;
};

class PenPalette {
public:
    std::vector<PenTool> pens;
    size_t activePenIndex = 0;

    PenPalette() {
        InitializeDefaults();
    }

    void InitializeDefaults() {
        pens.clear();

        // 0: High-Contrast White 0.5mm Pen
        PenTool whitePen;
        whitePen.penType = PenType::Pen;
        whitePen.baseSize = 0.5f; // 0.5 mm
        whitePen.color = BLRgba32(0xFF, 0xFF, 0xFF, 0xFF);
        pens.push_back(whitePen);

        // 1: Vibrant Blue 0.5mm Gel Pen
        PenTool bluePen;
        bluePen.penType = PenType::Pen;
        bluePen.baseSize = 0.5f; // 0.5 mm
        bluePen.color = BLRgba32(0x33, 0x99, 0xFF, 0xFF);
        pens.push_back(bluePen);

        // 2: Yellow Chisel 4.5mm Highlighter
        PenTool highlighter;
        highlighter.penType = PenType::Highlighter;
        highlighter.capType = CapType::Chisel;
        highlighter.blendMode = BlendMode::Multiply;
        highlighter.baseSize = 4.5f; // 4.5 mm
        highlighter.color = BLRgba32(0xFF, 0xE5, 0x00, 0x80);
        highlighter.isPressureEnabled = false;
        pens.push_back(highlighter);

        activePenIndex = 0;
    }

    size_t AddPen(const PenTool& tool) {
        pens.push_back(tool);
        return pens.size() - 1;
    }

    void RemovePen(size_t index) {
        if (index >= pens.size() || pens.size() <= 1) return;
        pens.erase(pens.begin() + index);
        if (activePenIndex >= pens.size()) {
            activePenIndex = pens.size() - 1;
        }
    }

    void SelectPen(size_t index) noexcept {
        if (index < pens.size()) {
            activePenIndex = index;
        }
    }

    [[nodiscard]] PenTool& GetActivePen() noexcept {
        return pens[activePenIndex];
    }

    [[nodiscard]] const PenTool& GetActivePen() const noexcept {
        return pens[activePenIndex];
    }
};