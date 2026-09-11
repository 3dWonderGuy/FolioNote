#pragma once
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <blend2d/blend2d.h>
#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

enum class ImageFormat : uint8_t {
    PNG = 0,
    JPEG = 1,
    WebP = 2,
    BMP = 3,
    ExternalPath = 4
};

/**
 * @brief Canvas object container representing an image placed on the infinite canvas.
 * 
 * Responsibilities:
 * - Spatial footprint, affine transformations, and bounding box computation.
 * - Precise geometric hit-testing for selection, marquee drag, and eraser tools.
 * - Hardware-accelerated Blend2D rendering of the image surface.
 * - Aspect ratio preservation during gizmo scaling and transformations.
 * 
 * (File I/O, disk decoding, and asset deduplication are handled externally by FileLoader / FileSaver).
 */
class ImageObject : public CanvasObject {
public:
    double worldX = 0.0;
    double worldY = 0.0;
    double worldWidth = 100.0;   // In canvas world millimeters (mm)
    double worldHeight = 75.0;   // In canvas world millimeters (mm)

    uint32_t naturalWidth = 0;   // Source image pixel dimensions
    uint32_t naturalHeight = 0;
    ImageFormat imageFormat = ImageFormat::PNG;
    std::string imagePath = "";            // Path relative to notebook package (e.g. "imports/images/<hash>.png")
    std::vector<uint8_t> embeddedData;     // Raw serialized payload (maintained for binary serializer compatibility)
    BLImage cachedBlImage;                 // Decoded Blend2D raster surface for rendering
    bool isLoaded = false;                 // True when cachedBlImage is valid and ready for blitting

    ImageObject() {
        type = ObjectType::Image;
        UpdateBounds();
    }

    /**
     * @brief Assigns an in-memory Blend2D image surface and updates dimensions.
     */
    void SetImage(const BLImage& img, const std::string& path = "", double defaultWorldWidth = 120.0) {
        cachedBlImage = img;
        imagePath = path;
        isLoaded = !img.is_empty();

        if (isLoaded) {
            naturalWidth = static_cast<uint32_t>(img.width());
            naturalHeight = static_cast<uint32_t>(img.height());

            if (defaultWorldWidth > 0.0 && naturalWidth > 0 && naturalHeight > 0) {
                worldWidth = defaultWorldWidth;
                worldHeight = defaultWorldWidth * (static_cast<double>(naturalHeight) / static_cast<double>(naturalWidth));
            }
        }
        UpdateBounds();
    }

    /**
     * @brief Computes enclosing world AABB by transforming all 4 corners through the affine matrix.
     */
    void UpdateBounds() override {
        BLPoint corners[4] = {
            transform.map_point(worldX, worldY),
            transform.map_point(worldX + worldWidth, worldY),
            transform.map_point(worldX + worldWidth, worldY + worldHeight),
            transform.map_point(worldX, worldY + worldHeight)
        };

        bounds = AABB{
            std::min({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
            std::min({corners[0].y, corners[1].y, corners[2].y, corners[3].y}),
            std::max({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
            std::max({corners[0].y, corners[1].y, corners[2].y, corners[3].y})
        };
    }

    /**
     * @brief Evaluates whether a world-space point intersects the oriented image rectangle.
     */
    bool HitTest(double hitWorldX, double hitWorldY) const override {
        if (!bounds.Contains(hitWorldX, hitWorldY)) return false;

        BLMatrix2D invTransform;
        if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) {
            invTransform = BLMatrix2D::make_identity();
        }

        BLPoint localPt = invTransform.map_point(hitWorldX, hitWorldY);
        return (localPt.x >= worldX && localPt.x <= worldX + worldWidth &&
                localPt.y >= worldY && localPt.y <= worldY + worldHeight);
    }

    /**
     * @brief Tests intersection with a marquee selection box.
     */
    bool Intersects(const AABB& selectionBounds) const override {
        if (!bounds.Intersects(selectionBounds)) return false;

        BLPoint corners[4] = {
            transform.map_point(worldX, worldY),
            transform.map_point(worldX + worldWidth, worldY),
            transform.map_point(worldX + worldWidth, worldY + worldHeight),
            transform.map_point(worldX, worldY + worldHeight)
        };

        for (int i = 0; i < 4; ++i) {
            if (selectionBounds.Contains(corners[i].x, corners[i].y)) return true;
        }

        double cx = (selectionBounds.minX + selectionBounds.maxX) * 0.5;
        double cy = (selectionBounds.minY + selectionBounds.maxY) * 0.5;
        return HitTest(cx, cy);
    }

    /**
     * @brief Evaluates intersection with a circular eraser or probe.
     */
    bool HitTestCircle(double worldXQuery, double worldYQuery, double radiusMm) const override {
        AABB queryBox(worldXQuery - radiusMm, worldYQuery - radiusMm, worldXQuery + radiusMm, worldYQuery + radiusMm);
        if (!bounds.Intersects(queryBox)) return false;

        if (HitTest(worldXQuery, worldYQuery)) return true;

        BLMatrix2D invTransform;
        if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) {
            invTransform = BLMatrix2D::make_identity();
        }
        BLPoint localPt = invTransform.map_point(worldXQuery, worldYQuery);
        double clampedX = std::clamp(localPt.x, worldX, worldX + worldWidth);
        double clampedY = std::clamp(localPt.y, worldY, worldY + worldHeight);

        BLPoint worldNearest = transform.map_point(clampedX, clampedY);
        double distSq = (worldNearest.x - worldXQuery) * (worldNearest.x - worldXQuery) +
                        (worldNearest.y - worldYQuery) * (worldNearest.y - worldYQuery);
        return distSq <= (radiusMm * radiusMm);
    }

    /**
     * @brief Swept continuous eraser hit-test (prevents fast-moving skips).
     */
    bool HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const override {
        AABB sweptBox(
            std::min(w0.x, w1.x) - radiusMm,
            std::min(w0.y, w1.y) - radiusMm,
            std::max(w0.x, w1.x) + radiusMm,
            std::max(w0.y, w1.y) + radiusMm
        );
        if (!bounds.Intersects(sweptBox)) return false;

        return HitTestCircle(w0.x, w0.y, radiusMm) ||
               HitTestCircle(w1.x, w1.y, radiusMm) ||
               HitTestCircle((w0.x + w1.x) * 0.5, (w0.y + w1.y) * 0.5, radiusMm);
    }

    /**
     * @brief Ensures the Blend2D raster surface is loaded, lazily decoding from embeddedData or imagePath.
     */
    bool EnsureLoaded(const std::string& packageRoot = "") {
        if (isLoaded && !cachedBlImage.is_empty()) return true;

        if (!embeddedData.empty()) {
            if (cachedBlImage.read_from_data(embeddedData.data(), embeddedData.size()) == BL_SUCCESS && !cachedBlImage.is_empty()) {
                naturalWidth = static_cast<uint32_t>(cachedBlImage.width());
                naturalHeight = static_cast<uint32_t>(cachedBlImage.height());
                isLoaded = true;
                return true;
            }
        }

        std::string fullPath = imagePath;
        if (!fullPath.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(fullPath, ec) && !packageRoot.empty()) {
                std::filesystem::path resolved = std::filesystem::path(packageRoot) / imagePath;
                if (std::filesystem::exists(resolved, ec)) {
                    fullPath = resolved.string();
                }
            }
            if (std::filesystem::exists(fullPath, ec) && cachedBlImage.read_from_file(fullPath.c_str()) == BL_SUCCESS && !cachedBlImage.is_empty()) {
                naturalWidth = static_cast<uint32_t>(cachedBlImage.width());
                naturalHeight = static_cast<uint32_t>(cachedBlImage.height());
                isLoaded = true;
                return true;
            }
        }

        return false;
    }

    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    /**
     * @brief Renders the image or a placeholder frame onto the Blend2D context.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override {
        if (!isVisible) return;
        if (!isLoaded || cachedBlImage.is_empty()) {
            const_cast<ImageObject*>(this)->EnsureLoaded();
        }

        ctx.save();
        ctx.apply_transform(transform);

        if (opacity < 0.999f) {
            ctx.set_global_alpha(static_cast<double>(opacity));
        }

        if (isLoaded && !cachedBlImage.is_empty()) {
            ctx.blit_image(BLRect(worldX, worldY, worldWidth, worldHeight), cachedBlImage);
        } else {
            // Placeholder frame when image is unloaded, missing, or still caching
            ctx.set_fill_style(BLRgba32(0x18, 0x1C, 0x24, static_cast<uint8_t>(opacity * 200)));
            ctx.fill_round_rect(BLRoundRect(worldX, worldY, worldWidth, worldHeight, 4.0, 4.0));

            ctx.set_stroke_style(BLRgba32(0x3B, 0x44, 0x54, static_cast<uint8_t>(opacity * 255)));
            ctx.set_stroke_width(1.0 / (viewport.zoom > 0.001 ? viewport.zoom : 1.0));
            ctx.stroke_round_rect(BLRoundRect(worldX, worldY, worldWidth, worldHeight, 4.0, 4.0));
        }

        ctx.restore();
    }

    [[nodiscard]] double GetAspectRatio() const noexcept {
        if (naturalWidth > 0 && naturalHeight > 0) {
            return static_cast<double>(naturalWidth) / static_cast<double>(naturalHeight);
        }
        return worldHeight > 0.001 ? (worldWidth / worldHeight) : 1.0;
    }

    void SetWidthPreservingAspect(double newWidth) {
        if (newWidth <= 0.0) return;
        double aspect = GetAspectRatio();
        worldWidth = newWidth;
        worldHeight = (aspect > 0.001) ? (newWidth / aspect) : worldHeight;
        UpdateBounds();
    }

    void SetHeightPreservingAspect(double newHeight) {
        if (newHeight <= 0.0) return;
        double aspect = GetAspectRatio();
        worldHeight = newHeight;
        worldWidth = (aspect > 0.001) ? (newHeight * aspect) : worldWidth;
        UpdateBounds();
    }

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<ImageObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

// Canonical type alias
using ImageContainer = ImageObject;

} // namespace Folio