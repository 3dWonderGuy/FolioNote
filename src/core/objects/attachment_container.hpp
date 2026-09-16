#pragma once
/**
 * @file attachment_container.hpp
 * @brief Canvas object representing a linked external file attachment.
 *
 * AttachmentObject is a non-resizable icon chip on the canvas that links to
 * an external file. The file is NEVER embedded in the .folio binary — it lives
 * in the notebook's companion folder:
 *
 *   <notebook_dir>/imports/files/<displayName>
 *
 * This keeps the .folio file small and predictable. The user is given the
 * option to "save file alongside notebook" at import time which copies the
 * file into that sidecar folder. If they choose not to, filePath is an
 * absolute path to wherever the file lives on the system.
 *
 * Behavior:
 *   Double-click (or right-click → Open) on the chip calls OpenFile(), which
 *   delegates to the OS default handler — exactly "right-click → Open" in
 *   Windows Explorer. On Windows this uses ShellExecuteW; on other platforms
 *   a TODO stub is left with the cross-platform hook point.
 *
 * Rendering:
 *   A fixed 140×52px rectangle chip with:
 *     - File-type color band on the left edge
 *     - File extension badge (e.g. "PDF", "XLSX")
 *     - Truncated displayName label
 *   The chip is NOT resizable. Only body-move is available via the gizmo.
 *   GetCustomGizmoHandles returns an empty list, which causes the engine to
 *   fall through to the body-move path without any resize grips.
 *
 * Scalability:
 *   To add new file-type icons or color bands, edit the GetTypeColor() helper.
 *   No other files need modification.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

#include <blend2d/blend2d.h>
#include <imgui.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

// Windows-only open-with-default-app support
#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace Folio {

/**
 * @brief Fixed-size canvas chip linking to an external file.
 *
 * Position is in world millimeters. Size is fixed at chipW × chipH world mm
 * (not user-resizable). Only translation (body-move) is permitted via gizmo.
 */
class AttachmentObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    std::string filePath;       ///< Absolute path or sidecar-relative path to the file
    std::string displayName;    ///< Shown on the chip label (usually the filename)
    std::string mimeType;       ///< e.g. "application/pdf", "image/png", "text/plain"

    double worldX    = 0.0;     ///< Chip position X (world mm, top-left)
    double worldY    = 0.0;     ///< Chip position Y (world mm, top-left)

    /// Fixed chip dimensions in world mm (not user-resizable)
    static constexpr double chipW = 50.0;
    static constexpr double chipH = 18.0;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    AttachmentObject() {
        type = ObjectType::AttachmentFile;
        UpdateBounds();
    }

    AttachmentObject(const std::string& path, const std::string& name,
                     const std::string& mime = "")
        : filePath(path), displayName(name), mimeType(mime)
    {
        type = ObjectType::AttachmentFile;
        UpdateBounds();
    }

    // =========================================================================
    // FILE OPEN
    // =========================================================================

    /**
     * @brief Opens the linked file with the OS default application.
     *
     * Equivalent to right-click → Open in Windows Explorer.
     * On Windows: ShellExecuteW(NULL, L"open", ...) with SW_SHOWNORMAL.
     *
     * TODO (cross-platform):
     *   macOS: system("open \"<path>\"")  or  NSWorkspace::openURL
     *   Linux: system("xdg-open \"<path>\"")
     */
    void OpenFile() const {
#if defined(_WIN32) || defined(_WIN64)
        // Convert UTF-8 filePath to wide string for WinAPI
        int wLen = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
        if (wLen > 0) {
            std::wstring wPath(wLen, 0);
            MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &wPath[0], wLen);
            ShellExecuteW(nullptr, L"open", wPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
#else
        // TODO: macOS / Linux cross-platform open
        (void)filePath;
#endif
    }

    // =========================================================================
    // BOUNDS & SPATIAL
    // =========================================================================

    void UpdateBounds() override {
        // Transform the fixed-size chip corners through the accumulated matrix
        BLPoint p[4] = {
            transform.map_point(worldX,         worldY),
            transform.map_point(worldX + chipW, worldY),
            transform.map_point(worldX + chipW, worldY + chipH),
            transform.map_point(worldX,         worldY + chipH)
        };
        double minX = p[0].x, maxX = p[0].x;
        double minY = p[0].y, maxY = p[0].y;
        for (int i = 1; i < 4; ++i) {
            minX = (std::min)(minX, p[i].x);
            maxX = (std::max)(maxX, p[i].x);
            minY = (std::min)(minY, p[i].y);
            maxY = (std::max)(maxY, p[i].y);
        }
        bounds = AABB(minX, minY, maxX, maxY);
    }

    bool HitTest(double wx, double wy) const override {
        return bounds.Contains(wx, wy);
    }

    bool Intersects(const AABB& sel) const override {
        return bounds.Intersects(sel);
    }

    // =========================================================================
    // TRANSFORM
    // =========================================================================

    /**
     * @brief Translation-only transform. Accumulates matrix, does not mutate worldX/Y.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    /**
     * @brief Bakes translation from transform into worldX/Y. Resets to identity.
     *
     * Attachment chips are never scaled or rotated (gizmo exposes no resize grips),
     * so we only extract the translation components (m20, m21).
     */
    void BakeTransform() override {
        worldX += transform.m20;
        worldY += transform.m21;
        transform = BLMatrix2D::make_identity();
        UpdateBounds();
    }

    // =========================================================================
    // GIZMO — body-move only, no resize grips
    // =========================================================================

    /**
     * @brief Returns false → engine falls through to standard body-move only.
     *
     * By returning false (no custom handles provided) AND having a valid AABB,
     * the engine's HitTest on the body triggers a body-drag. The standard 8-point
     * resize grips are NOT shown because GetCustomGizmoHandles returns false AND
     * the chip is always a fixed size.
     *
     * TODO: If we ever want a "no-resize" contract in the gizmo, we can implement
     * a GetResizeEnabled() virtual on CanvasObject. For now, attachment objects
     * just look non-resizable by having a tiny AABB that makes corner grips trivially
     * overlap and thus unusable.
     */
    bool GetCustomGizmoHandles(std::vector<GizmoHandle>& /*outHandles*/,
                                const CanvasTransform& /*transform*/) const override {
        return false; // Use body-move only
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Draws the file-link icon chip on the Blend2D canvas.
     *
     * Visual structure (world mm coordinates):
     *   ┌─[type band]──────────────────────────┐
     *   │  [EXT]   displayName (truncated)      │
     *   └────────────────────────────────────────┘
     *
     * The type band color is determined by file extension via GetTypeColor().
     */
    void Render(BLContext& ctx, const Viewport& /*viewport*/) const override {
        if (!isVisible) return;

        ctx.save();
        ctx.apply_transform(transform);

        const double x  = worldX;
        const double y  = worldY;
        const double w  = chipW;
        const double h  = chipH;
        const double r  = 2.0;          // Corner radius (mm)
        const double bw = 8.0;          // Type band width (mm)

        // Background chip
        ctx.set_fill_style(BLRgba32(0x1E, 0x20, 0x28, static_cast<uint8_t>(opacity * 235)));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

        // Type color band on the left edge
        BLRgba32 bandCol = GetTypeColor();
        ctx.save();
        BLRect bandRect(x, y, bw, h);
        // Clip to rounded left corners only (approximate with round rect)
        ctx.fill_round_rect(BLRoundRect(x, y, bw + r, h, r, r));  // left side fill
        ctx.set_fill_style(bandCol);
        ctx.fill_round_rect(BLRoundRect(x, y, bw + r, h, r, r));
        ctx.restore();

        // Border
        ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 200));
        ctx.set_stroke_width(0.4);
        ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

        // File extension label (white text in band)
        // Note: Blend2D BLFont text rendering requires font loading infrastructure.
        // For now we draw a placeholder dot — full font rendering hooked in Phase 2.
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 200));
        ctx.fill_circle(x + bw * 0.5, y + h * 0.5, 1.2); // placeholder dot

        ctx.restore();
    }

    // =========================================================================
    // DUPLICATION & PERSISTENCE
    // =========================================================================

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<AttachmentObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}

private:
    /**
     * @brief Returns a distinguishing color for the left type band based on extension.
     *
     * The extension is extracted from displayName (last '.' to end, lowercased).
     * Add new extensions here; return value is a BLRgba32 solid color.
     */
    BLRgba32 GetTypeColor() const {
        // Extract lowercase extension from displayName
        auto ext = std::string{};
        auto dot = displayName.rfind('.');
        if (dot != std::string::npos) {
            ext = displayName.substr(dot + 1);
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (ext == "pdf")                              return BLRgba32(0xE8, 0x11, 0x23, 0xFF); // Red
        if (ext == "xlsx" || ext == "xls" || ext == "csv")  return BLRgba32(0x10, 0x7C, 0x41, 0xFF); // Green
        if (ext == "docx" || ext == "doc")             return BLRgba32(0x00, 0x78, 0xD4, 0xFF); // Blue
        if (ext == "pptx" || ext == "ppt")             return BLRgba32(0xD8, 0x3B, 0x01, 0xFF); // Orange
        if (ext == "zip" || ext == "rar" || ext == "7z") return BLRgba32(0xF0, 0xCC, 0x00, 0xFF); // Yellow
        if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "svg")
                                                       return BLRgba32(0x00, 0x82, 0x72, 0xFF); // Teal
        if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "aac")
                                                       return BLRgba32(0x5C, 0x2D, 0x91, 0xFF); // Purple
        if (ext == "mp4" || ext == "mov" || ext == "avi" || ext == "mkv")
                                                       return BLRgba32(0x00, 0x66, 0xCC, 0xFF); // Dark blue
        if (ext == "txt" || ext == "md")               return BLRgba32(0x60, 0x60, 0x60, 0xFF); // Grey

        return BLRgba32(0x40, 0x44, 0x52, 0xFF); // Default dark grey
    }
};

} // namespace Folio
