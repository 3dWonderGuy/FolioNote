#pragma once
#include <string>
#include <vector>
#include <memory>
#include <blend2d/blend2d.h>
#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

enum class ImageFormat : uint8_t {
    PNG = 0,
    JPEG = 1,
    WebP = 2,
    ExternalPath = 3
};

/**
 * @brief Persistent canvas entity holding raster images (PNG, JPEG, WebP, or external file path).
 */
class ImageObject : public CanvasObject {
public:
    double worldX = 0.0;
    double worldY = 0.0;
    double worldWidth = 300.0;
    double worldHeight = 200.0;

    uint32_t naturalWidth = 0;
    uint32_t naturalHeight = 0;
    ImageFormat imageFormat = ImageFormat::PNG;
    std::string imagePath = "";            // Path if external reference
    std::vector<uint8_t> embeddedData;     // Raw compressed bytes (PNG/JPEG) if embedded
    BLImage cachedBlImage;                 // Blend2D decoded image cache
    bool isLoaded = false;

    ImageObject() {
        type = ObjectType::Image;
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

        if (isLoaded && !cachedBlImage.is_empty()) {
            ctx.blit_image(BLPoint(worldX, worldY), cachedBlImage);
        } else {
            // Draw placeholder frame
            ctx.set_fill_style(BLRgba32(0x1E, 0x22, 0x2A, static_cast<uint8_t>(opacity * 220)));
            ctx.fill_rect(worldX, worldY, worldWidth, worldHeight);

            ctx.set_stroke_style(BLRgba32(0x3A, 0x40, 0x4E, static_cast<uint8_t>(opacity * 255)));
            ctx.set_stroke_width(1.5 / (viewport.zoom > 0.001 ? viewport.zoom : 1.0));
            ctx.stroke_rect(worldX, worldY, worldWidth, worldHeight);
        }

        ctx.restore();
    }

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<ImageObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

} // namespace Folio