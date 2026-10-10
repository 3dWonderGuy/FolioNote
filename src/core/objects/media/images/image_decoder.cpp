/**
 * =========================================================================================
 * @file core/objects/media/images/image_decoder.cpp
 * @brief Implementation of ImageDecoder Multi-Format Pipeline and Memory Capping
 * =========================================================================================
 */

#include "core/objects/media/images/image_decoder.hpp"
#include "io/facade/file_manager.hpp"

#include <lunasvg.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include <SDL3/SDL.h>
#if __has_include(<SDL3_image/SDL_image.h>)
#include <SDL3_image/SDL_image.h>
#elif __has_include("third_party/SDL_image/include/SDL3_image/SDL_image.h")
#include "third_party/SDL_image/include/SDL3_image/SDL_image.h"
#elif __has_include("../../../../../third_party/SDL_image/include/SDL3_image/SDL_image.h")
#include "../../../../../third_party/SDL_image/include/SDL3_image/SDL_image.h"
#else
#include <SDL3_image/SDL_image.h>
#endif
/**
 * UNIFIED CROSS-PLATFORM DECODING ARCHITECTURE:
 * ---------------------------------------------
 * FolioNote avoids OS-dependent decoders (such as Windows WIC, Apple ImageIO, or Android Bitmap)
 * to guarantee identical rendering fidelity, deterministic color blitting, and zero platform
 * branching across Windows, Linux, macOS, and Android.
 *
 * The decoding pipeline executes across 4 cross-platform layers:
 *   1. LunaSVG Pipeline:
 *      Parses vector SVG documents and rasterizes them into Blend2D PRGB32 pixel buffers.
 *   2. SDL3_image Multi-Frame Animation Pipeline:
 *      Parses multi-frame GIF and animated WebP containers via IMG_LoadAnimation_IO.
 *      Automatically resolves inter-frame disposal methods into standalone frames.
 *   3. Blend2D Native SIMD Pipeline:
 *      JIT-accelerated (AVX2/NEON) decoding for high-frequency formats: PNG, JPEG, BMP, and QOI.
 *   4. SDL3_image Extended Raster Pipeline:
 *      Fallback decoder for static WebP, TIFF, TGA, ICO, AVIF, and PCX.
 *
 * All pipelines output directly to Blend2D BL_FORMAT_PRGB32 (Premultiplied 32-bit ARGB/BGRA),
 * matching the compositor memory layout for zero-copy blitting.
 */

namespace Folio {

// =============================================================================
// PIXEL FORMAT CONVERSION
// =============================================================================

BLImage ImageDecoder::ConvertRgbaToBLImage(const uint8_t* rgbaData, uint32_t width, uint32_t height) {
    if (!rgbaData || width == 0 || height == 0) {
        return BLImage();
    }

    BLImage img;
    if (img.create(static_cast<int>(width), static_cast<int>(height), BL_FORMAT_PRGB32) != BL_SUCCESS) {
        return BLImage();
    }

    BLImageData data;
    if (img.make_mutable(&data) != BL_SUCCESS) {
        return BLImage();
    }

    uint8_t* dst = static_cast<uint8_t*>(data.pixel_data);
    const size_t dstStride = data.stride;

    // Convert straight RGBA to Premultiplied ARGB32 (BL_FORMAT_PRGB32)
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t* dstRow = reinterpret_cast<uint32_t*>(dst + y * dstStride);
        const uint8_t* srcRow = rgbaData + y * width * 4;

        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t r = srcRow[x * 4 + 0];
            const uint8_t g = srcRow[x * 4 + 1];
            const uint8_t b = srcRow[x * 4 + 2];
            const uint8_t a = srcRow[x * 4 + 3];

            // Alpha premultiplication formula: c_premul = (c * a + 127) / 255
            const uint32_t pr = (static_cast<uint32_t>(r) * a + 127) / 255;
            const uint32_t pg = (static_cast<uint32_t>(g) * a + 127) / 255;
            const uint32_t pb = (static_cast<uint32_t>(b) * a + 127) / 255;

            // In Blend2D on little-endian x86/ARM, PRGB32 is 0xAARRGGBB in uint32_t
            dstRow[x] = (static_cast<uint32_t>(a) << 24) |
                        (pr << 16) |
                        (pg << 8)  |
                        pb;
        }
    }

    return img;
}

// =============================================================================
// MAIN DECODING ROUTINE
// =============================================================================

DecodedImageResult ImageDecoder::DecodeFromMemory(
    const uint8_t* data, 
    size_t size, 
    std::string_view filenameHint) 
{
    DecodedImageResult result;

    if (!data || size == 0) {
        result.errorMessage = "Input buffer is empty";
        return result;
    }

    const auto& config = ObjectConfig::Get();
    if (size > config.maxImageFileSizeBytes) {
        result.errorMessage = "Image buffer size (" + std::to_string(size) + 
                              " bytes) exceeds maximum configured limit (" + 
                              std::to_string(config.maxImageFileSizeBytes) + " bytes)";
        return result;
    }

    const uint32_t maxDecodedDim = config.maxDecodedPixelDimension;

    ImageFormat detectedFormat = ImageFormatFromExtension(filenameHint);

    // Sniff magic bytes if filename extension is absent, inaccurate, or mismatched
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') 
    {
        detectedFormat = ImageFormat::WebP;
    } else if (size >= 4 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8') {
        detectedFormat = ImageFormat::GIF;
    } else if (size >= 4 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 0x2A && data[3] == 0x00) ||
                             (data[0] == 'M' && data[1] == 'M' && data[2] == 0x00 && data[3] == 0x2A))) 
    {
        detectedFormat = ImageFormat::TIFF;
    } else if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        detectedFormat = ImageFormat::PNG;
    } else if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        detectedFormat = ImageFormat::JPEG;
    }

    // =========================================================================
    // 1. SVG VECTOR PIPELINE (LunaSVG)
    // =========================================================================
    bool isSvg = (detectedFormat == ImageFormat::SVG);
    if (!isSvg && size >= 5) {
        // Sniff for XML/SVG header signature
        std::string_view head(reinterpret_cast<const char*>(data), (std::min)(size, size_t(256)));
        if (head.find("<svg") != std::string_view::npos || 
            (head.find("<?xml") != std::string_view::npos && head.find("<svg") != std::string_view::npos)) 
        {
            isSvg = true;
        }
    }

    if (isSvg) {
        auto doc = lunasvg::Document::loadFromData(reinterpret_cast<const char*>(data), size);
        if (doc) {
            double naturalW = doc->width();
            double naturalH = doc->height();
            if (naturalW <= 0.0 || naturalH <= 0.0) {
                naturalW = 512.0;
                naturalH = 512.0;
            }

            // High-DPI Vector Rasterization:
            // SVG is resolution-independent vector geometry. If an SVG's native viewBox is small
            // (e.g. 24x24 or 32x32 for UI icons), rasterizing at viewBox size causes severe pixelation
            // and blurriness when scaled up on canvas.
            // Mathematical working process:
            //   maxNatural = max(naturalW, naturalH)
            //   targetDim  = clamp(max(2048.0, maxNatural), 512.0, maxDecodedDim)
            //   scale      = targetDim / maxNatural
            //   renderW    = round(naturalW * scale)
            //   renderH    = round(naturalH * scale)
            // This ensures a 24x24 icon renders into a razor-sharp 2048x2048 PRGB32 surface.
            const double maxDim = static_cast<double>(maxDecodedDim);
            const double maxNatural = (std::max)(naturalW, naturalH);
            const double targetCrispDim = (std::min)(maxDim, (std::max)(2048.0, maxNatural));
            const double scale = (maxNatural > 0.0) ? (targetCrispDim / maxNatural) : 1.0;

            const uint32_t renderW = (std::max)(1u, static_cast<uint32_t>(std::round(naturalW * scale)));
            const uint32_t renderH = (std::max)(1u, static_cast<uint32_t>(std::round(naturalH * scale)));

            lunasvg::Bitmap bitmap = doc->renderToBitmap(renderW, renderH);
            if (bitmap.valid()) {
                BLImage img;
                if (img.create(static_cast<int>(renderW), static_cast<int>(renderH), BL_FORMAT_PRGB32) == BL_SUCCESS) {
                    BLImageData imgData;
                    if (img.make_mutable(&imgData) == BL_SUCCESS) {
                        const uint8_t* srcPixels = bitmap.data();
                        const int srcStride = bitmap.stride();
                        uint8_t* dstPixels = static_cast<uint8_t*>(imgData.pixel_data);
                        const int dstStride = static_cast<int>(imgData.stride);
                        const size_t bytesPerRow = renderW * 4;

                        // LunaSVG generates ARGB32 Premultiplied, matching Blend2D PRGB32 directly
                        for (uint32_t y = 0; y < renderH; ++y) {
                            std::memcpy(dstPixels + y * dstStride, srcPixels + y * srcStride, bytesPerRow);
                        }

                        result.image = std::move(img);
                        result.naturalWidth = static_cast<uint32_t>(naturalW);
                        result.naturalHeight = static_cast<uint32_t>(naturalH);
                        result.format = ImageFormat::SVG;
                        result.success = true;
                        return result;
                    }
                }
            }
        }
    }

    // =========================================================================
    // 2. MULTI-FRAME ANIMATION PIPELINE (GIF, Animated WebP via SDL3_image)
    // =========================================================================
    // Working Process & Math:
    //   GIF files (and animated WebP) encapsulate multiple raster frames with:
    //     - Individual frame durations (delayMs)
    //     - Disposal modes (restore background, restore previous, leave in place)
    //     - Transparency index masking and local color palettes
    //   SDL3_image's IMG_LoadAnimation_IO automatically composites disposal modes
    //   into complete, standalone 32-bit surfaces for each frame.
    //
    // Timing & Delay Clamping:
    //   GIF spec defines delay in 1/100ths of a second (10ms increments).
    //   Ancient encoders wrote 0 or 1 for delay. Standard browsers clamp delays
    //   <= 10ms to 100ms (10 FPS) to prevent runaway CPU spinlocks.
    //
    // RAM Protection:
    //   If natural dimensions exceed s_maxDecodedDimension, every frame is
    //   downsampled proportionally using Blend2D bilinear blitting:
    //     scale = s_maxDecodedDimension / max(naturalWidth, naturalHeight)
    // =========================================================================
    bool isGifOrWebp = (detectedFormat == ImageFormat::GIF || detectedFormat == ImageFormat::WebP);
    if (!isGifOrWebp && size >= 4 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8') {
        isGifOrWebp = true;
        detectedFormat = ImageFormat::GIF;
    }

    if (isGifOrWebp) {
        SDL_IOStream* animIo = SDL_IOFromConstMem(data, size);
        if (animIo) {
            IMG_Animation* anim = IMG_LoadAnimation_IO(animIo, true);
            if (anim && anim->count > 0 && anim->w > 0 && anim->h > 0) {
                result.naturalWidth = static_cast<uint32_t>(anim->w);
                result.naturalHeight = static_cast<uint32_t>(anim->h);
                result.format = (detectedFormat != ImageFormat::Unknown) ? detectedFormat : ImageFormat::GIF;
                result.isAnimated = (anim->count > 1);

                // RAM Protection: Compute uniform downsample factor if frames exceed maxDecodedDim
                double scale = 1.0;
                if (result.naturalWidth > maxDecodedDim || result.naturalHeight > maxDecodedDim) {
                    scale = static_cast<double>(maxDecodedDim) /
                            static_cast<double>((std::max)(result.naturalWidth, result.naturalHeight));
                }
                const int downW = (std::max)(1, static_cast<int>(std::round(result.naturalWidth * scale)));
                const int downH = (std::max)(1, static_cast<int>(std::round(result.naturalHeight * scale)));

                result.frames.reserve(anim->count);

                for (int i = 0; i < anim->count; ++i) {
                    SDL_Surface* surf = anim->frames[i];
                    if (!surf) continue;

                    SDL_Surface* rgbaSurf = (surf->format == SDL_PIXELFORMAT_RGBA32)
                        ? surf
                        : SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);

                    if (rgbaSurf) {
                        BLImage converted = ConvertRgbaToBLImage(
                            static_cast<const uint8_t*>(rgbaSurf->pixels),
                            static_cast<uint32_t>(rgbaSurf->w),
                            static_cast<uint32_t>(rgbaSurf->h)
                        );

                        if (rgbaSurf != surf) {
                            SDL_DestroySurface(rgbaSurf);
                        }

                        if (!converted.is_empty()) {
                            BLImage finalImg;
                            if (scale < 1.0) {
                                if (finalImg.create(downW, downH, BL_FORMAT_PRGB32) == BL_SUCCESS) {
                                    BLContext ctx(finalImg);
                                    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
                                    ctx.blit_image(BLRect(0, 0, downW, downH), converted);
                                    ctx.end();
                                } else {
                                    finalImg = std::move(converted);
                                }
                            } else {
                                finalImg = std::move(converted);
                            }

                            // GIF delay in milliseconds. Clamps <= 10ms to 100ms (10 FPS)
                            uint32_t delayMs = (anim->delays && anim->delays[i] > 10)
                                ? static_cast<uint32_t>(anim->delays[i])
                                : 100u;

                            result.frames.push_back(ImageFrame{ std::move(finalImg), delayMs });
                        }
                    }
                }

                // Note: IMG_FreeAnimation automatically frees anim->frames[i], anim->delays, and anim
                IMG_FreeAnimation(anim);

                if (!result.frames.empty()) {
                    result.image = result.frames[0].image;
                    result.success = true;
                    return result;
                }
            } else if (anim) {
                IMG_FreeAnimation(anim);
            }
        }
    }

    // =========================================================================
    // 3. BLEND2D NATIVE SIMD PIPELINE (PNG, JPEG, BMP, QOI)
    // =========================================================================
    BLImage blImg;
    if (blImg.read_from_data(data, size) == BL_SUCCESS && !blImg.is_empty()) {
        result.naturalWidth = static_cast<uint32_t>(blImg.width());
        result.naturalHeight = static_cast<uint32_t>(blImg.height());
        result.format = (detectedFormat != ImageFormat::Unknown) ? detectedFormat : ImageFormat::PNG;

        // RAM Protection: If image exceeds maxDecodedDim, downsample display proxy
        if (result.naturalWidth > maxDecodedDim || result.naturalHeight > maxDecodedDim) {
            const double scale = static_cast<double>(maxDecodedDim) / 
                                static_cast<double>((std::max)(result.naturalWidth, result.naturalHeight));
            const int downW = (std::max)(1, static_cast<int>(result.naturalWidth * scale));
            const int downH = (std::max)(1, static_cast<int>(result.naturalHeight * scale));

            BLImage downsampled;
            if (downsampled.create(downW, downH, BL_FORMAT_PRGB32) == BL_SUCCESS) {
                BLContext ctx(downsampled);
                ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
                ctx.blit_image(BLRect(0, 0, downW, downH), blImg);
                ctx.end();
                result.image = std::move(downsampled);
            } else {
                result.image = std::move(blImg);
            }
        } else {
            result.image = std::move(blImg);
        }

        result.success = true;
        return result;
    }

    // =========================================================================
    // 4. EXTENDED FORMAT FALLBACK PIPELINE (WebP, TIFF, TGA, ICO, etc. via SDL3_image)
    // =========================================================================
    SDL_IOStream* io = SDL_IOFromConstMem(data, size);
    if (io) {
        SDL_Surface* surf = IMG_Load_IO(io, true);
        if (surf && surf->w > 0 && surf->h > 0) {
            result.naturalWidth = static_cast<uint32_t>(surf->w);
            result.naturalHeight = static_cast<uint32_t>(surf->h);
            if (detectedFormat != ImageFormat::Unknown) {
                result.format = detectedFormat;
            } else {
                result.format = ImageFormat::WebP;
            }

            SDL_Surface* rgbaSurf = (surf->format == SDL_PIXELFORMAT_RGBA32)
                ? surf
                : SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);

            if (rgbaSurf) {
                BLImage converted = ConvertRgbaToBLImage(
                    static_cast<const uint8_t*>(rgbaSurf->pixels),
                    static_cast<uint32_t>(rgbaSurf->w),
                    static_cast<uint32_t>(rgbaSurf->h)
                );

                if (rgbaSurf != surf) {
                    SDL_DestroySurface(rgbaSurf);
                }
                SDL_DestroySurface(surf);

                if (!converted.is_empty()) {
                    // RAM Protection: Downsample if exceeds max threshold
                    if (result.naturalWidth > maxDecodedDim || result.naturalHeight > maxDecodedDim) {
                        const double scale = static_cast<double>(maxDecodedDim) / 
                                            static_cast<double>((std::max)(result.naturalWidth, result.naturalHeight));
                        const int downW = (std::max)(1, static_cast<int>(result.naturalWidth * scale));
                        const int downH = (std::max)(1, static_cast<int>(result.naturalHeight * scale));

                        BLImage downsampled;
                        if (downsampled.create(downW, downH, BL_FORMAT_PRGB32) == BL_SUCCESS) {
                            BLContext ctx(downsampled);
                            ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
                            ctx.blit_image(BLRect(0, 0, downW, downH), converted);
                            ctx.end();
                            result.image = std::move(downsampled);
                        } else {
                            result.image = std::move(converted);
                        }
                    } else {
                        result.image = std::move(converted);
                    }

                    result.success = true;
                    return result;
                }
            } else {
                SDL_DestroySurface(surf);
            }
        }
    }

    const char* sdlErr = SDL_GetError();
    if (sdlErr && *sdlErr) {
        result.errorMessage = "Unsupported or corrupted image format (" + std::string(sdlErr) + ")";
        SDL_ClearError();
    } else {
        result.errorMessage = "Unsupported or corrupted image format (unrecognized magic signature)";
    }
    return result;
}

// =============================================================================
// FILE RESOLUTION & LOADING
// =============================================================================

DecodedImageResult ImageDecoder::DecodeFromFile(const std::string& filePath) {
    DecodedImageResult result;

    if (filePath.empty()) {
        result.errorMessage = "File path is empty";
        return result;
    }

    // Delegate path resolution to FileManager (automatically checks active package root or absolute disk paths)
    std::string resolvedPath = FileManager::ResolveAssetPath(filePath);

    if (!FileManager::Exists(resolvedPath)) {
        result.errorMessage = "File does not exist: " + resolvedPath;
        return result;
    }

    std::vector<uint8_t> fileBuffer;
    if (!FileManager::ReadBinary(resolvedPath, fileBuffer) || fileBuffer.empty()) {
        result.errorMessage = "Failed to read file bytes: " + resolvedPath;
        return result;
    }

    return DecodeFromMemory(fileBuffer.data(), fileBuffer.size(), resolvedPath);
}

} // namespace Folio
