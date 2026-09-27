#pragma once
/**
 * =========================================================================================
 * @file core/clipboard/clipboard_data_package.hpp
 * @brief Multi-Format Envelope for Rich Cross-Application and Native Clipboard Payloads
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * -------------------
 * In modern operating systems (Windows OLE, Linux Wayland/X11, macOS Pasteboard), the clipboard
 * is not a single string or bitmap. It is a multi-format negotiation tray where an application
 * publishes multiple simultaneous representations ("flavors") of copied canvas objects.
 *
 * Supported Clipboard Representations:
 * ------------------------------------
 * 1. Native Binary Blob ("application/x-folionote-ink"):
 *    High-performance, delta-compressed binary representation (.ink) containing complete
 *    vector stroke fidelity, pressure dynamics, text attributes, math parameters, and transforms.
 *    Used by FolioNote itself for lossless internal Paste and cross-window copy/paste.
 *
 * 2. Vector SVG XML ("image/svg+xml"):
 *    W3C standard vector XML markup representing scalable vector paths, polylines, colors,
 *    and font layouts. Directly pasted into vector suites (Illustrator, Figma, Inkscape)
 *    and modern Microsoft Office (OneNote, Word, PowerPoint).
 *
 * 3. Windows Ink / ISF ("application/x-ms-ink" / Ink Serialized Format):
 *    Microsoft's native digital ink stream. When pasted into Microsoft OneNote or Microsoft
 *    Whiteboard, strokes convert into native, editable OneNote ink strokes with pressure.
 *
 * 4. High-Resolution Raster Image ("image/png", "image/bmp", CF_DIBV5):
 *    Premultiplied RGBA raster image rendered with alpha transparency. Consumed by standard
 *    raster applications (Discord, Slack, MS Paint, Photoshop, web messengers, email).
 *
 * 5. Unicode Plain Text ("text/plain;charset=utf-8", CF_UNICODETEXT):
 *    Plaintext representation (extracted text from TextBoxes, video URLs, markdown notes,
 *    attachment paths).
 *
 * MATHEMATICAL FOUNDATIONS:
 * -------------------------
 * Collective Spatial Bounding Box:
 *   Given N objects with individual AABB bounding boxes {B_1, B_2, ..., B_N},
 *   the union bounding box is:
 *     B_total = [ min(B_i.minX), min(B_i.minY), max(B_i.maxX), max(B_i.maxY) ]
 *   Dimensions: W = B_total.maxX - B_total.minX, H = B_total.maxY - B_total.minY
 *   Centroid: C = (B_total.minX + W/2, B_total.minY + H/2)
 *
 * DPI Scaling for Raster Preview:
 *   Pixel dimensions for high-resolution PNG fallback:
 *     pixelW = round(W_mm * (targetDPI / 25.4))
 *     pixelH = round(H_mm * (targetDPI / 25.4))
 *   Blend2D renders the objects with translation (-B_total.minX, -B_total.minY)
 *   and uniform scale (targetDPI / 25.4).
 */

#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <blend2d/blend2d.h>

#include "core/spatial/aabb.hpp"
#include "core/objects/canvas_object.hpp"

namespace Folio {

/**
 * @struct ClipboardDataPackage
 * @brief Container holding multiple synchronized representations of copied canvas objects.
 */
struct ClipboardDataPackage {
    // --- Metadata ---
    std::string sourceApplication = "FolioNote";
    uint32_t objectCount = 0;
    AABB totalBounds;                          ///< Collective bounding box in world mm
    double originX = 0.0;                      ///< Reference centroid or anchor X in mm
    double originY = 0.0;                      ///< Reference centroid or anchor Y in mm

    // --- Flavor 1: Native In-Memory Objects & Binary Stream ---
    std::vector<std::shared_ptr<CanvasObject>> clonedObjects; ///< Deep-copied objects for instant internal paste
    std::vector<uint8_t> nativeBinaryBlob;                    ///< Compressed .ink binary blob for cross-process paste

    // --- Flavor 2: Scalable Vector Graphics (SVG) ---
    std::string svgXml;                                       ///< Full XML SVG markup (<svg ...> ... </svg>)

    // --- Flavor 3: High-Resolution Raster Image (PNG) ---
    std::vector<uint8_t> pngBytes;                            ///< Encoded PNG image buffer with alpha
    uint32_t imageWidthPx = 0;                                ///< Raster pixel width
    uint32_t imageHeightPx = 0;                              ///< Raster pixel height

    // --- Flavor 4: Unicode Text / URL ---
    std::string plainText;                                    ///< Extracted text or media URL

    // --- Flavor 5: Windows Ink Serialized Format (ISF) ---
    std::vector<uint8_t> isfBytes;                            ///< Binary ISF stream for MS OneNote / Whiteboard

    /**
     * @brief Checks if the package contains any valid data representations.
     * @return true if at least one data flavor is populated.
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return clonedObjects.empty() &&
               nativeBinaryBlob.empty() &&
               svgXml.empty() &&
               pngBytes.empty() &&
               plainText.empty() &&
               isfBytes.empty();
    }

    /**
     * @brief Clears all cached representations.
     */
    void Clear() noexcept {
        clonedObjects.clear();
        nativeBinaryBlob.clear();
        svgXml.clear();
        pngBytes.clear();
        imageWidthPx = 0;
        imageHeightPx = 0;
        plainText.clear();
        isfBytes.clear();
        objectCount = 0;
        totalBounds = AABB();
        originX = 0.0;
        originY = 0.0;
    }
};

} // namespace Folio
