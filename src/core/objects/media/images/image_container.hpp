#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/images/image_container.hpp
 * @brief Canvas Object Container Representing Placed Raster Images on the Infinite Canvas
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & UNIFIED COORDINATOR:
 * -------------------------------------------
 * ImageObject serves as the sole, high-level coordinator for image loading, decoding,
 * format deduction, animation frame unpacking, and physical millimeter projection.
 *
 * Callers and import pipelines simply save the raw payload to the notebook package sidecar
 * (e.g. "imports/images/<hash>.ext"), instantiate an ImageObject, and call:
 *   - LoadFromSource(relativePath, dpi) for files on disk, or
 *   - LoadFromMemory(data, size, filenameHint, dpi) for clipboard paste / embedded payloads.
 *
 * All codec delegation (Blend2D, SDL_image, LunaSVG), multi-frame extraction (GIF / animated WebP),
 * RAM safety clamping, aspect-locked physical millimeter sizing, and AABB computation
 * are handled internally.
 */

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/objects/media/images/image_format.hpp"
#include "core/objects/media/images/image_decoder.hpp"
#include "core/objects/object_config.hpp"
#include "app/context_menu_item.hpp"

namespace Folio {

/**
 * @class ImageObject
 * @brief Canvas object representing a raster bitmap image or multi-frame animation.
 */
class ImageObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS & ATTRIBUTES
    // =========================================================================

    uint32_t naturalWidth = 0;                  ///< Native source pixel width of the decoded image
    uint32_t naturalHeight = 0;                 ///< Native source pixel height of the decoded image
    ImageFormat imageFormat = ImageFormat::PNG; ///< Image compression/encoding format tag
    std::string imagePath = "";                 ///< Relative package path (e.g. "imports/images/<hash>.png")
    std::vector<uint8_t> embeddedData;          ///< Raw binary payload (preserved for serialization)
    BLImage cachedBlImage;                      ///< Decoded Blend2D raster surface used for rendering
    bool isLoaded = false;                      ///< True when cachedBlImage is decoded and valid in memory

    bool isAnimated = false;                    ///< True if image contains multiple animation frames (e.g. animated GIF/WebP)
    bool isAnimationPaused = false;             ///< When true, automatic frame advance is suspended
    bool loadFailed = false;                    ///< Set when a load attempt fails to prevent infinite reload thrashing and log spamming
    std::vector<ImageFrame> frames;             ///< Array of animation frames with per-frame delay timing
    size_t currentFrameIndex = 0;               ///< Index of currently displayed animation frame
    uint64_t lastFrameTickMs = 0;               ///< Timestamp (SDL_GetTicks) of last animation advance

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor creating an uninitialized ImageObject.
     * Sets type to ObjectType::Image, zero initial dimensions, and identity transform.
     */
    ImageObject();

    /**
     * @brief Data-centric constructor creating an ImageObject directly from a raw byte buffer.
     * Operates purely in memory with no filesystem coupling.
     *
     * @param[in] data Pointer to raw image bytes.
     * @param[in] size Size of the byte buffer.
     * @param[in] filenameHint Optional filename or extension hint (e.g. "image.png") for codec deduction.
     * @param[in] dpi Target display density in DPI for physical scaling (defaults to 96.0).
     */
    ImageObject(const uint8_t* data, size_t size, std::string_view filenameHint = "", double dpi = 96.0);

    /**
     * @brief Data-centric constructor creating an ImageObject from a std::vector buffer.
     *
     * @param[in] data Byte vector containing the image payload.
     * @param[in] filenameHint Optional filename or extension hint for codec deduction.
     * @param[in] dpi Target display density in DPI for physical scaling (defaults to 96.0).
     */
    ImageObject(const std::vector<uint8_t>& data, std::string_view filenameHint = "", double dpi = 96.0);

    /**
     * @brief File-centric constructor creating an ImageObject from a disk path or package companion asset.
     *
     * Working Process:
     *   1. Sets imagePath to filePath.
     *   2. Delegates loading and decoding to LoadFromFile(filePath, dpi).
     *
     * @param[in] filePath Relative package path (e.g. "imports/images/<hash>.png") or absolute disk path.
     * @param[in] dpi Target display density in DPI for physical scaling (defaults to 96.0).
     */
    ImageObject(const std::string& filePath, double dpi = 96.0);

    // =========================================================================
    // UNIFIED LOADING & COORDINATION API
    // =========================================================================

    /**
     * @brief Loads and decodes an image directly from a physical disk path or companion package asset.
     *
     * Working Process:
     *   1. Resolves path using FileManager::ResolveAssetPath (handles package roots and disk paths).
     *   2. Reads binary bytes into embeddedData via FileManager::ReadBinary for Unicode-safe I/O.
     *   3. Delegates decoding and layout sizing to LoadFromMemory using resolved payload bytes.
     *
     * @param[in] filePath Relative package path or absolute filesystem path.
     * @param[in] dpi Target display density in DPI for physical scaling (defaults to 96.0).
     * @return True if file reading, decoding, and physical dimension calculation succeeded; false otherwise.
     */
    bool LoadFromFile(const std::string& filePath, double dpi = 96.0);

    /**
     * @brief Unified entry point to decode image bytes from an in-memory buffer (e.g. clipboard paste or embedded payloads).
     *
     * Working Process:
     *   1. If filenameHint is provided, infer imagePath and format tag.
     *   2. Cache raw bytes in embeddedData for lossless document serialization.
     *   3. Delegate decoding to ImageDecoder::DecodeFromMemory(data, size, filenameHint).
     *   4. Unpack static or animated frames into cachedBlImage and frames array.
     *   5. Compute physical millimeter dimensions and update AABB bounds.
     *
     * @param[in] data Pointer to raw image bytes.
     * @param[in] size Size of the byte buffer.
     * @param[in] filenameHint Optional filename or extension hint (e.g. "clipboard.png") for codec deduction.
     * @param[in] dpi Target display density in DPI for physical scaling (defaults to 96.0).
     * @return True if loading and decoding succeeded; false on error.
     */
    bool LoadFromMemory(const uint8_t* data, size_t size, std::string_view filenameHint = "", double dpi = 96.0);

    /**
     * @brief Directly assigns a pre-rendered or programmatically generated raster surface (snapshots/previews).
     *
     * @param[in] img Blend2D raster image surface.
     * @param[in] dpi Target display density in DPI for physical scaling (defaults to 96.0).
     */
    void SetSurface(const BLImage& img, double dpi = 96.0);

    /**
     * @brief Ensures the Blend2D raster surface is decoded and resident in memory.
     * Automatically restores from embedded buffer or resolves imagePath via FileManager without duplicating decoding logic.
     *
     * @return True if the image surface is valid and ready for rendering; false otherwise.
     */
    bool EnsureLoaded();

    // =========================================================================
    // ANIMATION CONTROL & TIMING
    // =========================================================================

    /**
     * @brief Checks if this image is an active multi-frame animation.
     * @return True if isAnimated is true and more than 1 frame is stored in memory.
     */
    [[nodiscard]] bool IsAnimated() const noexcept {
        return isAnimated && frames.size() > 1;
    }

    /**
     * @brief Advances animation frame if elapsed time exceeds the current frame's delay.
     *
     * Mathematical & Timing Model:
     *   deltaT = nowMs - lastFrameTickMs
     *   delay  = max(20ms, frames[currentFrameIndex].delayMs)  // Safety clamp against degenerate 0/1ms GIFs
     *   if deltaT >= delay:
     *     currentFrameIndex = (currentFrameIndex + 1) % frameCount
     *     cachedBlImage = frames[currentFrameIndex].image
     *     lastFrameTickMs = nowMs
     *     return true  // Signal canvas rebake needed
     *
     * @param[in] nowMs Current application time in milliseconds (e.g. from SDL_GetTicks()).
     * @return True if the frame was advanced (requiring static canvas rebake); false if unchanged.
     */
    bool UpdateAnimation(uint64_t nowMs);

    /**
     * @brief Toggles animation playback between playing and paused.
     */
    void ToggleAnimationPlayPause() noexcept {
        isAnimationPaused = !isAnimationPaused;
    }

    /**
     * @brief Steps forward to the next animation frame immediately.
     */
    void StepNextFrame() noexcept;

    /**
     * @brief Steps backward to the previous animation frame immediately.
     */
    void StepPreviousFrame() noexcept;

    /**
     * @brief Resets playback to the initial animation frame (frame 0).
     */
    void ResetToFirstFrame() noexcept;

    // =========================================================================
    // STATIC CONFIGURATION & HARD CONSTRAINTS (DELEGATED TO OBJECTCONFIG)
    // =========================================================================

    /**
     * @brief Configures global hard maximum physical dimension constraint for placed images.
     * Delegates directly to centralized ObjectConfig.
     * @param[in] maxMm Upper limit in millimeters (e.g. 200.0mm).
     */
    static void SetMaxDimensionLimitMm(double maxMm) noexcept {
        if (maxMm >= 10.0) ObjectConfig::Get().maxImageCanvasDimensionMm = maxMm;
    }

    [[nodiscard]] static double GetMaxDimensionLimitMm() noexcept {
        return ObjectConfig::Get().maxImageCanvasDimensionMm;
    }

    /**
     * @brief Configures global hard minimum physical dimension constraint for placed images.
     * @param[in] minMm Lower limit in millimeters (e.g. 5.0mm).
     */
    static void SetMinDimensionLimitMm(double minMm) noexcept {
        if (minMm >= 1.0) ObjectConfig::Get().minImageCanvasDimensionMm = minMm;
    }

    [[nodiscard]] static double GetMinDimensionLimitMm() noexcept {
        return ObjectConfig::Get().minImageCanvasDimensionMm;
    }

    // =========================================================================
    // PHYSICAL DPI SCALING
    // =========================================================================

    /**
     * @brief Converts native pixel resolution into world millimeters via screen DPI,
     *        enforcing hard maximum and minimum constraints while strictly preserving aspect ratio.
     *
     * Mathematical Derivation:
     *   mmPerPixel = 25.4 / effectiveDpi
     *   rawW = naturalWidth * mmPerPixel
     *   rawH = naturalHeight * mmPerPixel
     *
     *   maxDimension = max(rawW, rawH)
     *   if maxDimension > s_maxDimensionLimitMm:
     *     downscale = s_maxDimensionLimitMm / maxDimension
     *     rawW *= downscale, rawH *= downscale
     *
     *   minDimension = min(rawW, rawH)
     *   if minDimension < s_minDimensionLimitMm:
     *     upscale = s_minDimensionLimitMm / minDimension
     *     rawW *= upscale, rawH *= upscale
     *
     * @param[in] dpi Target display density in dots-per-inch (defaults to 96.0 fallback if <= 0).
     */
    void CalculateDimensionsFromDpi(double dpi = 0.0);

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the image surface or fallback placeholder onto the Blend2D context.
     *
     * @param[in,out] ctx Blend2D graphics rendering context.
     * @param[in] viewport Current camera viewport settings.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // ASPECT RATIO & SIZING UTILITIES
    // =========================================================================

    /**
     * @brief Computes the native or current aspect ratio (Width / Height).
     * @return Aspect ratio as double (Width / Height). Returns 1.0 if degenerate.
     */
    [[nodiscard]] double GetAspectRatio() const noexcept;

    /**
     * @brief Resizes width while scaling height proportionally to maintain aspect ratio.
     * @param[in] newWidth Target width in world mm (must be positive).
     */
    void SetWidthPreservingAspect(double newWidth);

    /**
     * @brief Resizes height while scaling width proportionally to maintain aspect ratio.
     * @param[in] newHeight Target height in world mm (must be positive).
     */
    void SetHeightPreservingAspect(double newHeight);

    // =========================================================================
    // CONTEXT MENU & OBJECT ACTIONS
    // =========================================================================

    /**
     * @brief Injects image-specific actions into the interactive right-click context menu.
     * Adds: 100% Size Reset, Copy to Clipboard, Save Image As, and Animation Controls.
     *
     * @param[in,out] actions Mutable vector of menu items to append actions to.
     */
    void CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) override;

    // =========================================================================
    // CLONING
    // =========================================================================

    /// Callback invoked when internal context actions (e.g. Reset 100%, animation frame steps)
    /// alter geometry or visual state, notifying the canvas engine to rebake and update gizmo bounds immediately.
    std::function<void()> onVisualStateChanged;

    /**
     * @brief Creates an exact polymorphic duplicate of this ImageObject.
     * @return Unique pointer to cloned ImageObject.
     */
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;
};

} // namespace Folio
