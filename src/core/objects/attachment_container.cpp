/**
 * =========================================================================================
 * @file core/objects/attachment_container.cpp
 * @brief Implementation of AttachmentObject Rendering, File Launching, and Interaction
 * =========================================================================================
 *
 * GENERAL WORKING PROCESS:
 * -------------------------
 * This translation unit houses all concrete filesystem (FileManager) and typography
 * (FontManager) operations for AttachmentObject chips, isolating them away from
 * attachment_container.hpp to eliminate compilation cascades.
 */

#include "core/objects/attachment_container.hpp"
#include "core/objects/canvas_context.hpp"
#include "core/text/font_manager.hpp"
#include "io/file_manager.hpp"
#include "app/context_menu_item.hpp"
#include "utils/logger.hpp"
#include <SDL3/SDL.h>
#include <cctype>

namespace Folio {

AttachmentObject::AttachmentObject() {
    type = ObjectType::AttachmentFile;
    worldWidth = GetDefaultChipWidth();
    worldHeight = GetDefaultChipHeight();
    UpdateBounds();
}

AttachmentObject::AttachmentObject(const std::string &path, const std::string &name,
                                   const std::string &mime, bool embedded)
    : filePath(path), displayName(name), mimeType(mime), isEmbedded(embedded) {
    type = ObjectType::AttachmentFile;
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

bool AttachmentObject::OnPointerClick(const Folio::CanvasContext& ctx) {
    if (ctx.isDoubleClick) {
        LOG_INFO(CanvasObject, "Double-clicked attachment: opening file '" + filePath + "'");
        return OpenFile();
    }
    return false;
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

void AttachmentObject::ApplyTransform(const BLMatrix2D &matrix) {
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

void AttachmentObject::BakeTransform() {
    worldX += transform.m20;
    worldY += transform.m21;
    transform = BLMatrix2D::make_identity();
    UpdateBounds();
}

void AttachmentObject::Render(BLContext &ctx, const Viewport & /*viewport*/) const {
    if (!isVisible) return;

    ctx.save();
    ctx.apply_transform(transform);

    const double x = worldX;
    const double y = worldY;
    const double w = (worldWidth > 0.0) ? worldWidth : GetDefaultChipWidth();
    const double h = (worldHeight > 0.0) ? worldHeight : GetDefaultChipHeight();
    const double r = 2.0;  // Corner radius (mm)
    const double bw = 8.0; // Type band width (mm)

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
        for (auto &c : ext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (ext.size() > 4) ext = ext.substr(0, 4);
    } else {
        ext = "FILE";
    }

    // Draw extension inside the left color band
    BLFont badgeFont = FontManager::Instance().GetFont("Segoe UI", 3.2f, true);
    ctx.fill_utf8_text(BLPoint(x + 1.2, y + h * 0.5 + 1.1), badgeFont,
                       ext.data(), ext.size(), BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));

    // Draw display name label (tinted warning red if broken)
    BLFont labelFont = FontManager::Instance().GetFont("Segoe UI", 3.4f, false);
    BLRgba32 labelCol = isBroken
        ? BLRgba32(0xFF, 0x88, 0x88, static_cast<uint8_t>(opacity * 255))
        : BLRgba32(0xF0, 0xF2, 0xF5, static_cast<uint8_t>(opacity * 255));

    // Clip text so it doesn't bleed out of chip
    ctx.save();
    ctx.clip_to_rect(BLRect(x + bw + 2.0, y + 1.0, w - bw - 4.0, h - 2.0));
    ctx.fill_utf8_text(BLPoint(x + bw + 2.5, y + h * 0.5 + 1.2), labelFont,
                       displayName.data(), displayName.size(), labelCol);
    ctx.restore();

    // Draw embedded / link / broken mode badge in the bottom-right corner
    {
        const double badgeR = 2.8;                   // Badge circle radius (mm)
        const double badgeCX = x + w - badgeR - 1.2; // Centre X
        const double badgeCY = y + h - badgeR - 1.0; // Centre Y

        if (isBroken) {
            // Filled warning red circle with white '!'
            ctx.set_fill_style(BLRgba32(0xE8, 0x11, 0x23, 235));
            ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
            BLFont warnFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
            ctx.fill_utf8_text(BLPoint(badgeCX - 0.7, badgeCY + 0.9), warnFont, "!",
                               1, BLRgba32(0xFF, 0xFF, 0xFF, 255));
        } else if (isEmbedded) {
            // Filled teal circle = embedded/safe
            ctx.set_fill_style(BLRgba32(0x00, 0xB3, 0x9A, 210));
            ctx.fill_circle(BLCircle(badgeCX, badgeCY, badgeR));
            BLFont tinyFont = FontManager::Instance().GetFont("Segoe UI", 2.6f, true);
            ctx.fill_utf8_text(BLPoint(badgeCX - 1.5, badgeCY + 1.0), tinyFont, "E",
                               1, BLRgba32(0xFF, 0xFF, 0xFF, 230));
        } else {
            // Hollow warning-amber circle = link (may break)
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

std::unique_ptr<CanvasObject> AttachmentObject::Clone() const {
    return std::make_unique<AttachmentObject>(*this);
}

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
