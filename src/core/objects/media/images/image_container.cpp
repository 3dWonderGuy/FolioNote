/**
 * =========================================================================================
 * @file core/objects/media/images/image_container.cpp
 * @brief Implementation of ImageObject High-Level Coordinator and Raster Rendering
 * =========================================================================================
 */

#include "core/objects/media/images/image_container.hpp"
#include "core/objects/media/images/image_decoder.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"

#include <cmath>
#include <algorithm>
#include <SDL3/SDL.h>

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

ImageObject::ImageObject(const uint8_t* data, size_t size, std::string_view filenameHint, double dpi) {
    type = ObjectType::Image;
    worldWidth = 0.0;
    worldHeight = 0.0;
    UpdateBounds();
    LoadFromMemory(data, size, filenameHint, dpi);
}

ImageObject::ImageObject(const std::vector<uint8_t>& data, std::string_view filenameHint, double dpi)
    : ImageObject(data.data(), data.size(), filenameHint, dpi) {}

ImageObject::ImageObject(const std::string& filePath, double dpi) {
    type = ObjectType::Image;
    worldWidth = 0.0;
    worldHeight = 0.0;
    UpdateBounds();
    LoadFromFile(filePath, dpi);
}

bool ImageObject::LoadFromFile(const std::string& filePath, double dpi) {
    loadFailed = false;

    if (filePath.empty()) {
        isLoaded = false;
        loadFailed = true;
        LOG_WARN(CanvasObject, "ImageObject::LoadFromFile rejected: File path is empty.");
        return false;
    }

    imagePath = filePath;

    // Resolve relative companion package path or absolute filesystem disk path
    const std::string resolvedPath = FileManager::ResolveAssetPath(filePath);
    if (!FileManager::Exists(resolvedPath)) {
        isLoaded = false;
        loadFailed = true;
        LOG_ERROR(CanvasObject, "ImageObject::LoadFromFile: File does not exist: " + resolvedPath);
        return false;
    }

    std::vector<uint8_t> fileBuffer;
    if (!FileManager::ReadBinary(resolvedPath, fileBuffer) || fileBuffer.empty()) {
        isLoaded = false;
        loadFailed = true;
        LOG_ERROR(CanvasObject, "ImageObject::LoadFromFile: Failed to read binary bytes from: " + resolvedPath);
        return false;
    }

    return LoadFromMemory(fileBuffer.data(), fileBuffer.size(), filePath, dpi);
}

bool ImageObject::LoadFromMemory(const uint8_t* data, size_t size, std::string_view filenameHint, double dpi) {
    loadFailed = false;

    if (!data || size == 0) {
        isLoaded = false;
        loadFailed = true;
        LOG_WARN(CanvasObject, "ImageObject::LoadFromMemory rejected: Buffer pointer is null or size is 0 bytes.");
        return false;
    }

    if (size > ObjectConfig::Get().maxImageFileSizeBytes) {
        isLoaded = false;
        loadFailed = true;
        LOG_WARN(CanvasObject, "ImageObject::LoadFromMemory rejected: Buffer size (" + 
                               std::to_string(size) + " bytes) exceeds maximum configured limit (" + 
                               std::to_string(ObjectConfig::Get().maxImageFileSizeBytes) + " bytes).");
        return false;
    }

    if (!filenameHint.empty()) {
        ImageFormat detected = ImageFormatFromExtension(filenameHint);
        if (detected != ImageFormat::Unknown) {
            imageFormat = detected;
        }

        // Only adopt filenameHint as persistent imagePath if it represents an actual filename or path,
        // rather than just a bare extension hint (e.g. ".png" or "webp").
        const bool isBareExtension = (filenameHint.front() == '.' && filenameHint.find_first_of("/\\") == std::string_view::npos) ||
                                     (filenameHint.find_first_of("./\\") == std::string_view::npos);
        if (!isBareExtension) {
            imagePath = std::string(filenameHint);
        }
    }

    // Retain binary buffer for document persistence/serialization
    embeddedData.assign(data, data + size);

    DecodedImageResult result = ImageDecoder::DecodeFromMemory(data, size, filenameHint);
    if (!result.success || (result.image.is_empty() && result.frames.empty())) {
        isLoaded = false;
        loadFailed = true;
        const std::string hintStr = filenameHint.empty() ? "<in-memory payload>" : std::string(filenameHint);
        const std::string reason = result.errorMessage.empty() ? "Unsupported format or corrupted payload" : result.errorMessage;
        LOG_ERROR(CanvasObject, "ImageObject::LoadFromMemory failed to decode " + hintStr + " (" + std::to_string(size) + " bytes): " + reason);
        return false;
    }

    if (result.isAnimated && result.frames.size() > 1) {
        isAnimated = true;
        isAnimationPaused = false;
        frames = std::move(result.frames);
        cachedBlImage = frames[0].image;
        currentFrameIndex = 0;
        lastFrameTickMs = 0;
    } else {
        isAnimated = false;
        isAnimationPaused = false;
        frames.clear();
        cachedBlImage = std::move(result.image);
    }

    naturalWidth = result.naturalWidth;
    naturalHeight = result.naturalHeight;
    if (result.format != ImageFormat::Unknown) {
        imageFormat = result.format;
    }
    isLoaded = true;

    CalculateDimensionsFromDpi(dpi);
    UpdateBounds();
    return true;
}

void ImageObject::SetSurface(const BLImage& img, double dpi) {
    cachedBlImage = img;
    frames.clear();
    isAnimated = false;
    isAnimationPaused = false;
    currentFrameIndex = 0;
    lastFrameTickMs = 0;
    isLoaded = !img.is_empty();
    loadFailed = img.is_empty();

    if (isLoaded) {
        naturalWidth = static_cast<uint32_t>(img.width());
        naturalHeight = static_cast<uint32_t>(img.height());
        CalculateDimensionsFromDpi(dpi);
    }
    UpdateBounds();
}

bool ImageObject::EnsureLoaded() {
    if (isLoaded && !cachedBlImage.is_empty()) {
        return true;
    }

    // Suppress repeated reload thrashing and 120 FPS console log spamming if previous load failed
    if (loadFailed) {
        return false;
    }

    // Preserve custom bounds if already deserialized or set
    const double savedW = worldWidth;
    const double savedH = worldHeight;
    bool ok = false;

    if (!embeddedData.empty()) {
        ok = LoadFromMemory(embeddedData.data(), embeddedData.size(), imagePath);
    } else if (!imagePath.empty()) {
        // Fallback: reload from package companion asset or physical filesystem path
        ok = LoadFromFile(imagePath);
    } else {
        LOG_WARN(CanvasObject, "ImageObject::EnsureLoaded: No embedded buffer or valid image path available to restore image surface.");
    }

    if (ok && savedW > 0.0 && savedH > 0.0) {
        worldWidth = savedW;
        worldHeight = savedH;
        UpdateBounds();
    } else if (!ok) {
        loadFailed = true;
        LOG_ERROR(CanvasObject, "ImageObject::EnsureLoaded: Failed to restore surface for '" + imagePath + "'. Displaying placeholder fallback.");
    }

    return ok;
}

// =============================================================================
// PHYSICAL DPI SCALING & DIMENSION CALCULATION
// =============================================================================

void ImageObject::CalculateDimensionsFromDpi(double dpi) {
    // Robust dimension resolution: if naturalWidth/naturalHeight are 0, recover from frames or cached surface
    if ((naturalWidth == 0 || naturalHeight == 0) && !frames.empty() && !frames[0].image.is_empty()) {
        naturalWidth = static_cast<uint32_t>(frames[0].image.width());
        naturalHeight = static_cast<uint32_t>(frames[0].image.height());
    }
    if ((naturalWidth == 0 || naturalHeight == 0) && !cachedBlImage.is_empty()) {
        naturalWidth = static_cast<uint32_t>(cachedBlImage.width());
        naturalHeight = static_cast<uint32_t>(cachedBlImage.height());
    }

    if (naturalWidth == 0 || naturalHeight == 0) {
        return;
    }

    // Default to configured standard desktop display density fallback
    const double fallbackDpi = ObjectConfig::Get().defaultImageDpi;
    const double effectiveDpi = (dpi >= 10.0) ? dpi : (fallbackDpi >= 10.0 ? fallbackDpi : 96.0);

    // Convert pixel dimensions to physical millimeters: 1 inch = 25.4 mm
    const double mmPerPixel = 25.4 / effectiveDpi;
    double rawW = static_cast<double>(naturalWidth) * mmPerPixel;
    double rawH = static_cast<double>(naturalHeight) * mmPerPixel;

    // 1. Hard maximum limit clamping (preserves exact source aspect ratio)
    const double maxLimitMm = ObjectConfig::Get().maxImageCanvasDimensionMm;
    const double maxDimension = (std::max)(rawW, rawH);
    if (maxLimitMm > 0.0 && maxDimension > maxLimitMm) {
        const double downscale = maxLimitMm / maxDimension;
        rawW *= downscale;
        rawH *= downscale;
    }

    // 2. Hard minimum limit clamping (prevents sub-millimeter disappearance)
    const double minLimitMm = ObjectConfig::Get().minImageCanvasDimensionMm;
    const double minDimension = (std::min)(rawW, rawH);
    if (minLimitMm > 0.0 && minDimension < minLimitMm) {
        const double upscale = minLimitMm / minDimension;
        rawW *= upscale;
        rawH *= upscale;
    }

    worldWidth = rawW;
    worldHeight = rawH;
}

// =============================================================================
// ANIMATION CONTROL & TIMING
// =============================================================================

bool ImageObject::UpdateAnimation(uint64_t nowMs) {
    if (isAnimationPaused || !isAnimated || frames.size() <= 1) {
        return false;
    }

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

void ImageObject::StepNextFrame() noexcept {
    if (!frames.empty()) {
        currentFrameIndex = (currentFrameIndex + 1) % frames.size();
        cachedBlImage = frames[currentFrameIndex].image;
    }
}

void ImageObject::StepPreviousFrame() noexcept {
    if (!frames.empty()) {
        currentFrameIndex = (currentFrameIndex == 0) ? (frames.size() - 1) : (currentFrameIndex - 1);
        cachedBlImage = frames[currentFrameIndex].image;
    }
}

void ImageObject::ResetToFirstFrame() noexcept {
    if (!frames.empty()) {
        currentFrameIndex = 0;
        cachedBlImage = frames[0].image;
        lastFrameTickMs = 0;
    }
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

// =============================================================================
// CONTEXT MENU & OBJECT ACTIONS
// =============================================================================

void ImageObject::CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) {
    // 1. Reset to 100% Size (1:1 DPI projection)
    Folio::ContextMenuItem resetSizeAct;
    resetSizeAct.label = "Reset to 100% Size";
    resetSizeAct.shortcut = "1:1";
    resetSizeAct.icon = "🔍";
    resetSizeAct.iconKey = "zoom_reset";
    resetSizeAct.order = 50;
    resetSizeAct.onTrigger = [this]() {
        transform = BLMatrix2D::make_identity();
        CalculateDimensionsFromDpi(96.0);
        UpdateBounds();
        if (onVisualStateChanged) {
            onVisualStateChanged();
        }
    };
    actions.push_back(std::move(resetSizeAct));

    // 2. Copy Image to Clipboard
    Folio::ContextMenuItem copyAct;
    copyAct.label = "Copy Image to Clipboard";
    copyAct.shortcut = "Ctrl+C";
    copyAct.icon = "📋";
    copyAct.iconKey = "copy";
    copyAct.order = 51;
    copyAct.onTrigger = [this]() {
        if (!imagePath.empty()) {
            FileManager::SetClipboardText(imagePath);
        }
    };
    actions.push_back(std::move(copyAct));

    // 3. Save Image As...
    Folio::ContextMenuItem saveAct;
    saveAct.label = "Save Image As...";
    saveAct.icon = "💾";
    saveAct.iconKey = "save";
    saveAct.order = 52;
    saveAct.onTrigger = [this]() {
        std::string suggestedName = "image.png";
        if (!imagePath.empty()) {
            suggestedName = FileManager::GetFileName(imagePath);
        }
        std::string saveDest = FileManager::ShowSaveFileDialog("Save Image As", suggestedName);
        if (!saveDest.empty()) {
            bool saveOk = false;
            if (!embeddedData.empty()) {
                saveOk = FileManager::WriteBinaryAtomic(saveDest, embeddedData);
            } else if (!cachedBlImage.is_empty()) {
                saveOk = (cachedBlImage.write_to_file(saveDest.c_str()) == BL_SUCCESS);
            }
            if (!saveOk) {
                LOG_ERROR(CanvasObject, "ImageObject: Failed to save image to '" + saveDest + "'.");
            }
        }
    };
    actions.push_back(std::move(saveAct));

    // 4. Animation Controls (only visible if multi-frame animation)
    if (IsAnimated()) {
        Folio::ContextMenuItem pauseAct;
        pauseAct.label = isAnimationPaused ? "Play Animation" : "Pause Animation";
        pauseAct.icon = isAnimationPaused ? "▶" : "⏸";
        pauseAct.iconKey = isAnimationPaused ? "play" : "pause";
        pauseAct.order = 40;
        pauseAct.isSeparatorBefore = true;
        pauseAct.onTrigger = [this]() {
            ToggleAnimationPlayPause();
            if (onVisualStateChanged) {
                onVisualStateChanged();
            }
        };
        actions.push_back(std::move(pauseAct));

        Folio::ContextMenuItem nextAct;
        nextAct.label = "Next Frame";
        nextAct.icon = "⏭";
        nextAct.iconKey = "next";
        nextAct.order = 41;
        nextAct.onTrigger = [this]() {
            StepNextFrame();
            if (onVisualStateChanged) {
                onVisualStateChanged();
            }
        };
        actions.push_back(std::move(nextAct));

        Folio::ContextMenuItem prevAct;
        prevAct.label = "Previous Frame";
        prevAct.icon = "⏮";
        prevAct.iconKey = "prev";
        prevAct.order = 42;
        prevAct.onTrigger = [this]() {
            StepPreviousFrame();
            if (onVisualStateChanged) {
                onVisualStateChanged();
            }
        };
        actions.push_back(std::move(prevAct));

        Folio::ContextMenuItem resetFrameAct;
        resetFrameAct.label = "Reset to Frame 0";
        resetFrameAct.icon = "⏮";
        resetFrameAct.iconKey = "rewind";
        resetFrameAct.order = 43;
        resetFrameAct.onTrigger = [this]() {
            ResetToFirstFrame();
            if (onVisualStateChanged) {
                onVisualStateChanged();
            }
        };
        actions.push_back(std::move(resetFrameAct));
    }
}

// =============================================================================
// CLONING
// =============================================================================

std::unique_ptr<CanvasObject> ImageObject::Clone() const {
    return std::make_unique<ImageObject>(*this);
}

} // namespace Folio
