#pragma once
/**
 * @file attachment_container.hpp
 * @brief Canvas object representing a file attachment chip pinned to the canvas.
 *
 * AttachmentObject is a lightweight, non-resizable interactive chip that provides access to
 * an external or embedded file (documents, spreadsheets, executables, images, etc.).
 *
 * Attachment Modes:
 *   - Link mode (isEmbedded == false):
 *       filePath stores the original absolute path on disk. The file is NOT copied.
 *       The chip renders a chain-link badge. If the file is moved or deleted, the link breaks.
 *   - Embedded mode (isEmbedded == true):
 *       The file was copied into the notebook sidecar folder at attach time.
 *       filePath stores the sidecar-relative path (e.g. "attachments/<uuid>_filename.xlsx").
 *       The chip renders an embed badge. Fully portable — works after moving the notebook.
 *
 * Interaction Model:
 *   - Single-click:  Selects the chip; gizmo allows moving it.
 *   - Double-click:  Opens the file in the OS default application via OpenFile().
 *   - Right-click:   Shows a context menu with Open, Copy Path, and Remove from Page.
 *
 * Key Design Principles:
 *   1. Non-resizable Chip: Fixed-size badge (chipW x chipH mm) with body-move only.
 *   2. Quick Launcher: OpenFile() delegates to FileManager::OpenWithDefaultApp().
 *   3. Visual distinction: Embedded vs Link mode shown via a small corner badge.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

#include <blend2d/blend2d.h>
#include <imgui.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/text/font_manager.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"

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

    std::string filePath;       ///< Absolute path (link mode) or sidecar-relative path (embedded mode)
    std::string displayName;    ///< Shown on the chip label (usually the filename)
    std::string mimeType;       ///< e.g. "application/pdf", "image/png", "text/plain"

    /// Attachment mode:
    ///   true  = file was copied into the notebook sidecar at attach time (portable, safe to move notebook).
    ///   false = file path link to original location on disk (fast, but breaks if file is moved/renamed).
    bool isEmbedded = false;

    /// Fixed chip dimensions in world mm (not user-resizable)
    static constexpr double chipW = 50.0;
    static constexpr double chipH = 18.0;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor for deserialization and disk loading.
     * 
     * Working Process:
     *   Used by BinarySerializer and database loaders to instantiate a blank object
     *   before reading saved properties (filePath, world coordinates, transform, etc.)
     *   from disk. Initializes the type tag and calculates initial default bounds.
     */
    AttachmentObject() {
        type = ObjectType::AttachmentFile;
        worldWidth = chipW;
        worldHeight = chipH;
        UpdateBounds();
    }

    /**
     * @brief Parameterized constructor used when a user attaches a new file interactively.
     * 
     * Working Process:
     *   Directly called when a user picks a file through the UI ribbon / file dialog.
     *   Initializes the display label, paths, MIME type, and computes the world-space bounding box.
     * 
     * @param path     Absolute path (link) or sidecar-relative path (embedded).
     * @param name     Display label shown on the chip badge.
     * @param mime     Optional MIME type string, e.g. "application/pdf" or "image/png".
     * @param embedded True if the file was copied into the notebook sidecar (embedded mode).
     */
    AttachmentObject(const std::string& path, const std::string& name,
                     const std::string& mime = "", bool embedded = false)
        : filePath(path), displayName(name), mimeType(mime), isEmbedded(embedded)
    {
        type = ObjectType::AttachmentFile;
        worldWidth = chipW;
        worldHeight = chipH;
        UpdateBounds();
    }

    // =========================================================================
    // FILE OPEN
    // =========================================================================

    /**
     * @brief Opens the linked or embedded file with the OS default application.
     * 
     * Working Process:
     *   Delegates directly to FileManager::OpenWithDefaultApp(), which handles
     *   input validation, error logging, URI normalization, and cross-platform OS dispatch.
     * 
     * @return true if successfully launched by the operating system.
     */
    bool OpenFile() const {
        return FileManager::OpenWithDefaultApp(filePath);
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
     * @brief File attachment chips are fixed size and use a locked MoveOnly gizmo (body drag only, no resize grips).
     */
    GizmoStyle GetGizmoStyle() const noexcept override {
        return GizmoStyle::MoveOnly;
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

        // Type color band on the left edge (clip to round rect boundary)
        BLRgba32 bandCol = GetTypeColor();
        ctx.save();
        ctx.clip_to_rect(BLRect(x, y, bw, h));
        ctx.set_fill_style(bandCol);
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));
        ctx.restore();

        // Border
        ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 200));
        ctx.set_stroke_width(0.4);
        ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

        // Extract uppercase extension for the badge
        std::string ext = "";
        auto dot = displayName.rfind('.');
        if (dot != std::string::npos && dot + 1 < displayName.size()) {
            ext = displayName.substr(dot + 1);
            for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (ext.size() > 4) ext = ext.substr(0, 4);
        } else {
            ext = "FILE";
        }

        // Draw extension inside the left color band
        BLFont badgeFont = FontManager::Instance().GetFont("Segoe UI", 3.2f, true);
        ctx.fill_utf8_text(BLPoint(x + 1.2, y + h * 0.5 + 1.1), badgeFont, ext.data(), ext.size(), BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));

        // Draw display name label
        BLFont labelFont = FontManager::Instance().GetFont("Segoe UI", 3.4f, false);

        // Clip text so it doesn't bleed out of chip
        ctx.save();
        ctx.clip_to_rect(BLRect(x + bw + 2.0, y + 1.0, w - bw - 4.0, h - 2.0));
        ctx.fill_utf8_text(BLPoint(x + bw + 2.5, y + h * 0.5 + 1.2), labelFont, displayName.data(), displayName.size(), BLRgba32(0xF0, 0xF2, 0xF5, static_cast<uint8_t>(opacity * 255)));
        ctx.restore();

        // Draw embedded / link mode badge in the bottom-right corner of the chip.
        // Embedded: filled teal dot with 'E'.  Link: hollow grey dot with chain symbol.
        {
            const double badgeR  = 2.8;                              // Badge circle radius (mm)
            const double badgeCX = x + w - badgeR - 1.2;            // Centre X
            const double badgeCY = y + h - badgeR - 1.0;            // Centre Y

            if (isEmbedded) {
                // Filled teal circle = embedded/safe
                ctx.set_fill_style(BLRgba32(0x00, 0xB3, 0x9A, 210));
                ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
                // 'E' glyph
                BLFont tinyFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
                ctx.fill_utf8_text(BLPoint(badgeCX - 1.5, badgeCY + 1.0), tinyFont, "E", 1, BLRgba32(0xFF, 0xFF, 0xFF, 230));
            } else {
                // Hollow warning-amber circle = link (may break)
                ctx.set_stroke_style(BLRgba32(0xE8, 0xB3, 0x00, 190));
                ctx.set_stroke_width(0.5);
                ctx.stroke_circle(BLCircle(badgeCX, badgeCY, badgeR));
                // Chain-link '⚯' approximated with 'L'
                BLFont tinyFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
                ctx.fill_utf8_text(BLPoint(badgeCX - 1.3, badgeCY + 1.0), tinyFont, "L", 1, BLRgba32(0xE8, 0xB3, 0x00, 200));
            }
        }

        ctx.restore();
    }

    // =========================================================================
    // DUPLICATION
    // =========================================================================

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<AttachmentObject>(*this);
    }

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
