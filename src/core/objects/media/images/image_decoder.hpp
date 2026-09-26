#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/images/image_decoder.hpp
 * @brief High-Performance Multi-Format Image Decoding Pipeline and RAM Protection Service
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & WORKING PROCESS:
 * ---------------------------------------
 * ImageDecoder serves as FolioNote's unified decoding bridge, taking raw binary data
 * (from memory buffers or companion sidecar disk files) and decoding them into
 * hardware-accelerated Blend2D `BLImage` surfaces.
 *
 * Supported Decoding Pipelines:
 *   1. Blend2D Native SIMD Pipeline:
 *      High-performance JIT-accelerated decoding for PNG, JPEG, BMP, and QOI.
 *   2. LunaSVG Vector Rasterization Pipeline:
 *      Parses SVG XML documents and renders them to pixel-perfect `BLImage` surfaces
 *      at display resolution.
 *   3. Extended Format Fallback Pipeline:
 *      Decodes GIF, TGA, PSD, and other formats into raw RGBA buffers and maps them
 *      to Blend2D `BL_FORMAT_PRGB32` surfaces.
 *
 * RAM PROTECTION & GIGABYTE FILE SAFETY:
 * --------------------------------------
 * A raw 16000 x 16000 uncompressed TIFF or multi-gigabyte raw scan consumes:
 *   RAM = 16000 * 16000 * 4 bytes = 1,024,000,000 bytes = 1.02 GB
 *
 * To prevent runaway memory consumption and Out-Of-Memory (OOM) crashes:
 *   - The original file is stored 100% untouched on disk (zero data loss).
 *   - During decoding, ImageDecoder enforces `s_maxDecodedDimension` (default: 4096 px).
 *   - If max(naturalWidth, naturalHeight) > s_maxDecodedDimension:
 *       scale = s_maxDecodedDimension / max(naturalWidth, naturalHeight)
 *       downsampledWidth  = naturalWidth * scale
 *       downsampledHeight = naturalHeight * scale
 *     Capping display RAM consumption to:
 *       Max RAM = 4096 * 4096 * 4 bytes = 67.1 MB (94% RAM reduction).
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <blend2d/blend2d.h>

#include "core/objects/media/images/image_format.hpp"

namespace Folio {

/**
 * @struct ImageFrame
 * @brief Encapsulates a single decoded animation frame with duration metadata.
 *
 * Mathematical & Timing Model:
 *   - Each frame maintains a private Blend2D BL_FORMAT_PRGB32 surface.
 *   - delayMs specifies presentation duration in milliseconds.
 *   - GIF standard defines delay in 1/100th second (10ms increments).
 *   - Web browsers standardly clamp delays <= 10ms to 100ms (10 FPS)
 *     to prevent runaway CPU usage from archaic GIF encoders.
 */
struct ImageFrame {
    BLImage image;             ///< Decoded Blend2D PRGB32 raster surface for this frame
    uint32_t delayMs = 100;    ///< Presentation duration in milliseconds (e.g. 100ms = 10 FPS)
};

/**
 * @struct DecodedImageResult
 * @brief Encapsulates the output of a completed image decode operation.
 */
struct DecodedImageResult {
    BLImage image;                              ///< Decoded Blend2D raster surface ready for canvas blitting (Frame 0)
    std::vector<ImageFrame> frames;             ///< All animation frames with per-frame delay timers (populated if animated)
    uint32_t naturalWidth = 0;                  ///< Native unscaled source pixel width
    uint32_t naturalHeight = 0;                 ///< Native unscaled source pixel height
    ImageFormat format = ImageFormat::Unknown;  ///< Resolved image format tag
    bool isAnimated = false;                    ///< True if image contains multiple animation frames (e.g. animated GIF)
    bool success = false;                       ///< True if decoding succeeded and image is valid
    std::string errorMessage;                   ///< Diagnostic details if decoding failed
};

/**
 * @class ImageDecoder
 * @brief Static coordinator for decoding diverse raster and vector image formats.
 */
class ImageDecoder {
public:
    // =========================================================================
    // CONFIGURATION & MEMORY CONSTRAINTS
    // =========================================================================

    inline static uint32_t s_maxDecodedDimension = 4096; ///< Hard maximum pixel boundary for display proxy surfaces (4096px)

    /**
     * @brief Configures the upper pixel dimension limit for in-memory raster surfaces.
     * @param[in] maxDim Maximum allowed width or height in pixels (minimum 512, maximum 16384).
     */
    static void SetMaxDecodedDimension(uint32_t maxDim) noexcept {
        if (maxDim >= 512 && maxDim <= 16384) {
            s_maxDecodedDimension = maxDim;
        }
    }

    /**
     * @brief Retrieves the current maximum pixel dimension limit for decoded surfaces.
     * @return Maximum dimension in pixels.
     */
    [[nodiscard]] static uint32_t GetMaxDecodedDimension() noexcept {
        return s_maxDecodedDimension;
    }

    // =========================================================================
    // DECODING APIS
    // =========================================================================

    /**
     * @brief Decodes an image from a raw binary memory buffer.
     *
     * Working Process:
     *   1. Inspects filename hint or magic bytes to deduce the format.
     *   2. If SVG: Delegates to LunaSVG vector rasterizer.
     *   3. If PNG, JPEG, BMP, QOI: Delegates to Blend2D SIMD decoder.
     *   4. If GIF / other: Delegates to extended fallback decoder.
     *   5. Checks if decoded dimensions exceed s_maxDecodedDimension:
     *      If true, scales down to fit s_maxDecodedDimension to protect system RAM.
     *
     * @param[in] data Pointer to raw image bytes in memory.
     * @param[in] size Size of data buffer in bytes.
     * @param[in] filenameHint Optional path or extension hint (e.g. "asset.svg", ".webp").
     * @return DecodedImageResult containing BLImage surface, natural dimensions, and format tag.
     */
    static DecodedImageResult DecodeFromMemory(
        const uint8_t* data, 
        size_t size, 
        std::string_view filenameHint = ""
    );

    /**
     * @brief Decodes an image from a physical filesystem path or notebook package asset.
     *
     * Working Process:
     *   1. Resolves path using Folio::FileManager (packageRoot-relative or absolute).
     *   2. Reads binary payload into memory via FileManager::ReadBinary for safe Unicode I/O.
     *   3. Delegates to DecodeFromMemory with filename hint.
     *
     * @param[in] filePath Relative package path or absolute disk path.
     * @param[in] packageRoot Optional notebook root directory for relative resolution.
     * @return DecodedImageResult containing BLImage surface, natural dimensions, and format tag.
     */
    static DecodedImageResult DecodeFromFile(
        const std::string& filePath, 
        const std::string& packageRoot = ""
    );

    /**
     * @brief Helper to convert a raw 32-bit RGBA pixel buffer into a Blend2D PRGB32 surface.
     *
     * Mathematical Formula (Alpha Premultiplication):
     *   pr = (r * a + 127) / 255
     *   pg = (g * a + 127) / 255
     *   pb = (b * a + 127) / 255
     *   dstPixel = (a << 24) | (pr << 16) | (pg << 8) | pb
     *
     * @param[in] rgbaData Raw 32-bit RGBA pixel array (width * height * 4 bytes).
     * @param[in] width Pixel width.
     * @param[in] height Pixel height.
     * @return Blend2D BLImage surface in native BL_FORMAT_PRGB32 format.
     */
    static BLImage ConvertRgbaToBLImage(const uint8_t* rgbaData, uint32_t width, uint32_t height);
};

} // namespace Folio
