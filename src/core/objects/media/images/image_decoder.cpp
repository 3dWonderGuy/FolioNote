/**
 * =========================================================================================
 * @file core/objects/media/images/image_decoder.cpp
 * @brief Implementation of ImageDecoder Multi-Format Pipeline and Memory Capping
 * =========================================================================================
 */

#include "core/objects/media/images/image_decoder.hpp"
#include "io/file_manager.hpp"

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

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

/**
 * @brief Decodes image bytes using the native Windows Imaging Component (WIC).
 *
 * Mathematical / Color Processing Context:
 *   WIC operates with OS-registered hardware codecs. On modern Windows (Windows 10/11),
 *   Microsoft ships the WebP Image Extension (registered under WIC), plus built-in codecs
 *   for TIFF, JPEG, PNG, GIF, BMP, and ICO.
 *
 * Pixel Alignment & Premultiplication:
 *   WIC converts the source frame to GUID_WICPixelFormat32bppPBGRA (Premultiplied BGRA),
 *   where byte order in memory on little-endian x86/x64 is:
 *     Byte 0: Blue  (pb = (b * a + 127) / 255)
 *     Byte 1: Green (pg = (g * a + 127) / 255)
 *     Byte 2: Red   (pr = (r * a + 127) / 255)
 *     Byte 3: Alpha (a)
 *   This is identical bit-for-bit to Blend2D BL_FORMAT_PRGB32:
 *     uint32_t pixel = (a << 24) | (pr << 16) | (pg << 8) | pb;
 *   allowing direct single-pass pixel blit into BLImage without secondary color swizzling.
 *
 * @param[in]  data       Raw compressed byte buffer.
 * @param[in]  size       Size of data in bytes.
 * @param[out] outImage   Destination BLImage populated with PRGB32 raster data.
 * @param[out] outWidth   Native image width in pixels.
 * @param[out] outHeight  Native image height in pixels.
 * @return True if WIC decoded a valid frame, false on error or unsupported format.
 */
static bool DecodeWithWIC(
    const uint8_t* data, 
    size_t size, 
    BLImage& outImage, 
    std::vector<Folio::ImageFrame>& outFrames,
    bool& outIsAnimated,
    uint32_t& outWidth, 
    uint32_t& outHeight)
{
    if (!data || size == 0) return false;

    HRESULT initHr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (initHr == RPC_E_CHANGED_MODE) {
        initHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }
    const bool needsUninit = (initHr == S_OK || initHr == S_FALSE);

    IWICImagingFactory* pFactory = NULL;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pFactory)
    );
    if (FAILED(hr) || !pFactory) {
        if (needsUninit) CoUninitialize();
        return false;
    }

    IWICStream* pStream = NULL;
    hr = pFactory->CreateStream(&pStream);
    if (FAILED(hr) || !pStream) {
        pFactory->Release();
        if (needsUninit) CoUninitialize();
        return false;
    }

    hr = pStream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size));
    if (FAILED(hr)) {
        pStream->Release();
        pFactory->Release();
        if (needsUninit) CoUninitialize();
        return false;
    }

    IWICBitmapDecoder* pDecoder = NULL;
    hr = pFactory->CreateDecoderFromStream(
        pStream,
        NULL,
        WICDecodeMetadataCacheOnDemand,
        &pDecoder
    );
    if (FAILED(hr) || !pDecoder) {
        pStream->Release();
        pFactory->Release();
        if (needsUninit) CoUninitialize();
        return false;
    }

    UINT frameCount = 1;
    hr = pDecoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || frameCount == 0) {
        frameCount = 1;
    }

    // Parse WebP ANMF chunk durations if animated WebP container
    std::vector<uint32_t> webpDelays;
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') 
    {
        size_t pos = 12;
        while (pos + 8 <= size) {
            uint32_t chunkSize = 0;
            std::memcpy(&chunkSize, data + pos + 4, 4);
            if (std::memcmp(data + pos, "ANMF", 4) == 0) {
                if (pos + 8 + 15 <= size) {
                    // Duration is a 24-bit unsigned little-endian integer at offset 12..14 of ANMF chunk
                    const uint32_t dur = static_cast<uint32_t>(data[pos + 8 + 12]) |
                                        (static_cast<uint32_t>(data[pos + 8 + 13]) << 8) |
                                        (static_cast<uint32_t>(data[pos + 8 + 14]) << 16);
                    webpDelays.push_back(dur > 10 ? dur : 100);
                }
            }
            pos += 8 + chunkSize + (chunkSize % 2);
        }
    }

    outFrames.reserve(frameCount);

    for (UINT i = 0; i < frameCount; ++i) {
        IWICBitmapFrameDecode* pFrame = NULL;
        hr = pDecoder->GetFrame(i, &pFrame);
        if (FAILED(hr) || !pFrame) continue;

        UINT w = 0, h = 0;
        pFrame->GetSize(&w, &h);
        if (w == 0 || h == 0) {
            pFrame->Release();
            continue;
        }

        if (i == 0) {
            outWidth = static_cast<uint32_t>(w);
            outHeight = static_cast<uint32_t>(h);
        }

        IWICFormatConverter* pConverter = NULL;
        hr = pFactory->CreateFormatConverter(&pConverter);
        if (FAILED(hr) || !pConverter) {
            pFrame->Release();
            continue;
        }

        // Convert directly to Premultiplied 32-bit BGRA (matches BL_FORMAT_PRGB32 on little-endian)
        hr = pConverter->Initialize(
            pFrame,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.0,
            WICBitmapPaletteTypeCustom
        );

        if (SUCCEEDED(hr)) {
            BLImage frameImg;
            if (frameImg.create(static_cast<int>(w), static_cast<int>(h), BL_FORMAT_PRGB32) == BL_SUCCESS) {
                BLImageData imgData;
                if (frameImg.make_mutable(&imgData) == BL_SUCCESS) {
                    hr = pConverter->CopyPixels(
                        NULL,
                        static_cast<UINT>(imgData.stride),
                        static_cast<UINT>(imgData.stride * h),
                        static_cast<BYTE*>(imgData.pixel_data)
                    );
                    if (SUCCEEDED(hr)) {
                        uint32_t delayMs = 100;
                        if (i < webpDelays.size()) {
                            delayMs = webpDelays[i];
                        } else {
                            IWICMetadataQueryReader* pReader = NULL;
                            if (SUCCEEDED(pFrame->GetMetadataQueryReader(&pReader)) && pReader) {
                                PROPVARIANT propVal;
                                PropVariantInit(&propVal);
                                if (SUCCEEDED(pReader->GetMetadataByName(L"/grctlext/Delay", &propVal))) {
                                    if (propVal.vt == VT_UI2) delayMs = propVal.uiVal * 10;
                                    else if (propVal.vt == VT_UI4) delayMs = propVal.ulVal * 10;
                                    PropVariantClear(&propVal);
                                } else if (SUCCEEDED(pReader->GetMetadataByName(L"/Delay", &propVal)) ||
                                           SUCCEEDED(pReader->GetMetadataByName(L"/features/Delay", &propVal))) {
                                    if (propVal.vt == VT_UI4) delayMs = propVal.ulVal;
                                    else if (propVal.vt == VT_UI2) delayMs = propVal.uiVal;
                                    PropVariantClear(&propVal);
                                }
                                pReader->Release();
                            }
                        }
                        if (delayMs <= 10) delayMs = 100;

                        outFrames.push_back(Folio::ImageFrame{ std::move(frameImg), delayMs });
                    }
                }
            }
        }

        pConverter->Release();
        pFrame->Release();
    }

    pDecoder->Release();
    pStream->Release();
    pFactory->Release();
    if (needsUninit) CoUninitialize();

    if (!outFrames.empty()) {
        outImage = outFrames[0].image;
        outIsAnimated = (outFrames.size() > 1);
        return true;
    }

    return false;
}
#endif

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

            // Scale to fit s_maxDecodedDimension if SVG viewbox is unusually large
            const double maxDim = static_cast<double>(s_maxDecodedDimension);
            double scale = 1.0;
            if (naturalW > maxDim || naturalH > maxDim) {
                scale = maxDim / (std::max)(naturalW, naturalH);
            }

            const uint32_t renderW = (std::max)(1u, static_cast<uint32_t>(naturalW * scale));
            const uint32_t renderH = (std::max)(1u, static_cast<uint32_t>(naturalH * scale));

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

                // RAM Protection: Compute uniform downsample factor if frames exceed s_maxDecodedDimension
                double scale = 1.0;
                if (result.naturalWidth > s_maxDecodedDimension || result.naturalHeight > s_maxDecodedDimension) {
                    scale = static_cast<double>(s_maxDecodedDimension) /
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

        // RAM Protection: If image exceeds s_maxDecodedDimension, downsample display proxy
        if (result.naturalWidth > s_maxDecodedDimension || result.naturalHeight > s_maxDecodedDimension) {
            const double scale = static_cast<double>(s_maxDecodedDimension) / 
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

#if defined(_WIN32)
    // =========================================================================
    // 3. WINDOWS NATIVE WIC PIPELINE (WebP, TIFF, JPEG-XR, HEIC, System Codecs)
    // =========================================================================
    {
        BLImage wicImg;
        std::vector<ImageFrame> wicFrames;
        bool wicIsAnimated = false;
        uint32_t wicW = 0, wicH = 0;
        if (DecodeWithWIC(data, size, wicImg, wicFrames, wicIsAnimated, wicW, wicH) && !wicImg.is_empty()) {
            result.naturalWidth = wicW;
            result.naturalHeight = wicH;
            result.format = (detectedFormat != ImageFormat::Unknown) ? detectedFormat : ImageFormat::WebP;
            result.isAnimated = wicIsAnimated;
            result.frames = std::move(wicFrames);

            // RAM Protection: If image exceeds s_maxDecodedDimension, downsample display proxy
            if (result.naturalWidth > s_maxDecodedDimension || result.naturalHeight > s_maxDecodedDimension) {
                const double scale = static_cast<double>(s_maxDecodedDimension) / 
                                    static_cast<double>((std::max)(result.naturalWidth, result.naturalHeight));
                const int downW = (std::max)(1, static_cast<int>(std::round(result.naturalWidth * scale)));
                const int downH = (std::max)(1, static_cast<int>(std::round(result.naturalHeight * scale)));

                for (auto& f : result.frames) {
                    BLImage downsampled;
                    if (downsampled.create(downW, downH, BL_FORMAT_PRGB32) == BL_SUCCESS) {
                        BLContext ctx(downsampled);
                        ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
                        ctx.blit_image(BLRect(0, 0, downW, downH), f.image);
                        ctx.end();
                        f.image = std::move(downsampled);
                    }
                }

                if (!result.frames.empty()) {
                    result.image = result.frames[0].image;
                } else {
                    BLImage downsampled;
                    if (downsampled.create(downW, downH, BL_FORMAT_PRGB32) == BL_SUCCESS) {
                        BLContext ctx(downsampled);
                        ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
                        ctx.blit_image(BLRect(0, 0, downW, downH), wicImg);
                        ctx.end();
                        result.image = std::move(downsampled);
                    } else {
                        result.image = std::move(wicImg);
                    }
                }
            } else {
                result.image = result.frames.empty() ? std::move(wicImg) : result.frames[0].image;
            }

            result.success = true;
            return result;
        }
    }
#endif

    // =========================================================================
    // 4. EXTENDED FORMAT FALLBACK PIPELINE (GIF, TGA, PCX, etc. via SDL3_image)
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
                result.format = ImageFormat::GIF;
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
                    if (result.naturalWidth > s_maxDecodedDimension || result.naturalHeight > s_maxDecodedDimension) {
                        const double scale = static_cast<double>(s_maxDecodedDimension) / 
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

    result.errorMessage = "Unsupported or corrupted image format";
    return result;
}

// =============================================================================
// FILE RESOLUTION & LOADING
// =============================================================================

DecodedImageResult ImageDecoder::DecodeFromFile(
    const std::string& filePath, 
    const std::string& packageRoot) 
{
    DecodedImageResult result;

    if (filePath.empty()) {
        result.errorMessage = "File path is empty";
        return result;
    }

    std::string resolvedPath = filePath;
    if (!FileManager::Exists(resolvedPath) && !packageRoot.empty()) {
        std::string candidate = FileManager::JoinPath(packageRoot, filePath);
        if (FileManager::Exists(candidate)) {
            resolvedPath = candidate;
        }
    }

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
