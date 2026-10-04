/**
 * =========================================================================================
 * @file core/objects/attachment_container/attachment_container.cpp
 * @brief Implementation of AttachmentObject Rendering, File Launching, and Interaction
 * =========================================================================================
 *
 * GENERAL WORKING PROCESS:
 * -------------------------
 * This translation unit houses all concrete filesystem (FileManager) and typography
 * (FontManager) operations for AttachmentObject chips, isolating them away from
 * attachment_container.hpp to eliminate compilation cascades.
 */

#include "core/objects/attachment_container/attachment_container.hpp"
#include "core/objects/object_registry.hpp"
#include "core/text/font_manager.hpp"
#include "io/file_manager.hpp"
#include "app/context_menu_item.hpp"
#include "utils/logger.hpp"
#include <SDL3/SDL.h>
#include <cctype>

namespace Folio {

namespace {

// Static self-registration into ObjectRegistry
const bool s_attachmentRegistered = []() {
    ObjectRegistry::Register<AttachmentObject>(
        ObjectType::AttachmentFile,
        "AttachmentFile",
        "📎",
        true
    );
    return true;
}();

} // anonymous namespace

AttachmentObject::AttachmentObject() {
    type = ObjectType::AttachmentFile;
    gizmoStyle = GizmoStyle::MoveOnly;
    worldWidth = GetDefaultChipWidth();
    worldHeight = GetDefaultChipHeight();
    UpdateBounds();
}

AttachmentObject::AttachmentObject(const std::string &path, const std::string &name,
                                   const std::string &mime, bool embedded)
    : filePath(path), displayName(name), mimeType(mime), isEmbedded(embedded) {
    type = ObjectType::AttachmentFile;
    gizmoStyle = GizmoStyle::MoveOnly;
    worldWidth = GetDefaultChipWidth();
    worldHeight = GetDefaultChipHeight();
    UpdateBounds();
}

void AttachmentObject::SetFilePath(const std::string &newPath, bool updateDisplayName) {
    filePath = newPath;
    if (updateDisplayName) {
        std::string fname = FileManager::GetFileName(newPath);
        if (!fname.empty()) {
            displayName = fname;
        }
    }
}

bool AttachmentObject::OpenFile() const {
    return FileManager::OpenWithDefaultApp(filePath);
}

bool AttachmentObject::LocateAndRelinkFile() {
    if (onRelinkRequested) {
        onRelinkRequested();
        return true;
    }
    std::string newPath = FileManager::ShowOpenFileDialog("Locate Attachment File");
    if (!newPath.empty() && FileManager::Exists(newPath)) {
        std::string oldPath = filePath;
        std::string oldName = displayName;
        SetFilePath(newPath, false);
        SetFileValid(true);
        if (onRelinkCallback) {
            onRelinkCallback(oldPath, filePath, oldName, displayName);
        }
        return true;
    }
    return false;
}

/**
 * @brief Applies an affine transformation matrix to the attachment chip.
 *
 * MATHEMATICAL PROCESS & INVARIANTS:
 * -----------------------------------
 * Attachment chips are fixed-dimension UI cards (GizmoStyle::MoveOnly). They must
 * never scale, stretch, or rotate with the canvas camera or group transformations:
 * 1. Project the current top-left anchor point (worldX, worldY) through the incoming
 *    2x3 affine matrix `M`:
 *      newX = (worldX * M.m00) + (worldY * M.m10) + M.m20
 *      newY = (worldX * M.m01) + (worldY * M.m11) + M.m21
 * 2. Calculate purely translational displacement deltas:
 *      dx = newX - worldX
 *      dy = newY - worldY
 * 3. Construct a translation-only affine matrix `T = BLMatrix2D::make_translation(dx, dy)`.
 *    This completely discards scale, shear, and rotation components while keeping the
 *    chip position pinned accurately in world space.
 * 4. Post-multiplies `T` into the local `transform` and recomputes axis-aligned bounding box.
 *
 * @param[in] matrix Incoming 2D transformation matrix from canvas or gizmo manipulation.
 */
void AttachmentObject::ApplyTransform(const BLMatrix2D &matrix) {
    const double newX = (worldX * matrix.m00) + (worldY * matrix.m10) + matrix.m20;
    const double newY = (worldX * matrix.m01) + (worldY * matrix.m11) + matrix.m21;

    const double dx = newX - worldX;
    const double dy = newY - worldY;

    BLMatrix2D translationOnly = BLMatrix2D::make_translation(dx, dy);
    transform.post_transform(translationOnly);
    UpdateBounds();
}

/**
 * @brief Commits accumulated translation deltas into world coordinates and resets transform to identity.
 *
 * MATHEMATICAL PROCESS:
 * Extracts translation offsets (m20, m21) from the affine transform matrix:
 *   worldX += transform.m20;
 *   worldY += transform.m21;
 * Resets `transform` to identity to prevent unbounded matrix accumulation.
 */
void AttachmentObject::BakeTransform() {
    worldX += transform.m20;
    worldY += transform.m21;
    transform = BLMatrix2D::make_identity();
    UpdateBounds();
}

/**
 * @brief Renders the interactive attachment file card into the Blend2D context.
 *
 * GEOMETRIC LAYOUT & MATHEMATICAL COMPOSITION (Physical Canvas Millimeters):
 * --------------------------------------------------------------------------
 * The attachment card is rendered in world millimeter space:
 *   - Origin: (x, y) = (worldX, worldY)
 *   - Dimensions: w x h (typically 50.0mm x 18.0mm from ObjectConfig)
 *   - Corner Radius: r = cfg.cardCornerRadiusMm (2.0mm)
 *   - Type Band: Left strip of width bw = cfg.cardTypeBandWidthMm (8.0mm)
 *
 * VISUAL LAYERING (Back-to-Front Painter's Algorithm):
 * 1. Base Card Surface: Rounded rectangle filled with theme-aware card color (dark/light mode).
 * 2. Type Band Strip: Left-aligned colored pill based on file extension (PDF=Red, Office=Blue/Green, etc.)
 *    clipped to the card's rounded boundary.
 * 3. Type Extension Badge: Bold uppercase text centered in the type band.
 * 4. Filename Label: Scissor-clipped label preventing text bleed outside the right margin.
 * 5. State Indicator Pill: Bottom-right status badge:
 *      - Embedded ('E' Teal): Safe copy stored inside the notebook sidecar bundle.
 *      - Link ('L' Amber): External filesystem path that may break if moved.
 *      - Broken ('!' Fluent Red): File was not found at specified path.
 *
 * @param[in,out] ctx Target Blend2D drawing context.
 * @param[in]     viewport Active camera viewport for zoom and scale references.
 */
void AttachmentObject::Render(BLContext &ctx, const Viewport & /*viewport*/) const {
    if (!isVisible) return;

    const auto& cfg = GetConfig();

    ctx.save();
    ctx.apply_transform(transform);

    const double x  = worldX;
    const double y  = worldY;
    const double w  = (worldWidth > 0.0) ? worldWidth : GetDefaultChipWidth();
    const double h  = (worldHeight > 0.0) ? worldHeight : GetDefaultChipHeight();
    const double r  = cfg.cardCornerRadiusMm;
    const double bw = cfg.cardTypeBandWidthMm;

    // -------------------------------------------------------------------------
    // 1. BASE CARD SURFACE
    // -------------------------------------------------------------------------
    ctx.set_fill_style(cfg.GetCardBackgroundColor(opacity));
    ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

    // -------------------------------------------------------------------------
    // 2. FILE TYPE COLOR BAND (Left Edge)
    // -------------------------------------------------------------------------
    BLRgba32 bandCol = GetTypeColor();
    ctx.save();
    ctx.clip_to_rect(BLRect(x, y, bw, h));
    ctx.set_fill_style(bandCol);
    ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));
    ctx.restore();

    // -------------------------------------------------------------------------
    // 3. CARD BORDER & STATUS OUTLINE
    // -------------------------------------------------------------------------
    bool isBroken = !IsFileValid();
    ctx.set_stroke_style(cfg.GetCardBorderColor(isBroken, opacity));
    ctx.set_stroke_width(isBroken ? 0.6 : 0.4);
    ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

    // -------------------------------------------------------------------------
    // 4. TYPE BADGE TEXT (Centered in left band)
    // -------------------------------------------------------------------------
    std::string ext = "";
    auto dot = displayName.rfind('.');
    if (dot != std::string::npos && dot + 1 < displayName.size()) {
        ext = displayName.substr(dot + 1);
        for (auto &c : ext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (ext.size() > 4) ext = ext.substr(0, 4);
    } else {
        ext = "FILE";
    }

    BLFont badgeFont = FontManager::Instance().GetFont("Segoe UI", cfg.badgeFontSizePt, true);
    ctx.fill_utf8_text(BLPoint(x + 1.2, y + h * 0.5 + 1.1), badgeFont,
                       ext.data(), ext.size(), BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));

    // -------------------------------------------------------------------------
    // 5. FILENAME DISPLAY LABEL (Scissor-Clipped)
    // -------------------------------------------------------------------------
    BLFont labelFont = FontManager::Instance().GetFont("Segoe UI", cfg.labelFontSizePt, false);
    BLRgba32 labelCol = cfg.GetCardTextColor(isBroken, opacity);

    ctx.save();
    ctx.clip_to_rect(BLRect(x + bw + 2.0, y + 1.0, w - bw - 4.0, h - 2.0));
    ctx.fill_utf8_text(BLPoint(x + bw + 2.5, y + h * 0.5 + 1.2), labelFont,
                       displayName.data(), displayName.size(), labelCol);
    ctx.restore();

    // -------------------------------------------------------------------------
    // 6. EMBEDDED / LINK / BROKEN BADGE (Bottom-Right Corner)
    // -------------------------------------------------------------------------
    {
        const double badgeR = 2.8;                   // Badge circle radius (mm)
        const double badgeCX = x + w - badgeR - 1.2; // Center X
        const double badgeCY = y + h - badgeR - 1.0; // Center Y

        if (isBroken) {
            // Filled warning red circle with centered white '!'
            ctx.set_fill_style(BLRgba32(0xE8, 0x11, 0x23, 235));
            ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
            BLFont warnFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
            ctx.fill_utf8_text(BLPoint(badgeCX - 0.7, badgeCY + 0.9), warnFont, "!",
                               1, BLRgba32(0xFF, 0xFF, 0xFF, 255));
        } else if (isEmbedded) {
            // Filled teal circle with white 'E' (Embedded)
            ctx.set_fill_style(BLRgba32(0x00, 0xB3, 0x9A, 210));
            ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
            BLFont tinyFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
            ctx.fill_utf8_text(BLPoint(badgeCX - 1.5, badgeCY + 1.0), tinyFont, "E",
                               1, BLRgba32(0xFF, 0xFF, 0xFF, 230));
        } else {
            // Hollow warning-amber circle with amber 'L' (Link)
            ctx.set_stroke_style(BLRgba32(0xE8, 0xB3, 0x00, 190));
            ctx.set_stroke_width(0.5);
            ctx.stroke_circle(BLCircle(badgeCX, badgeCY, badgeR));
            BLFont tinyFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
            ctx.fill_utf8_text(BLPoint(badgeCX - 1.3, badgeCY + 1.0), tinyFont, "L",
                               1, BLRgba32(0xE8, 0xB3, 0x00, 200));
        }
    }

    ctx.restore();
}

/**
 * @brief Creates an exact deep polymorphic copy of this AttachmentObject.
 * @return Unique pointer to cloned CanvasObject.
 */
std::unique_ptr<CanvasObject> AttachmentObject::Clone() const {
    return std::make_unique<AttachmentObject>(*this);
}

/**
 * @brief Injects contextual operations into the right-click radial or dropdown menu.
 *
 * Menu items injected:
 * 1. Open (if valid): Invokes default system application.
 * 2. Re-link (if broken): Prompts file dialog to locate missing file.
 * 3. Copy File Path: Places full path string onto system clipboard.
 * 4. Show in File Explorer: Reveals parent folder in OS file manager.
 *
 * @param[in,out] actions Menu action list to append context items to.
 */
void AttachmentObject::CustomizeActions(std::vector<Folio::ContextMenuItem> &actions) {
    bool isValid = IsFileValid();

    if (isValid) {
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
        Folio::ContextMenuItem relinkAct;
        relinkAct.label = "Locate / Re-link File...";
        relinkAct.icon = "⚠️";
        relinkAct.iconKey = "warning";
        relinkAct.order = 10;
        relinkAct.isEnabled = true;
        relinkAct.onTrigger = [this]() { LocateAndRelinkFile(); };
        actions.push_back(std::move(relinkAct));
    }

    Folio::ContextMenuItem copyPathAct;
    copyPathAct.label = "Copy File Path";
    copyPathAct.icon = "📋";
    copyPathAct.iconKey = "copy";
    copyPathAct.order = 30;
    copyPathAct.onTrigger = [this]() {
        SDL_SetClipboardText(filePath.c_str());
    };
    actions.push_back(std::move(copyPathAct));

    Folio::ContextMenuItem explorerAct;
    explorerAct.label = "Show in File Explorer";
    explorerAct.icon = "📁";
    explorerAct.iconKey = "folder";
    explorerAct.order = 40;
    explorerAct.isEnabled = isFileValid;
    explorerAct.onTrigger = [this]() {
        std::string parentDir = FileManager::GetParentPath(filePath);
        if (!parentDir.empty() && FileManager::Exists(parentDir)) {
            FileManager::OpenWithDefaultApp(parentDir);
        }
    };
    actions.push_back(std::move(explorerAct));
}

/**
 * @brief Resolves color-coding for the file extension badge based on MIME and file extension.
 *
 * Color Mapping Scheme:
 * - PDF: Fluent Red (0xE81123)
 * - Spreadsheets (XLSX, XLS, CSV): Excel Green (0x107C41)
 * - Documents (DOCX, DOC): Word Blue (0x0078D4)
 * - Presentations (PPTX, PPT): PowerPoint Orange (0xD83B01)
 * - Archives (ZIP, RAR, 7Z): Amber Yellow (0xF0CC00)
 * - Media (PNG, JPG, SVG): Teal (0x008272)
 * - Audio (MP3, WAV, FLAC): Purple (0x5C2D91)
 * - Video (MP4, MKV, MOV): Indigo (0x0066CC)
 * - Plaintext (TXT, MD): Charcoal Grey (0x606060)
 * - Fallback: Neutral Slate (0x404452)
 *
 * @return BLRgba32 Accent color for the badge strip.
 */
BLRgba32 AttachmentObject::GetTypeColor() const {
    auto ext = std::string{};
    auto dot = displayName.rfind('.');
    if (dot != std::string::npos) {
        ext = displayName.substr(dot + 1);
        for (auto &c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    if (ext == "pdf")  return BLRgba32(0xE8, 0x11, 0x23, 0xFF); // Red
    if (ext == "xlsx" || ext == "xls" || ext == "csv") return BLRgba32(0x10, 0x7C, 0x41, 0xFF); // Green
    if (ext == "docx" || ext == "doc") return BLRgba32(0x00, 0x78, 0xD4, 0xFF); // Blue
    if (ext == "pptx" || ext == "ppt") return BLRgba32(0xD8, 0x3B, 0x01, 0xFF); // Orange
    if (ext == "zip"  || ext == "rar" || ext == "7z")  return BLRgba32(0xF0, 0xCC, 0x00, 0xFF); // Yellow
    if (ext == "png"  || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "svg") return BLRgba32(0x00, 0x82, 0x72, 0xFF); // Teal
    if (ext == "mp3"  || ext == "wav" || ext == "flac" || ext == "aac") return BLRgba32(0x5C, 0x2D, 0x91, 0xFF); // Purple
    if (ext == "mp4"  || ext == "mov" || ext == "avi"  || ext == "mkv") return BLRgba32(0x00, 0x66, 0xCC, 0xFF); // Dark blue
    if (ext == "txt"  || ext == "md")  return BLRgba32(0x60, 0x60, 0x60, 0xFF); // Grey

    return BLRgba32(0x40, 0x44, 0x52, 0xFF); // Default dark grey
}

} // namespace Folio
