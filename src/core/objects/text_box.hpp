#pragma once
#include <string>
#include <memory>
#include <blend2d/blend2d.h>
#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @brief Persistent canvas entity holding formatted text boxes.
 */
class TextBoxObject : public CanvasObject {
public:
    double worldX = 0.0;
    double worldY = 0.0;
    double worldWidth = 280.0;
    double worldHeight = 60.0;

    std::string text = "";
    std::string fontFamily = "Segoe UI";
    float fontSize = 16.0f;
    BLRgba32 textColor{0xFFFFFFFF}; // Default white
    bool isEditing = false;
    bool isWrap = true;

    TextBoxObject() {
        type = ObjectType::Text;
        UpdateBounds();
    }

    void UpdateBounds() override {
        bounds.minX = worldX;
        bounds.minY = worldY;
        bounds.maxX = worldX + worldWidth;
        bounds.maxY = worldY + worldHeight;
    }

    bool HitTest(double hitWorldX, double hitWorldY) const override {
        return bounds.Contains(hitWorldX, hitWorldY);
    }

    bool Intersects(const AABB& selectionBounds) const override {
        return bounds.Intersects(selectionBounds);
    }

    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    void Render(BLContext& ctx, const Viewport& viewport) const override {
        if (!isVisible) return;

        ctx.save();
        ctx.apply_transform(transform);

        // Background / Border placeholder
        ctx.set_fill_style(BLRgba32(0x18, 0x1A, 0x20, static_cast<uint8_t>(opacity * 200)));
        ctx.fill_rect(worldX, worldY, worldWidth, worldHeight);

        ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x52, static_cast<uint8_t>(opacity * 255)));
        ctx.set_stroke_width(1.0 / (viewport.zoom > 0.001 ? viewport.zoom : 1.0));
        ctx.stroke_rect(worldX, worldY, worldWidth, worldHeight);

        // If text is not empty, draw basic text rendering
        if (!text.empty()) {
            ctx.set_fill_style(textColor);
            // Blend2D font rendering or text overlay
        }

        ctx.restore();
    }

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<TextBoxObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

} // namespace Folio