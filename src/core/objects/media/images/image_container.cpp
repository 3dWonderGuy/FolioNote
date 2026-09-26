/**
 * =========================================================================================
 * @file core/objects/media/images/image_container.cpp
 * @brief Implementation of ImageObject Methods and Raster Rendering Pipeline
 * =========================================================================================
 */

#include "core/objects/media/images/image_container.hpp"
#include "core/objects/media/images/image_decoder.hpp"
#include "io/file_manager.hpp"

namespace Folio {

// =============================================================================
// CONSTRUCTORS
// =============================================================================

ImageObject::ImageObject() {
    type = ObjectType::Image;
    worldWidth = 0.0;
    worldHeight = 0.0;
    UpdateBounds();
}

ImageObject::ImageObject(const BLImage& img, const std::string& path, double dpi) {
    type = ObjectType::Image;
    SetImage(img, path, dpi);
}

// =============================================================================
// IMAGE ASSIGNMENT & DECODING
// =============================================================================

void ImageObject::CalculateDimensionsFromDpi(double dpi) {
    
    
    // check if natural dimensions are set
    if (naturalWidth == 0 || naturalHeight == 0) {
        return;
    }

    // default to 96 dpi if not set
    const double effectiveDpi = (dpi >= 10.0) ? dpi : 96.0;

    // Convert pixel dimensions to physical millimeters: 1 inch = 25.4 mm
    const double mmPerPixel = 25.4 / effectiveDpi;
    double rawW = static_cast<double>(naturalWidth) * mmPerPixel;
    double rawH = static_cast<double>(naturalHeight) * mmPerPixel;

    // 1. Hard maximum limit clamping (preserves aspect ratio)
    const double maxDimension = (std::max)(rawW, rawH);
    if (maxDimension > s_maxDimensionLimitMm && maxDimension > 0.0) {
        const double downscale = s_maxDimensionLimitMm / maxDimension;
        rawW *= downscale;
        rawH *= downscale;
    }

    // 2. Hard minimum limit clamping (prevents sub-millimeter disappearance)
    const double minDimension = (std::min)(rawW, rawH);
    if (minDimension < s_minDimensionLimitMm && minDimension > 0.0) {
        const double upscale = s_minDimensionLimitMm / minDimension;
        rawW *= upscale;
        rawH *= upscale;
    }

    worldWidth = rawW;
    worldHeight = rawH;
}

void ImageObject::SetImage(const BLImage& img, const std::string& path, double dpi) {
    cachedBlImage = img;
    imagePath = path;
    isLoaded = !img.is_empty();
    frames.clear();
    isAnimated = false;
    currentFrameIndex = 0;
    lastFrameTickMs = 0;

    if (!path.empty()) {
        ImageFormat detected = ImageFormatFromExtension(path);
        if (detected != ImageFormat::Unknown) {
            imageFormat = detected;
        }
    }

    if (isLoaded) {
        naturalWidth = static_cast<uint32_t>(img.width());
        naturalHeight = static_cast<uint32_t>(img.height());
        CalculateDimensionsFromDpi(dpi);
    }
    UpdateBounds();
}

void ImageObject::SetAnimatedFrames(std::vector<ImageFrame> animFrames) {
    frames = std::move(animFrames);
    isAnimated = (frames.size() > 1);
    currentFrameIndex = 0;
    lastFrameTickMs = 0;
    if (!frames.empty()) {
        cachedBlImage = frames[0].image;
        naturalWidth = static_cast<uint32_t>(cachedBlImage.width());
        naturalHeight = static_cast<uint32_t>(cachedBlImage.height());
        isLoaded = true;
    }
}

bool ImageObject::UpdateAnimation(uint64_t nowMs) {
    if (!isAnimated || frames.size() <= 1) return false;

    if (lastFrameTickMs == 0) {
        lastFrameTickMs = nowMs;
        return false;
    }

    uint32_t delay = frames[currentFrameIndex].delayMs;
    // Standard web browser clamp: delays <= 10ms are clamped to 100ms (10 FPS)
    // to protect system scheduler from ancient broken GIF encoders.
    if (delay <= 10) {
        delay = 100;
    }

    if (nowMs - lastFrameTickMs >= delay) {
        currentFrameIndex = (currentFrameIndex + 1) % frames.size();
        cachedBlImage = frames[currentFrameIndex].image;
        lastFrameTickMs = nowMs;
        return true;
    }

    return false;
}

bool ImageObject::EnsureLoaded(const std::string& packageRoot) {
    if (isLoaded && !cachedBlImage.is_empty()) {
        return true;
    }

    DecodedImageResult result;

    // 1. Attempt decoding from raw embedded memory buffer if available
    if (!embeddedData.empty()) {
        result = ImageDecoder::DecodeFromMemory(embeddedData.data(), embeddedData.size(), imagePath);
    }
    // 2. Otherwise attempt decoding from physical file on disk via FileManager
    else if (!imagePath.empty()) {
        result = ImageDecoder::DecodeFromFile(imagePath, packageRoot);
    }

    if (result.success) {
        cachedBlImage = std::move(result.image);
        frames = std::move(result.frames);
        isAnimated = result.isAnimated;
        currentFrameIndex = 0;
        lastFrameTickMs = 0;
        naturalWidth = result.naturalWidth;
        naturalHeight = result.naturalHeight;
        if (result.format != ImageFormat::Unknown) {
            imageFormat = result.format;
        }
        isLoaded = true;
        return true;
    }

    return false;
}

// =============================================================================
// RENDERING
// =============================================================================

void ImageObject::Render(BLContext& ctx, const Viewport& viewport) const {
    if (!isVisible) return;

    if (!isLoaded || cachedBlImage.is_empty()) {
        const_cast<ImageObject*>(this)->EnsureLoaded();
    }

    ctx.save();
    ctx.apply_transform(transform);

    if (opacity < 0.999f) {
        ctx.set_global_alpha(static_cast<double>(opacity));
    }

    if (isLoaded && !cachedBlImage.is_empty()) {
        // High-performance hardware-accelerated raster blit
        ctx.blit_image(BLRect(worldX, worldY, worldWidth, worldHeight), cachedBlImage);
    } else {
        // Sleek placeholder frame when the image file is unloaded or missing
        ctx.set_fill_style(BLRgba32(0x18, 0x1C, 0x24, static_cast<uint8_t>(opacity * 200)));
        ctx.fill_round_rect(BLRoundRect(worldX, worldY, worldWidth, worldHeight, 4.0, 4.0));

        ctx.set_stroke_style(BLRgba32(0x3B, 0x44, 0x54, static_cast<uint8_t>(opacity * 255)));
        const double strokeScale = (viewport.zoom > 0.001) ? (1.0 / viewport.zoom) : 1.0;
        ctx.set_stroke_width(strokeScale);
        ctx.stroke_round_rect(BLRoundRect(worldX, worldY, worldWidth, worldHeight, 4.0, 4.0));
    }

    ctx.restore();
}

// =============================================================================
// ASPECT RATIO & SIZING UTILITIES
// =============================================================================

double ImageObject::GetAspectRatio() const noexcept {
    if (naturalWidth > 0 && naturalHeight > 0) {
        return static_cast<double>(naturalWidth) / static_cast<double>(naturalHeight);
    }
    return (worldHeight > 0.001) ? (worldWidth / worldHeight) : 1.0;
}

void ImageObject::SetWidthPreservingAspect(double newWidth) {
    if (newWidth <= 0.0) return;
    const double aspect = GetAspectRatio();
    worldWidth = newWidth;
    worldHeight = (aspect > 0.001) ? (newWidth / aspect) : worldHeight;
    UpdateBounds();
}

void ImageObject::SetHeightPreservingAspect(double newHeight) {
    if (newHeight <= 0.0) return;
    const double aspect = GetAspectRatio();
    worldHeight = newHeight;
    worldWidth = (aspect > 0.001) ? (newHeight * aspect) : worldWidth;
    UpdateBounds();
}

std::unique_ptr<CanvasObject> ImageObject::Clone() const {
    return std::make_unique<ImageObject>(*this);
}

} // namespace Folio
