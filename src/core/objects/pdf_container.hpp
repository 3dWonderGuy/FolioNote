#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

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
#include "io/file_manager.hpp"

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

    /**
     * @brief Resets the cached raster surface and load attempt flag, allowing reload upon path updates.
     */
    void ResetLoadState() {
        isLoaded = false;
        loadAttempted = false;
        cachedPageImage.reset();
    }

    /**
     * @brief Ensures that the underlying PDF page raster surface is decoded and cached in memory.
     * 
     * --- RESOLUTION WORKFLOW ---
     * 1. Checks `resolvedDiskPath` first (direct absolute path on disk).
     * 2. If empty or missing, falls back to `pdfPath`.
     * 3. If missing and `fallbackDir` (e.g. notebook package root) is supplied, attempts
     *    resolving relative path via FileManager::JoinPath(fallbackDir, pdfPath).
     * 4. Once verified, invokes Folio::PdfRenderer::RenderPage to decode the vector page.
     * 
     * @param fallbackDir Optional directory (such as notebook root folder) to locate relative assets.
     */
    void EnsurePageLoaded(const std::string& fallbackDir = "") const {
        if (isLoaded) return;
        if (loadAttempted && cachedPageImage.is_empty() && fallbackDir.empty()) return;
        loadAttempted = true;

        std::string diskPath = resolvedDiskPath;
        if (diskPath.empty()) {
            diskPath = pdfPath;
        }

        // 1. If direct path fails, attempt fallback directory resolution
        if (!diskPath.empty() && !FileManager::Exists(diskPath) && !fallbackDir.empty()) {
            std::string resolved = FileManager::JoinPath(fallbackDir, pdfPath);
            if (FileManager::Exists(resolved)) {
                diskPath = resolved;
                const_cast<PdfContainer*>(this)->resolvedDiskPath = diskPath;
            }
        }

        // 2. Final existence check before pushing to renderer
        if (diskPath.empty() || !FileManager::Exists(diskPath)) {
            return;
        }

        // 3. Render and cache
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
        worldWidth = 210.0;
        worldHeight = 297.0;
        UpdateBounds();
    }

    PdfContainer(const std::string& path, const std::string& origName, int pageIdx, int totalPages,
                 double x = 0.0, double y = 0.0, double w = 210.0, double h = 297.0, bool asBackground = false)
        : pdfPath(path), originalFileName(origName), pageIndex(pageIdx), totalPageCount(totalPages),
          isBackground(asBackground)
    {
        type = ObjectType::PDF;
        worldX = x;
        worldY = y;
        worldWidth = w;
        worldHeight = h;
        UpdateBounds();
    }

    void ApplyTransform(const BLMatrix2D& matrix) override {
        if (isBackground) return; // Background layer is locked in place
        CanvasObject::ApplyTransform(matrix);
    }

    bool HitTest(double queryWorldX, double queryWorldY) const override {
        if (!isVisible || !isSelectable) return false;
        if (isBackground) return false; // In background mode, let clicks pass through to canvas
        if (!bounds.Contains(queryWorldX, queryWorldY)) return false;

        // Fast-path: already selected object allows immediate interaction inside bounds
        if (isSelected) return true;

        BLMatrix2D inv;
        BLMatrix2D::invert(inv, transform);
        BLPoint local = inv.map_point(queryWorldX, queryWorldY);

        double minX = std::min(worldX, worldX + worldWidth);
        double maxX = std::max(worldX, worldX + worldWidth);
        double minY = std::min(worldY, worldY + worldHeight);
        double maxY = std::max(worldY, worldY + worldHeight);

        return (local.x >= minX && local.x <= maxX && local.y >= minY && local.y <= maxY);
    }

    /**
     * @brief Circular proximity hit-test.
     * 
     * @note When `isBackground` is true, the PDF container acts as an immutable paper backdrop;
     *       all selection probes and clicks are rejected so freehand drawing and lasso selection
     *       pass freely over the page without selecting or moving it.
     */
    bool HitTestCircle(double worldXQuery, double worldYQuery, double /*radiusMm*/) const override {
        if (!isVisible || !isSelectable) return false;
        if (isBackground) return false;
        return HitTest(worldXQuery, worldYQuery);
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
};

} // namespace Folio