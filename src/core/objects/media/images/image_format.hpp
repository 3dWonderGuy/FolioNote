#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/images/image_format.hpp
 * @brief Image Format Enumeration and Metadata Conversion Utilities
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & WORKING PROCESS:
 * ---------------------------------------
 * This header defines supported raster image formats within FolioNote's canvas engine,
 * along with bidirectional mapping functions between format enumerations, standard file
 * extensions, and RFC 2046 MIME type strings.
 *
 * Supported Raster Formats:
 *   - Unknown (0): Unrecognized or unsupported binary format payload.
 *   - PNG (1): Portable Network Graphics (lossless, RGBA, full transparency).
 *   - JPEG (2): Joint Photographic Experts Group (lossy, high compression).
 *   - WebP (3): Google WebP format (lossy & lossless, transparency).
 *   - BMP (4): Windows Bitmap (uncompressed raw pixel arrays).
 *   - GIF (5): Graphics Interchange Format (supports multi-frame animations).
 *   - TIFF (6): Tagged Image File Format (high dynamic range, multi-page).
 *   - QOI (7): "Quite OK Image" Format (fast, lossless, minimal decode latency).
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

namespace Folio {

/**
 * @enum ImageFormat
 * @brief Discriminated format tags for raster graphics assets on the canvas.
 */
enum class ImageFormat : uint8_t {
    Unknown = 0, ///< Unrecognized or corrupt format payload
    PNG,         ///< Portable Network Graphics (.png)
    JPEG,        ///< Joint Photographic Experts Group (.jpg, .jpeg)
    WebP,        ///< WebP Raster Image (.webp)
    BMP,         ///< Windows Bitmap (.bmp)
    GIF,         ///< Graphics Interchange Format (.gif)
    TIFF,        ///< Tagged Image File Format (.tif, .tiff)
    QOI,         ///< Quite OK Image Format (.qoi)
    SVG          ///< Scalable Vector Graphics (.svg)
};

/**
 * @brief Deduces the ImageFormat from a file extension or filename string.
 *
 * Working Process:
 *   1. Finds the last dot '.' separator in the input path or extension.
 *   2. Extracts the substring and transforms characters to lowercase.
 *   3. Matches against known extension aliases (.jpg, .jpeg, .tif, .tiff, .svg, etc.).
 *   4. Returns ImageFormat::Unknown if no match is found.
 *
 * @param[in] pathOrExt Filename (e.g. "photo.jpeg") or extension (e.g. ".png" or "webp").
 * @return Deducible ImageFormat enumeration, or ImageFormat::Unknown if unrecognized.
 */
inline ImageFormat ImageFormatFromExtension(std::string_view pathOrExt) {
    if (pathOrExt.empty()) {
        return ImageFormat::Unknown;
    }

    // Locate trailing extension delimiter
    const auto lastDot = pathOrExt.rfind('.');
    std::string_view ext = (lastDot != std::string_view::npos) 
        ? pathOrExt.substr(lastDot + 1) 
        : pathOrExt;

    // Convert to lowercase for case-insensitive matching
    std::string lowerExt;
    lowerExt.reserve(ext.size());
    for (char c : ext) {
        lowerExt.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lowerExt == "png")                return ImageFormat::PNG;
    if (lowerExt == "jpg" || lowerExt == "jpeg") return ImageFormat::JPEG;
    if (lowerExt == "webp")               return ImageFormat::WebP;
    if (lowerExt == "bmp")                return ImageFormat::BMP;
    if (lowerExt == "gif")                return ImageFormat::GIF;
    if (lowerExt == "tif" || lowerExt == "tiff") return ImageFormat::TIFF;
    if (lowerExt == "qoi")                return ImageFormat::QOI;
    if (lowerExt == "svg")                return ImageFormat::SVG;

    return ImageFormat::Unknown;
}

/**
 * @brief Converts an ImageFormat enum into its canonical lowercase file extension string.
 *
 * @param[in] format The ImageFormat to query.
 * @return String literal containing canonical extension (e.g. "png", "jpeg", "qoi", "svg").
 */
constexpr std::string_view ImageFormatToExtension(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::PNG:     return "png";
        case ImageFormat::JPEG:    return "jpeg";
        case ImageFormat::WebP:    return "webp";
        case ImageFormat::BMP:     return "bmp";
        case ImageFormat::GIF:     return "gif";
        case ImageFormat::TIFF:    return "tiff";
        case ImageFormat::QOI:     return "qoi";
        case ImageFormat::SVG:     return "svg";
        case ImageFormat::Unknown:
        default:                   return "";
    }
}

/**
 * @brief Converts an ImageFormat enum into its human-readable display label.
 *
 * @param[in] format The ImageFormat to query.
 * @return Human-readable display string (e.g. "PNG", "JPEG", "WebP", "SVG").
 */
constexpr std::string_view ImageFormatToString(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::PNG:     return "PNG";
        case ImageFormat::JPEG:    return "JPEG";
        case ImageFormat::WebP:    return "WebP";
        case ImageFormat::BMP:     return "BMP";
        case ImageFormat::GIF:     return "GIF";
        case ImageFormat::TIFF:    return "TIFF";
        case ImageFormat::QOI:     return "QOI";
        case ImageFormat::SVG:     return "SVG";
        case ImageFormat::Unknown:
        default:                   return "Unknown";
    }
}

/**
 * @brief Maps an ImageFormat enum to its standard RFC 2046 MIME type string.
 *
 * @param[in] format The ImageFormat to query.
 * @return Canonical MIME type string literal (e.g. "image/png", "image/svg+xml").
 */
constexpr std::string_view ImageFormatToMimeType(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::PNG:     return "image/png";
        case ImageFormat::JPEG:    return "image/jpeg";
        case ImageFormat::WebP:    return "image/webp";
        case ImageFormat::BMP:     return "image/bmp";
        case ImageFormat::GIF:     return "image/gif";
        case ImageFormat::TIFF:    return "image/tiff";
        case ImageFormat::QOI:     return "image/qoi";
        case ImageFormat::SVG:     return "image/svg+xml";
        case ImageFormat::Unknown:
        default:                   return "application/octet-stream";
    }
}

/**
 * @brief Resolves an ImageFormat enum from a standard MIME type string.
 *
 * @param[in] mime The MIME type string to parse (e.g. "image/png", "image/svg+xml").
 * @return Corresponding ImageFormat enumeration, or ImageFormat::Unknown if unmatched.
 */
inline ImageFormat ImageFormatFromMimeType(std::string_view mime) {
    if (mime == "image/png")             return ImageFormat::PNG;
    if (mime == "image/jpeg")            return ImageFormat::JPEG;
    if (mime == "image/jpg")             return ImageFormat::JPEG;
    if (mime == "image/webp")            return ImageFormat::WebP;
    if (mime == "image/bmp")             return ImageFormat::BMP;
    if (mime == "image/gif")             return ImageFormat::GIF;
    if (mime == "image/tiff")            return ImageFormat::TIFF;
    if (mime == "image/qoi")             return ImageFormat::QOI;
    if (mime == "image/svg+xml" || mime == "image/svg") return ImageFormat::SVG;
    return ImageFormat::Unknown;
}

/**
 * @brief Identifies whether an image format inherently supports multiple animation frames.
 *
 * @param[in] format Target ImageFormat.
 * @return True if the format can contain sequential multi-frame animations (e.g. GIF).
 */
constexpr bool IsFormatAnimated(ImageFormat format) noexcept {
    return format == ImageFormat::GIF;
}

} // namespace Folio
