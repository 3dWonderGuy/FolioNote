#pragma once
/**
 * =========================================================================================
 * @file core/objects/attachment_container/attachment_container.hpp
 * @brief Canvas object representing a file attachment chip pinned to the canvas.
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INVARIANTS:
 * ----------------------------------
 * AttachmentObject is a lightweight, non-resizable interactive chip providing
 * quick access to an external or embedded file (documents, spreadsheets, images, etc.).
 *
 * 1. Attachment Modes:
 *    - Link mode (isEmbedded == false):
 *        `filePath` stores the original absolute path on disk. The file is NOT copied.
 *        The chip renders a link badge. If the file is moved/deleted, the link breaks.
 *    - Embedded mode (isEmbedded == true):
 *        The file was copied into the notebook sidecar package folder at attach time.
 *        `filePath` stores the sidecar-relative path. Portable across notebook moves.
 *
 * 2. Invariant Geometry & Interaction:
 *    - Fixed-size badge (chipW x chipH mm) with body-move only via GizmoStyle::MoveOnly.
 *    - Double-click dispatches to injected `CanvasContext::fileManager` (zero downcasting).
 *    - Zero direct includes of typography or filesystem managers in this header.
 */

#include <string>
#include <functional>
#include <memory>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/objects/object_config.hpp"

namespace Folio {

struct ContextMenuItem;

/**
 * @brief Fixed-size canvas chip linking to an external or embedded file.
 */
class AttachmentObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    std::string filePath;    ///< Absolute path (link mode) or sidecar-relative path (embedded mode)
    std::string displayName; ///< Shown on the chip label (usually the filename)
    std::string mimeType;    ///< e.g. "application/pdf", "image/png", "text/plain"

    bool isEmbedded = false; ///< True if copied into sidecar; false if linked directly to disk
    bool isFileValid = true; ///< Cached validity state, evaluated by parent coordinator

    /// Optional callback invoked upon successful path re-linking for undo/redo recording
    std::function<void(const std::string& oldPath, const std::string& newPath,
                       const std::string& oldName, const std::string& newName)> onRelinkCallback = nullptr;

    /// Optional callback invoked when user clicks re-link action
    std::function<void()> onRelinkRequested = nullptr;

    /// Default chip dimensions in world mm (delegated to ObjectConfig)
    static constexpr double defaultChipW = 50.0;
    static constexpr double defaultChipH = 18.0;
    static constexpr double chipW = defaultChipW;
    static constexpr double chipH = defaultChipH;

    static double GetDefaultChipWidth() noexcept {
        const double w = ObjectConfig::Get().attachmentChipWidthMm;
        return (w >= 10.0) ? w : defaultChipW;
    }

    static double GetDefaultChipHeight() noexcept {
        const double h = ObjectConfig::Get().attachmentChipHeightMm;
        return (h >= 5.0) ? h : defaultChipH;
    }

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Constructs an uninitialized AttachmentObject chip with MoveOnly gizmo styling.
     * Dimensions are seeded from ObjectConfig::Get().
     */
    AttachmentObject();

    /**
     * @brief Constructs an AttachmentObject configured with path, display label, and embedding mode.
     * @param[in] path     Filesystem path (absolute for linked files, sidecar-relative for embedded).
     * @param[in] name     Human-readable display title for the chip.
     * @param[in] mime     MIME type string (e.g. "application/pdf").
     * @param[in] embedded True if file is copied into the notebook sidecar bundle.
     */
    AttachmentObject(const std::string &path, const std::string &name,
                     const std::string &mime = "", bool embedded = false);

    // =========================================================================
    // FILE OPEN & PATH MANAGEMENT
    // =========================================================================

    /// @brief Checks whether the target file currently exists on the local filesystem.
    [[nodiscard]] bool IsFileValid() const noexcept { return isFileValid; }

    /// @brief Updates cached filesystem presence flag.
    void SetFileValid(bool valid) noexcept { isFileValid = valid; }

    /**
     * @brief Updates target file path and optionally synchronizes displayName with new basename.
     * @param[in] newPath New filesystem path.
     * @param[in] updateDisplayName If true, resets displayName to the filename portion of newPath.
     */
    void SetFilePath(const std::string &newPath, bool updateDisplayName = true);

    /**
     * @brief Launches the attached file using the OS default application handler.
     * @return true if launched successfully; false on OS error.
     */
    bool OpenFile() const;

    /**
     * @brief Displays an OS file picker dialog to locate and repair a broken file link.
     * @return true if file was located and re-linked; false if canceled.
     */
    bool LocateAndRelinkFile();

    // =========================================================================
    // TRANSFORM — Translation Only (Locked Scale & Rotation)
    // =========================================================================

    /**
     * @brief Applies translation deltas while discarding rotation and scale to enforce fixed chip size.
     * @param[in] matrix Incoming 2D affine transform.
     */
    void ApplyTransform(const BLMatrix2D &matrix) override;

    /// @brief Commits active translation deltas into worldX/worldY and resets matrix to identity.
    void BakeTransform() override;

    // =========================================================================
    // RENDERING & LIFECYCLE
    // =========================================================================

    /**
     * @brief Draws the theme-aware attachment chip into the Blend2D context.
     * @param[in,out] ctx Target 2D raster context.
     * @param[in]     viewport Active camera viewport.
     */
    void Render(BLContext &ctx, const Viewport &viewport) const override;

    /// @brief Creates an exact deep polymorphic copy of this AttachmentObject.
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;

    // =========================================================================
    // CONTEXT MENU & OBJECT ACTIONS
    // =========================================================================

    /**
     * @brief Appends contextual actions (Open, Re-link, Copy Path, Open Folder) into the right-click menu.
     * @param[in,out] actions Menu action vector to append context items to.
     */
    void CustomizeActions(std::vector<Folio::ContextMenuItem> &actions) override;

private:
    /**
     * @brief Resolves color-coding for the file extension badge based on extension.
     * @return BLRgba32 Accent color for the badge strip.
     */
    BLRgba32 GetTypeColor() const;
};

} // namespace Folio
