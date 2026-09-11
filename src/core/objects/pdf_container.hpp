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
#include "core/render/pdf_renderer.hpp"

namespace Folio {

/**
 * @brief Canvas object container representing a PDF document page placed on the canvas.
 * 
 * Supports:
 * - Standalone placement or locked background layer.
 * - Dynamic affine transformations (position, scale, rotation).
 * - High-resolution vector paper preview and cached raster blitting.
 * - Protection from accidental eraser hits and background selection locking.
 */
class PdfContainer : public CanvasObject {
public:
    double worldX = 0.0;
    double worldY = 0.0;
    // Standard A4 default dimensions in millimeters (210mm x 297mm)
    double worldWidth = 210.0;
    double worldHeight = 297.0;

    std::string pdfPath = "";             // Path relative to notebook package (e.g. "imports/pdfs/pdf_<hash>.pdf") or absolute external path
    std::string originalFileName = "";    // Original document name for UI / tooltip / search
    std::string resolvedDiskPath = "";    // Direct absolute path on disk if known
    bool isExternalLink = false;          // True if referenced directly from external disk without local copy
    int pageIndex = 0;                    // 0-based page index
    int totalPageCount = 1;               // Total pages in source document
    bool isBackground = false;            // Locked background layer mode (ink flows over it, cannot be accidentally dragged or erased)
    mutable bool isLoaded = false;        // True when rendered surface is ready
    mutable bool loadAttempted = false;
    mutable BLImage cachedPageImage;      // Decoded raster surface for the page

    void EnsurePageLoaded() const {
        if (isLoaded || loadAttempted) return;
        loadAttempted = true;

        std::string diskPath = resolvedDiskPath;
        if (diskPath.empty()) {
            diskPath = pdfPath;
        }
        if (diskPath.empty() || !std::filesystem::exists(diskPath)) {
            return;
        }

        auto res = Folio::PdfRenderer::RenderPage(diskPath, pageIndex, 150.0);
        if (res.success && !res.image.is_empty()) {
            cachedPageImage = res.image;
            isLoaded = true;
            if (res.widthMm > 10.0 && res.heightMm > 10.0) {
                auto* mutableThis = const_cast<PdfContainer*>(this);
                mutableThis->worldWidth = res.widthMm;
                mutableThis->worldHeight = res.heightMm;
                mutableThis->UpdateBounds();
            }
        }
    }

    PdfContainer() {
        type = ObjectType::PDF;
        UpdateBounds();
    }

    PdfContainer(const std::string& path, const std::string& origName, int pageIdx, int totalPages,
                 double x = 0.0, double y = 0.0, double w = 210.0, double h = 297.0, bool asBackground = false)
        : worldX(x), worldY(y), worldWidth(w), worldHeight(h),
          pdfPath(path), originalFileName(origName), pageIndex(pageIdx), totalPageCount(totalPages),
          isBackground(asBackground)
    {
        type = ObjectType::PDF;
        UpdateBounds();
    }

    void UpdateBounds() override {
        double minX = worldX;
        double minY = worldY;
        double maxX = worldX + worldWidth;
        double maxY = worldY + worldHeight;

        if (worldWidth < 0.0) { minX = worldX + worldWidth; maxX = worldX; }
        if (worldHeight < 0.0) { minY = worldY + worldHeight; maxY = worldY; }

        BLPoint corners[4] = {
            transform.map_point(minX, minY),
            transform.map_point(maxX, minY),
            transform.map_point(minX, maxY),
            transform.map_point(maxX, maxY)
        };

        bounds = AABB{
            std::min({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
            std::min({corners[0].y, corners[1].y, corners[2].y, corners[3].y}),
            std::max({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
            std::max({corners[0].y, corners[1].y, corners[2].y, corners[3].y})
        };
    }

    void ApplyTransform(const BLMatrix2D& matrix) override {
        if (isBackground) return; // Background layer is locked in place
        transform.post_transform(matrix);
        UpdateBounds();
    }

    bool HitTest(double queryWorldX, double queryWorldY) const override {
        if (!isVisible || opacity <= 0.0f) return false;
        if (isBackground) return false; // In background mode, let clicks pass through to canvas
        if (!bounds.Contains(queryWorldX, queryWorldY)) return false;

        BLMatrix2D inv;
        BLMatrix2D::invert(inv, transform);
        BLPoint local = inv.map_point(queryWorldX, queryWorldY);

        double minX = std::min(worldX, worldX + worldWidth);
        double maxX = std::max(worldX, worldX + worldWidth);
        double minY = std::min(worldY, worldY + worldHeight);
        double maxY = std::max(worldY, worldY + worldHeight);

        return (local.x >= minX && local.x <= maxX && local.y >= minY && local.y <= maxY);
    }

    bool HitTestSwept(const Point2D& /*w0*/, const Point2D& /*w1*/, double /*radiusMm*/) const override {
        // Erasers must never erase PDF document pages
        return false;
    }

    bool Intersects(const AABB& selectionBounds) const override {
        return bounds.Intersects(selectionBounds);
    }

    void Render(BLContext& ctx, const Viewport& viewport) const override {
        if (!isVisible || opacity <= 0.0f) return;
        if (!bounds.Intersects(viewport.bounds)) return;

        EnsurePageLoaded();

        ctx.save();
        ctx.apply_transform(transform);

        double rx = std::min(worldX, worldX + worldWidth);
        double ry = std::min(worldY, worldY + worldHeight);
        double rw = std::abs(worldWidth);
        double rh = std::abs(worldHeight);

        // 1. Drop shadow behind the page
        ctx.set_fill_style(BLRgba32(0x00, 0x00, 0x00, 0x22));
        ctx.fill_round_rect(rx + 1.5, ry + 2.0, rw, rh, 2.0);

        // 2. White Paper Surface
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.fill_round_rect(rx, ry, rw, rh, 1.5);

        // 3. Render cached page surface if available
        if (isLoaded && !cachedPageImage.is_empty()) {
            BLRect dst(rx, ry, rw, rh);
            ctx.blit_image(dst, cachedPageImage);
        } else {
            // High-fidelity CAD / architectural vector placeholder
            // Subtle page border
            ctx.set_stroke_style(BLRgba32(0xCC, 0xD1, 0xD9, 0xFF));
            ctx.set_stroke_width(0.6);
            ctx.stroke_round_rect(rx, ry, rw, rh, 1.5);

            // Red PDF Banner / Header
            ctx.set_fill_style(BLRgba32(0xEA, 0x43, 0x35, 0xEE)); // Adobe/Google Red
            ctx.fill_round_rect(rx + 8.0, ry + 8.0, 42.0, 16.0, 3.0);

            // Document Header line placeholder
            ctx.set_fill_style(BLRgba32(0x5F, 0x63, 0x68, 0xCC));
            ctx.fill_round_rect(rx + 56.0, ry + 12.0, std::min(rw - 70.0, 120.0), 8.0, 2.0);

            // Watermark grid / document lines
            ctx.set_stroke_style(BLRgba32(0xE8, 0xEA, 0xED, 0xBB));
            ctx.set_stroke_width(0.5);
            double lineY = ry + 40.0;
            while (lineY < ry + rh - 20.0) {
                ctx.stroke_line(rx + 12.0, lineY, rx + rw - 12.0, lineY);
                lineY += 12.0;
            }

            if (isBackground) {
                ctx.set_fill_style(BLRgba32(0x1A, 0x73, 0xE8, 0x33));
                ctx.fill_round_rect(rx + 8.0, ry + rh - 24.0, 90.0, 14.0, 2.0);
            }
        }

        ctx.restore();
    }

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<PdfContainer>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

} // namespace Folio
