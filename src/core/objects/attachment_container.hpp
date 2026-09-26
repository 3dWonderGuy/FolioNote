#pragma once
/**
 * =========================================================================================
 * @file core/objects/attachment_container.hpp
 * @brief Canvas Object Representing a Pinned File Attachment Chip
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTERACTION MODEL:
 * -----------------------------------------
 * AttachmentObject provides an interactive, fixed-size vector chip (50mm x 18mm) pinned to
 * the infinite canvas, referencing either external files (Link Mode) or internal package assets
 * (Embedded Mode).
 *
 * 1. Attachment Storage Modes:
 *    - Link Mode (`isEmbedded == false`):
 *        `filePath` stores an absolute filesystem path. The file is not duplicated.
 *        Renders an amber badge ('L'). If the external file is deleted or moved, the link
 *        breaks dynamically and shows a warning state.
 *    - Embedded Mode (`isEmbedded == true`):
 *        `filePath` stores a package-relative path (e.g. "attachments/<uuid>_doc.xlsx").
 *        Renders a teal badge ('E'). Files are resolved relative to the active notebook root
 *        or via a registered path resolver.
 *
 * 2. Interaction & Visual States:
 *    - Valid State: Smooth dark slate card with left extension band, icon, and mode badge.
 *    - Broken State: Warning red border, tinted text label, and red warning ('!') badge.
 *    - Primary Actions: "Open" when valid; automatically swaps to "Locate / Re-link File..."
 *      when broken or missing.
 *
 * 3. Spatial & Transform Invariants:
 *    - Fixed Chip Dimensions: Fixed at `chipW` (50mm) x `chipH` (18mm) in world millimeters.
 *    - Translation-Only Gizmo: Locked to `GizmoStyle::MoveOnly`. Multi-object group rotations
 *      and scaling matrices project only the anchor translation delta (dx, dy), preserving
 *      horizontal orientation and aspect ratio without distortion.
 *
 * 4. High-Performance Rendering:
 *    - Font Cache: Utilizes a shared static `AttachmentFontCache` to eliminate per-frame
 *      mutex acquisitions and hash lookups in the 120 FPS render loop.
 *    - Existence Caching: Caches filesystem verification to prevent synchronous stat I/O
 *      during drawing operations.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cctype>

#include <blend2d/blend2d.h>

#include "app/context_menu_item.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/text/font_manager.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

/**
 * @struct AttachmentFontCache
 * @brief Thread-safe, cached Blend2D font instances used across all attachment chips.
 *
 * RATIONALE:
 * Calling `FontManager::Instance().GetFont(...)` on every frame for every chip causes
 * repeated mutex lock acquisitions and map queries. Pre-allocating and caching static font
 * handles provides zero-overhead rendering in the 120 FPS canvas pipeline.
 */
struct AttachmentFontCache {
    BLFont badgeFont;        ///< 3.2pt Bold font for file extension text in the type band
    BLFont labelFont;        ///< 3.4pt Regular font for the main filename display label
    BLFont badgeSymbolFont;  ///< 2.6pt Bold font for mode indicator glyphs ('E', 'L', '!')
    bool isInitialized = false;

    [[nodiscard]] static const AttachmentFontCache& Get() {
        static AttachmentFontCache s_cache;
        if (!s_cache.isInitialized) {
            s_cache.badgeFont = FontManager::Instance().GetFont("Segoe UI", 3.2f, true);
            s_cache.labelFont = FontManager::Instance().GetFont("Segoe UI", 3.4f, false);
            s_cache.badgeSymbolFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
            s_cache.isInitialized = true;
        }
        return s_cache;
    }
};

/**
 * @class AttachmentObject
 * @brief Fixed-size interactive canvas chip linking to an external or embedded file.
 */
class AttachmentObject : public CanvasObject {
public:
    // =========================================================================
    // TYPE DEFINITIONS & HOOKS
    // =========================================================================
    using EmbeddedPathResolver = std::function<std::string(const std::string& relativePath)>;

    // =========================================================================
    // FIELDS
    // =========================================================================
    std::string filePath;       ///< Absolute path (link mode) or sidecar-relative path (embedded mode)
    std::string displayName;    ///< Filename label shown on the chip body
    std::string mimeType;       ///< MIME type identifier (e.g. "application/pdf", "image/png")

    /// Attachment storage mode:
    ///   true  = embedded in notebook package folder (portable)
    ///   false = absolute path link to disk file
    bool isEmbedded = false;

    /// Optional callback invoked upon successful path re-linking for undo/redo and dirty notification
    std::function<void(const std::string& oldPath, const std::string& newPath,
                       const std::string& oldName, const std::string& newName)> onRelinkCallback = nullptr;

    /// Fixed chip dimensions in world millimeters (non-resizable)
    static constexpr double chipW = 50.0;
    static constexpr double chipH = 18.0;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor for deserialization and database loading.
     */
    AttachmentObject() {
        type = ObjectType::AttachmentFile;
        worldWidth = chipW;
        worldHeight = chipH;
        UpdateBounds();
    }

    /**
     * @brief Parameterized constructor for creating new attachment chips.
     *
     * @param path Target filesystem path (absolute or package-relative).
     * @param name Display label shown on the chip.
     * @param mime Optional MIME type classification.
     * @param embedded True for notebook-embedded files, false for external links.
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
    // PATH RESOLUTION & VALIDATION
    // =========================================================================

    /**
     * @brief Registers a global resolver for embedded relative paths (e.g. from active notebook).
     *
     * @param resolver Callback mapping relative paths to absolute package paths.
     */
    static void SetEmbeddedPathResolver(EmbeddedPathResolver resolver) {
        s_embeddedPathResolver = std::move(resolver);
    }

    /**
     * @brief Resolves `filePath` into an absolute, normalized filesystem path on disk.
     *
     * RESOLUTION PROCESS:
     *   1. If `isEmbedded` is false (Link mode): returns `filePath` directly normalized.
     *   2. If `isEmbedded` is true (Embedded mode):
     *      a. If `notebookRootDir` is provided, joins `notebookRootDir` with `filePath`.
     *      b. If `s_embeddedPathResolver` is registered, queries it for active notebook path.
     *      c. If `filePath` is already absolute, returns normalized `filePath`.
     *      d. Fallback: joins `filePath` against `FileManager::GetAppRootDirectory()`.
     *
     * @param notebookRootDir Optional override for the parent notebook package folder.
     * @return Absolute normalized UTF-8 filesystem path string.
     */
    [[nodiscard]] std::string GetResolvedPath(const std::string& notebookRootDir = "") const {
        if (filePath.empty()) return "";

        if (!isEmbedded) {
            return FileManager::NormalizeSeparators(filePath);
        }

        // Embedded relative resolution:
        if (!notebookRootDir.empty()) {
            return FileManager::NormalizeSeparators(FileManager::JoinPath(notebookRootDir, filePath));
        }

        if (s_embeddedPathResolver) {
            std::string resolved = s_embeddedPathResolver(filePath);
            if (!resolved.empty()) {
                return FileManager::NormalizeSeparators(resolved);
            }
        }

        if (FileManager::IsAbsolutePath(filePath)) {
            return FileManager::NormalizeSeparators(filePath);
        }

        return FileManager::NormalizeSeparators(FileManager::JoinPath(FileManager::GetAppRootDirectory(), filePath));
    }

    /**
     * @brief Tests whether the referenced file exists on disk.
     *
     * PERFORMANCE & WORKING PROCESS:
     *   - Resolves target path via `GetResolvedPath(notebookRootDir)`.
     *   - Uses cached boolean to avoid issuing synchronous OS stat calls during 120 FPS rendering.
     *   - Cache is invalidated on path change or forced check.
     *
     * @param notebookRootDir Optional parent directory for embedded files.
     * @param forceCheck When true, bypasses the cache and queries disk immediately.
     * @return true if the physical file exists on disk; false otherwise.
     */
    bool IsFileValid(const std::string& notebookRootDir = "", bool forceCheck = false) const {
        if (forceCheck || !m_validityChecked) {
            std::string resolved = GetResolvedPath(notebookRootDir);
            m_isFileValidCached = !resolved.empty() && FileManager::Exists(resolved);
            m_validityChecked = true;
        }
        return m_isFileValidCached;
    }

    /**
     * @brief Updates the target file path and resets validation caches.
     *
     * @param newPath New filesystem path (absolute or package-relative).
     * @param updateDisplayName When true, extracts filename stem for displayName.
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
        IsFileValid("", true);
    }

    /**
     * @brief Launches the linked or embedded file with the system default application.
     *
     * @param notebookRootDir Optional notebook directory for embedded resolution.
     * @return true if successfully launched by the operating system.
     */
    bool OpenFile(const std::string& notebookRootDir = "") const {
        std::string resolved = GetResolvedPath(notebookRootDir);
        if (resolved.empty()) return false;
        return FileManager::OpenWithDefaultApp(resolved);
    }

    /**
     * @brief Prompts the user to locate a replacement file on disk and updates filePath.
     *
     * WORKING PROCESS & UNDO/REDO HOOK:
     *   1. Displays the native OS file picker via `FileManager::ShowOpenFileDialog`.
     *   2. If a valid file is selected, captures `oldPath` and `oldDisplayName`.
     *   3. Applies `newPath` and updates cached validation state.
     *   4. Invocates optional `onRelinked` callback allowing callers (e.g. Session/Command History)
     *      to record an undoable `RelinkAttachmentCommand` and flag document modified.
     *
     * @param notebookRootDir Optional parent directory for path resolution.
     * @param onRelinked Optional callback: `void(oldPath, newPath, oldName, newName)`.
     * @return true if a valid file was selected and relinked; false if cancelled.
     */
    bool LocateAndRelinkFile(
        const std::string& notebookRootDir = "",
        std::function<void(const std::string& oldPath, const std::string& newPath,
                           const std::string& oldName, const std::string& newName)> onRelinked = nullptr
    ) {
        std::string newPath = FileManager::ShowOpenFileDialog("Locate Attachment File");
        if (newPath.empty() || !FileManager::Exists(newPath)) {
            return false;
        }

        std::string oldPath = filePath;
        std::string oldName = displayName;

        SetFilePath(newPath, false);

        if (onRelinked) {
            onRelinked(oldPath, filePath, oldName, displayName);
        } else if (onRelinkCallback) {
            onRelinkCallback(oldPath, filePath, oldName, displayName);
        }
        return true;
    }

    // =========================================================================
    // TRANSFORM — Translation Only (Locked Scale & Rotation)
    // =========================================================================

    /**
     * @brief Applies a 2D affine transformation while preserving locked badge scale & rotation.
     *
     * MATHEMATICAL MODEL:
     *   Multi-object transformations (e.g. group rotations/scaling around pivot C) use matrix:
     *     M = [ m00  m01  0 ]
     *         [ m10  m11  0 ]
     *         [ m20  m21  1 ]
     *
     *   To prevent distortion of the fixed 50mm x 18mm chip, the anchor position is projected:
     *     x_new = (worldX * m00) + (worldY * m10) + m20
     *     y_new = (worldX * m01) + (worldY * m11) + m21
     *
     *   The translational delta (dx = x_new - worldX, dy = y_new - worldY) is accumulated
     *   as pure translation, ensuring scale and rotation invariants remain locked.
     *
     * @param matrix 2D affine transformation matrix.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override {
        const double newX = (worldX * matrix.m00) + (worldY * matrix.m10) + matrix.m20;
        const double newY = (worldX * matrix.m01) + (worldY * matrix.m11) + matrix.m21;

        const double dx = newX - worldX;
        const double dy = newY - worldY;

        BLMatrix2D translationOnly = BLMatrix2D::make_translation(dx, dy);
        transform.post_transform(translationOnly);
        UpdateBounds();
    }

    /**
     * @brief Bakes accumulated translation into worldX/Y and resets transform to identity.
     */
    void BakeTransform() override {
        worldX += transform.m20;
        worldY += transform.m21;
        transform = BLMatrix2D::make_identity();
        UpdateBounds();
    }

    /**
     * @brief Attachment chips use MoveOnly interaction (no resize corner handles).
     */
    [[nodiscard]] GizmoStyle GetGizmoStyle() const noexcept override {
        return GizmoStyle::MoveOnly;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the attachment chip using Blend2D vector graphics.
     *
     * @param ctx Blend2D graphics rendering context.
     * @param viewport Active camera viewport.
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
        const double bw = 8.0;          // Extension band width (mm)

        // Retrieve pre-cached static fonts (zero mutex/lookup overhead in render loop)
        const auto& fonts = AttachmentFontCache::Get();

        // 1. Draw main chip card body
        ctx.set_fill_style(BLRgba32(0x1E, 0x20, 0x28, static_cast<uint8_t>(opacity * 235)));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

        // 2. Type color band on the left edge
        BLRgba32 bandCol = GetTypeColor();
        ctx.save();
        ctx.clip_to_rect(BLRect(x, y, bw, h));
        ctx.set_fill_style(bandCol);
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));
        ctx.restore();

        // 3. Border (Warning red if broken, dark slate if valid)
        bool isBroken = !IsFileValid();
        if (isBroken) {
            ctx.set_stroke_style(BLRgba32(0xE8, 0x11, 0x23, 230));
            ctx.set_stroke_width(0.6);
        } else {
            ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x55, 200));
            ctx.set_stroke_width(0.4);
        }
        ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

        // 4. File extension inside left color band
        std::string ext = "";
        auto dot = displayName.rfind('.');
        if (dot != std::string::npos && dot + 1 < displayName.size()) {
            ext = displayName.substr(dot + 1);
            for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (ext.size() > 4) ext = ext.substr(0, 4);
        } else {
            ext = "FILE";
        }
        ctx.fill_utf8_text(BLPoint(x + 1.2, y + h * 0.5 + 1.1), fonts.badgeFont, ext.data(), ext.size(), BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));

        // 5. Display filename label (tinted warning red if broken)
        BLRgba32 labelCol = isBroken 
            ? BLRgba32(0xFF, 0x88, 0x88, static_cast<uint8_t>(opacity * 255)) 
            : BLRgba32(0xF0, 0xF2, 0xF5, static_cast<uint8_t>(opacity * 255));

        ctx.save();
        ctx.clip_to_rect(BLRect(x + bw + 2.0, y + 1.0, w - bw - 4.0, h - 2.0));
        ctx.fill_utf8_text(BLPoint(x + bw + 2.5, y + h * 0.5 + 1.2), fonts.labelFont, displayName.data(), displayName.size(), labelCol);
        ctx.restore();

        // 6. Mode & Status Badge (Bottom-Right corner)
        {
            const double badgeR  = 2.8;                              // Radius in mm
            const double badgeCX = x + w - badgeR - 1.2;            // Center X
            const double badgeCY = y + h - badgeR - 1.0;            // Center Y

            if (isBroken) {
                // Filled warning red circle with white '!'
                ctx.set_fill_style(BLRgba32(0xE8, 0x11, 0x23, 235));
                ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
                ctx.fill_utf8_text(BLPoint(badgeCX - 0.7, badgeCY + 0.9), fonts.badgeSymbolFont, "!", 1, BLRgba32(0xFF, 0xFF, 0xFF, 255));
            } else if (isEmbedded) {
                // Filled teal circle with white 'E' (Embedded/Safe)
                ctx.set_fill_style(BLRgba32(0x00, 0xB3, 0x9A, 210));
                ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
                ctx.fill_utf8_text(BLPoint(badgeCX - 1.5, badgeCY + 1.0), fonts.badgeSymbolFont, "E", 1, BLRgba32(0xFF, 0xFF, 0xFF, 230));
            } else {
                // Hollow amber circle with amber 'L' (Link Mode)
                ctx.set_stroke_style(BLRgba32(0xE8, 0xB3, 0x00, 190));
                ctx.set_stroke_width(0.5);
                ctx.stroke_circle(BLCircle(badgeCX, badgeCY, badgeR));
                ctx.fill_utf8_text(BLPoint(badgeCX - 1.3, badgeCY + 1.0), fonts.badgeSymbolFont, "L", 1, BLRgba32(0xE8, 0xB3, 0x00, 200));
            }
        }

        ctx.restore();
    }

    // =========================================================================
    // CLONING
    // =========================================================================

    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<AttachmentObject>(*this);
    }

    // =========================================================================
    // CONTEXT MENU & OBJECT ACTIONS
    // =========================================================================

    /**
     * @brief Injects domain-specific actions for this attachment into the context menu.
     *
     * @param[in,out] actions Mutable vector of ContextMenuItem descriptors.
     */
    void CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) override {
        bool isValid = IsFileValid();

        if (isValid) {
            // 1. Open File (Primary action when file is valid, order: 10)
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

        // 2. Copy File Path (order: 30) - routes through FileManager abstraction
        Folio::ContextMenuItem copyPathAct;
        copyPathAct.label = "Copy File Path";
        copyPathAct.icon = "📋";
        copyPathAct.iconKey = "copy";
        copyPathAct.order = 30;
        copyPathAct.onTrigger = [this]() {
            FileManager::SetClipboardText(this->GetResolvedPath());
        };
        actions.push_back(std::move(copyPathAct));

        // 3. Show in File Explorer (order: 40)
        Folio::ContextMenuItem explorerAct;
        explorerAct.label = "Show in File Explorer";
        explorerAct.icon = "📁";
        explorerAct.iconKey = "folder";
        explorerAct.order = 40;
        explorerAct.isEnabled = isValid;
        explorerAct.onTrigger = [this]() {
            std::string resolved = this->GetResolvedPath();
            std::string parentDir = FileManager::GetParentPath(resolved);
            if (!parentDir.empty() && FileManager::Exists(parentDir)) {
                FileManager::OpenWithDefaultApp(parentDir);
            }
        };
        actions.push_back(std::move(explorerAct));
    }

private:
    inline static EmbeddedPathResolver s_embeddedPathResolver = nullptr;
    mutable bool m_isFileValidCached = true;
    mutable bool m_validityChecked = false;

    /**
     * @brief Returns a distinguishing color for the left type band based on file extension.
     */
    [[nodiscard]] BLRgba32 GetTypeColor() const {
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
