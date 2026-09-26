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

#include "app/context_menu_item.hpp"
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
    // FILE OPEN & PATH MANAGEMENT
    // =========================================================================

    /**
     * @brief Checks whether the target file actually exists on the filesystem.
     * 
     * Working Process:
     *   - Verifies physical disk presence via FileManager::Exists(filePath) for both linked
     *     and embedded attachments (protects against missing, deleted, or corrupted sidecar files).
     *   - Uses a cached boolean flag to avoid performing synchronous OS filesystem stat calls
     *     on every frame of the high-framerate render loop.
     *
     * @param forceCheck When true, bypasses the cache and queries disk immediately.
     * @return true if the referenced file exists on disk; false otherwise.
     */
    bool IsFileValid(bool forceCheck = false) const {
        if (forceCheck || !m_validityChecked) {
            m_isFileValidCached = !filePath.empty() && FileManager::Exists(filePath);
            m_validityChecked = true;
        }
        return m_isFileValidCached;
    }

    /**
     * @brief Updates the target file path (re-linking a moved or renamed file).
     *
     * Working Process:
     *   1. Updates `filePath` to `newPath`.
     *   2. If `updateDisplayName` is true, extracts the new filename as `displayName`.
     *   3. Forces a re-check of file existence so badge and border visuals update immediately.
     *
     * @param newPath Absolute filesystem path (or sidecar-relative path).
     * @param updateDisplayName When true, refreshes displayName to match the new file name.
     */
    void SetFilePath(const std::string& newPath, bool updateDisplayName = true) {
        filePath = newPath;
        if (updateDisplayName) {
            std::string fname = FileManager::GetFileName(newPath);
            if (!fname.empty()) {
                displayName = fname;
            }
        }
        m_validityChecked = false;
        IsFileValid(true);
    }

    /**
     * @brief Opens the linked or embedded file with the OS default application.
     * 
     * @return true if successfully launched by the operating system.
     */
    bool OpenFile() const {
        return FileManager::OpenWithDefaultApp(filePath);
    }

    /**
     * @brief Prompts user to select a replacement file on disk and updates filePath.
     *
     * Working Process:
     *   Delegates to FileManager::ShowOpenFileDialog to display the native OS file picker.
     *   If a file is selected and exists on disk, updates `filePath` and resets validation caches.
     *
     * @return true if a valid file was selected and relinked, false if cancelled.
     */
    bool LocateAndRelinkFile() {
        std::string newPath = FileManager::ShowOpenFileDialog("Locate Attachment File");
        if (!newPath.empty() && FileManager::Exists(newPath)) {
            SetFilePath(newPath, false);
            return true;
        }
        return false;
    }

    // =========================================================================
    // TRANSFORM — Translation Only (Locked Scale & Rotation)
    // =========================================================================


    /**
     * @brief Applies a 2D affine transformation while locking badge dimensions and rotation.
     *
     * Mathematical Process & Theory:
     *   When an object is transformed as part of a multi-object group (e.g. scaling, rotating,
     *   or translating around an arbitrary group pivot (cx, cy)), the full transformation matrix M is:
     *
     *     M = [ m00  m01  0 ]
     *         [ m10  m11  0 ]
     *         [ m20  m21  1 ]
     *
     *   If we applied M directly to an attachment badge, its visual chip dimensions (50mm x 18mm)
     *   would distort (rubber-band stretch) and rotate awkwardly. As a fixed-size UI chip,
     *   the badge must preserve its exact aspect ratio and horizontal orientation, but still
     *   follow the spatial trajectory of the group.
     *
     *   To achieve this, the chip's anchor position (worldX, worldY) is projected through M:
     *     x_new = (worldX * m00) + (worldY * m10) + m20
     *     y_new = (worldX * m01) + (worldY * m11) + m21
     *
     *   The translational displacement of the anchor is:
     *     dx = x_new - worldX
     *     dy = y_new - worldY
     *
     *   We construct a pure translation matrix T = make_translation(dx, dy) and accumulate it
     *   into `transform`. This guarantees that:
     *     1. Scale factors (m00, m11) and shears/rotations (m01, m10) never distort the badge.
     *     2. The chip's world position tracks the group selection correctly.
     *
     * @param matrix 2D affine transformation matrix passed from gizmo or group operation.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override {
        // Project current anchor point through incoming matrix to calculate translation delta
        const double newX = (worldX * matrix.m00) + (worldY * matrix.m10) + matrix.m20;
        const double newY = (worldX * matrix.m01) + (worldY * matrix.m11) + matrix.m21;

        const double dx = newX - worldX;
        const double dy = newY - worldY;

        // Apply pure translation to maintain locked scale and rotation invariants
        BLMatrix2D translationOnly = BLMatrix2D::make_translation(dx, dy);
        transform.post_transform(translationOnly);
        UpdateBounds();
    }

    /**
     * @brief Bakes accumulated translation from transform into worldX/Y and resets to identity.
     *
     * Mathematical Process:
     *   Because ApplyTransform guarantees that `transform` contains strictly translational
     *   components (scale = 1.0, rotation = 0.0), baking simply transfers the translation
     *   offsets (m20, m21) permanently into world-space coordinates:
     *     worldX += transform.m20
     *     worldY += transform.m21
     *   The transform matrix is then reset to identity and world bounds are updated.
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
        
        // check if object is even visible
        if (!isVisible) return;

        // create blend2d context and apply transformation
        ctx.save();
        ctx.apply_transform(transform);

        // the look of our object
        const double x  = worldX;
        const double y  = worldY;
        const double w  = chipW;
        const double h  = chipH;
        const double r  = 2.0;          // Corner radius (mm)
        const double bw = 8.0;          // Type band width (mm)

        // Draws main card body
        ctx.set_fill_style(BLRgba32(0x1E, 0x20, 0x28, static_cast<uint8_t>(opacity * 235)));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

        // Type color band on the left edge (clip to round rect boundary)
        BLRgba32 bandCol = GetTypeColor();
        ctx.save();
        ctx.clip_to_rect(BLRect(x, y, bw, h));
        ctx.set_fill_style(bandCol);
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));
        ctx.restore();

        
        bool isBroken = !IsFileValid();

        // Border (Warning red if broken, dark slate if valid)
        if (isBroken) {
            ctx.set_stroke_style(BLRgba32(0xE8, 0x11, 0x23, 230));
            ctx.set_stroke_width(0.6);
        } else {
            ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 200));
            ctx.set_stroke_width(0.4);
        }
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

        // Draw display name label (tinted warning red if broken)
        BLFont labelFont = FontManager::Instance().GetFont("Segoe UI", 3.4f, false);
        BLRgba32 labelCol = isBroken 
            ? BLRgba32(0xFF, 0x88, 0x88, static_cast<uint8_t>(opacity * 255)) 
            : BLRgba32(0xF0, 0xF2, 0xF5, static_cast<uint8_t>(opacity * 255));

        // Clip text so it doesn't bleed out of chip
        ctx.save();
        ctx.clip_to_rect(BLRect(x + bw + 2.0, y + 1.0, w - bw - 4.0, h - 2.0));
        ctx.fill_utf8_text(BLPoint(x + bw + 2.5, y + h * 0.5 + 1.2), labelFont, displayName.data(), displayName.size(), labelCol);
        ctx.restore();

        // Draw embedded / link / broken mode badge in the bottom-right corner of the chip.
        // Embedded: filled teal dot with 'E'.
        // Link Valid: hollow amber dot with 'L'.
        // Link Broken: filled warning red dot with '!'.
        {
            const double badgeR  = 2.8;                              // Badge circle radius (mm)
            const double badgeCX = x + w - badgeR - 1.2;            // Centre X
            const double badgeCY = y + h - badgeR - 1.0;            // Centre Y

            if (isBroken) {
                // Filled warning red circle with white '!'
                ctx.set_fill_style(BLRgba32(0xE8, 0x11, 0x23, 235));
                ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
                BLFont warnFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
                ctx.fill_utf8_text(BLPoint(badgeCX - 0.7, badgeCY + 0.9), warnFont, "!", 1, BLRgba32(0xFF, 0xFF, 0xFF, 255));
            } else if (isEmbedded) {
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
                // Chain-link approximated with 'L'
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

    // =========================================================================
    // CONTEXT MENU & OBJECT ACTIONS
    // =========================================================================

    /**
     * @brief Injects domain-specific actions for this attachment into the context menu.
     *
     * Working Process:
     *   Injects "Open File", "Locate / Re-link File..." (if broken), "Copy File Path",
     *   and "Show in File Explorer" with explicit priority ordering.
     *   Universal canvas actions (Delete, Layering, Duplicate) are automatically provided
     *   by the ObjectActionRegistry.
     *
     * @param[in,out] actions Mutable vector of ContextMenuItem descriptors to inject into.
     */
    void CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) override {
        bool isValid = IsFileValid();

        if (isValid) {
            // 1. Open (Primary action when file is valid, order: 10)
            Folio::ContextMenuItem openAct;
            openAct.label = "Open";
            openAct.shortcut = "Double-Click";
            openAct.icon = "🚀";
            openAct.iconKey = "open";
            openAct.order = 10;
            openAct.isEnabled = true;
            openAct.onTrigger = [this]() { OpenFile(); };
            actions.push_back(std::move(openAct));
        } else {
            // 1. Re-link (Replaces Open as primary action when broken/missing, order: 10)
            Folio::ContextMenuItem relinkAct;
            relinkAct.label = "Locate / Re-link File...";
            relinkAct.icon = "⚠️";
            relinkAct.iconKey = "warning";
            relinkAct.order = 10;
            relinkAct.isEnabled = true;
            relinkAct.onTrigger = [this]() { LocateAndRelinkFile(); };
            actions.push_back(std::move(relinkAct));
        }

        // 3. Copy File Path (order: 30)
        Folio::ContextMenuItem copyPathAct;
        copyPathAct.label = "Copy File Path";
        copyPathAct.icon = "📋";
        copyPathAct.iconKey = "copy";
        copyPathAct.order = 30;
        copyPathAct.onTrigger = [this]() { SDL_SetClipboardText(filePath.c_str()); };
        actions.push_back(std::move(copyPathAct));

        // 4. Show in File Explorer (order: 40)
        Folio::ContextMenuItem explorerAct;
        explorerAct.label = "Show in File Explorer";
        explorerAct.icon = "📁";
        explorerAct.iconKey = "folder";
        explorerAct.order = 40;
        explorerAct.isEnabled = isValid;
        explorerAct.onTrigger = [this]() {
            std::string parentDir = FileManager::GetParentPath(filePath);
            if (!parentDir.empty() && FileManager::Exists(parentDir)) {
                FileManager::OpenWithDefaultApp(parentDir);
            }
        };
        actions.push_back(std::move(explorerAct));
    }

private:
    mutable bool m_isFileValidCached = true;
    mutable bool m_validityChecked = false;
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
