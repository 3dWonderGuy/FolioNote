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
    float         baseSize          = 0.5f; // Baseline in physical millimeters (0.5 mm)
    float         minSizeFactor     = 0.2f;
    float         maxSizeFactor     = 1.8f;
    
    BLRgba32      color             = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
    float         opacity           = 1.0f;
    float         smoothing         = 0.5f;
    bool          isPressureEnabled = true;
    bool          isTiltEnabled     = true;
    bool          isFlowSimEnabled  = false;
};

class PenPalette {
public:
    PenTool activePen;

    PenPalette() {
        activePen.penType = PenType::Pen;
        activePen.baseSize = 0.5f;
        activePen.color = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
    }

    [[nodiscard]] PenTool& GetActivePen() noexcept {
        return activePen;
    }

    [[nodiscard]] const PenTool& GetActivePen() const noexcept {
        return activePen;
    }
};