#pragma once
/**
 * =========================================================================================
 * @file core/objects/attachment_container.hpp
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
struct CanvasContext;

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

    AttachmentObject();
    AttachmentObject(const std::string &path, const std::string &name,
                     const std::string &mime = "", bool embedded = false);

    // =========================================================================
    // FILE OPEN & PATH MANAGEMENT
    // =========================================================================

    [[nodiscard]] bool IsFileValid() const noexcept { return isFileValid; }
    void SetFileValid(bool valid) noexcept { isFileValid = valid; }
    void SetFilePath(const std::string &newPath, bool updateDisplayName = true);

    bool OpenFile() const;
    bool LocateAndRelinkFile();

    // Polymorphic interaction hook
    bool OnPointerClick(const Folio::CanvasContext& ctx) override;

    // =========================================================================
    // TRANSFORM — Translation Only (Locked Scale & Rotation)
    // =========================================================================

    void ApplyTransform(const BLMatrix2D &matrix) override;
    void BakeTransform() override;

    [[nodiscard]] GizmoStyle GetGizmoStyle() const noexcept override {
        return GizmoStyle::MoveOnly;
    }

    // =========================================================================
    // RENDERING & LIFECYCLE
    // =========================================================================

    void Render(BLContext &ctx, const Viewport &viewport) const override;
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;

    // =========================================================================
    // CONTEXT MENU & OBJECT ACTIONS
    // =========================================================================

    void CustomizeActions(std::vector<Folio::ContextMenuItem> &actions) override;

private:
    BLRgba32 GetTypeColor() const;
};

} // namespace Folio
