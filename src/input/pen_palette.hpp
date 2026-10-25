#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <blend2d/blend2d.h>

#include "core/ink_engine/ink_engine.hpp"

enum class BlendMode : uint8_t {
    Normal = 0,
    Multiply,
    Additive
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
    union {
        float     baseSize          = 0.5f; // Baseline in physical millimeters (0.5 mm)
        float     widthMm;                  // Physical millimeter stroke width alias
    };
    float         minSizeFactor     = 0.2f;
    float         maxSizeFactor     = 1.8f;
    
    BLRgba32      color             = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
    float         opacity           = 1.0f;
    float         smoothing         = 0.5f;
    bool          isPressureEnabled = true;
    bool          isTiltEnabled     = true;
    bool          isFlowSimEnabled  = false;

    /// Seamless conversion allowing PenTool to be passed directly to InkEngine APIs
    operator StrokeStyle() const noexcept {
        return ToStrokeStyle();
    }

    [[nodiscard]] StrokeStyle ToStrokeStyle() const noexcept {
        BLRgba32 effectiveColor = color;
        float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);
        effectiveColor.setA(static_cast<uint32_t>(static_cast<float>(color.a()) * clampedOpacity + 0.5f));
        return StrokeStyle{ baseSize, capType, strokePattern, ToColorRgba(effectiveColor), isPressureEnabled, isTiltEnabled, isFlowSimEnabled };
    }
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
