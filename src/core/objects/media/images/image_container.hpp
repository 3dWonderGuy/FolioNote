#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/images/image_container.hpp
 * @brief Canvas Object Container Representing Placed Raster Images on the Infinite Canvas
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTERACTION MODEL:
 * -----------------------------------------
 * ImageObject encapsulates raster bitmap data (PNG, JPEG, WebP, BMP, GIF, TIFF, QOI)
 * rendered directly on the infinite canvas.
 *
 * LIFECYCLE: WHAT HAPPENS WHEN YOU INSERT AN IMAGE
 * ------------------------------------------------
 * 1. User Action:
 *    User clicks "Insert Image" (file dialog) or presses Ctrl+V (clipboard paste).
 * 2. In-Memory Decode:
 *    Raw bytes are decoded into a Blend2D BLImage raster surface.
 * 3. Package Deduplication:
 *    The asset is hashed and stored into the notebook companion package
 *    (e.g. "imports/images/<hash>.png") so the notebook remains fully portable.
 * 4. Physical DPI Projection & Clamping:
 *    ImageObject::SetImage() maps source pixels to canvas millimeters:
 *      mmPerPixel = 25.4 / screenDpi
 *      worldWidth = pixelWidth * mmPerPixel
 *      worldHeight = pixelHeight * mmPerPixel
 *    Clamps to a hard maximum limit (200mm) and minimum limit (5mm)
 *    while strictly preserving the source aspect ratio.
 * 5. Placement & Spatial Indexing:
 *    The object is centered at the cursor or viewport center, added to CanvasPage,
 *    and indexed into the R-Tree for sub-millisecond hit-testing.
 * 6. 120 FPS Rendering:
 *    Blend2D blits the cached hardware-accelerated BLImage directly onto the canvas,
 *    applying 2D affine transforms (gizmo scaling, pan, zoom, rotation).
 *
 * Key Responsibilities:
 *   1. Spatial Footprint & Bounds: Tracks coordinates in world millimeters.
 *   2. Aspect Ratio Invariance: Locks proportions during interactive gizmo resize.
 *   3. Spatial Bounding & Hit-Testing: Inherits standard CanvasObject AABB queries.
 *   4. Hardware-Accelerated Rendering: High-performance Blend2D raster blits.
 *   5. Lazy Loading & Sidecar Integration: Decodes on demand from memory or disk.
 */

#include <string>
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

namespace Folio {

/**
 * @class ImageObject
 * @brief Canvas object representing a raster bitmap image.
 */
class ImageObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS & ATTRIBUTES
    // =========================================================================

    uint32_t naturalWidth = 0;              ///< Native source pixel width of the decoded image
    uint32_t naturalHeight = 0;             ///< Native source pixel height of the decoded image
    ImageFormat imageFormat = ImageFormat::PNG; ///< Image compression/encoding format tag
    std::string imagePath = "";             ///< Relative package path (e.g. "imports/images/<hash>.png")
    std::vector<uint8_t> embeddedData;      ///< Raw binary payload (preserved for serialization)
    BLImage cachedBlImage;                  ///< Decoded Blend2D raster surface used for rendering
    bool isLoaded = false;                  ///< True when cachedBlImage is decoded and valid in memory

    bool isAnimated = false;                ///< True if image contains multiple animation frames (e.g. animated GIF)
    std::vector<ImageFrame> frames;         ///< Array of animation frames with per-frame delay timing
    size_t currentFrameIndex = 0;           ///< Index of currently displayed animation frame
    uint64_t lastFrameTickMs = 0;           ///< Timestamp (SDL_GetTicks) of last animation advance

    /**
     * @brief Checks if it is an image or animation
     * @return True if isAnimated is true and more than 1 frame is stored in memory.
     */
    [[nodiscard]] bool IsAnimated() const noexcept {
        return isAnimated && frames.size() > 1;
    }

    /**
     * @brief Responsible for update the frame of animated image
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
     * @brief Basicaly creates a setup needed for animated type of images (times, frames, etc)
     * @param[in] animFrames Decoded animation frames with per-frame delay timers.
     */
    void SetAnimatedFrames(std::vector<ImageFrame> animFrames);

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor creating an uninitialized ImageObject. Used for loading from data base
     */
    ImageObject();

    /**
     * @brief Constructor used to initialize the object with image data. Upon initial creation
     
     * @param[in] img Decoded Blend2D raster image surface.
     * @param[in] path Associated file path or package reference (e.g. "imports/images/<hash>.png").
     * @param[in] dpi Display density in DPI passed once at insertion (defaults to 96.0 fallback if <= 0).
     */
    ImageObject(const BLImage& img, const std::string& path = "", double dpi = 0.0);

    // =========================================================================
    // STATIC CONFIGURATION & HARD CONSTRAINTS
    // =========================================================================

    inline static double s_maxDimensionLimitMm = 200.0;    ///< Hard upper limit for physical width/height (200mm)
    inline static double s_minDimensionLimitMm = 5.0;      ///< Hard lower limit to prevent sub-millimeter disappearance (5mm)

    /**
     * @brief Configures global hard maximum physical dimension constraint for placed images.
     * @param[in] maxMm Upper limit in millimeters (e.g. 200.0mm).
     */
    static void SetMaxDimensionLimitMm(double maxMm) noexcept {
        if (maxMm >= 10.0) s_maxDimensionLimitMm = maxMm;
    }

    // =========================================================================
    // IMAGE ASSIGNMENT & DECODING
    // =========================================================================

    /**
     * @brief Converts whatever resoltion of image was used using Dpi into mm and scaled accordinly
     *
     * Working Process:
     *   1. Screen Pixel to Millimeter Conversion:
     *      Uses standard 1 inch = 25.4 mm ratio:
     *        mmPerPixel = 25.4 / effectiveDpi
     *        rawW = naturalWidth * mmPerPixel
     *        rawH = naturalHeight * mmPerPixel
     *
     *   2. Hard Maximum Limit (s_maxDimensionLimitMm):
     *      If the image exceeds s_maxDimensionLimitMm, scales both dimensions down
     *      proportionally to preserve the exact aspect ratio:
     *        scale = s_maxDimensionLimitMm / max(rawW, rawH)
     *        worldWidth = rawW * scale
     *        worldHeight = rawH * scale
     *
     *   3. Hard Minimum Limit (s_minDimensionLimitMm):
     *      If a tiny snip is smaller than s_minDimensionLimitMm, scales it up so it
     *      doesn't become an unselectable speck on the canvas:
     *        scale = s_minDimensionLimitMm / min(worldWidth, worldHeight)
     *        worldWidth *= scale
     *        worldHeight *= scale
     *
     * @param[in] dpi Target display density in dots-per-inch (defaults to 96.0 fallback if <= 0).
     */
    void CalculateDimensionsFromDpi(double dpi = 0.0);

    /**
     * @brief Assigns a decoded raster surface, caches it in memory, and computes physical
     *        canvas millimeter dimensions from screen DPI with hard size clamping.
     *
     * Working Process:
     *   1. Binds cached BLImage surface and sets isLoaded = true.
     *   2. Deduces imageFormat from file extension if path is provided.
     *   3. Captures source pixel dimensions (naturalWidth, naturalHeight).
     *   4. Delegates to CalculateDimensionsFromDpi() to compute aspect-locked mm bounds.
     *   5. Updates the spatial AABB bounding box for R-Tree indexing.
     *
     * @param[in] img In-memory decoded BLImage surface.
     * @param[in] path Associated file path or package reference.
     * @param[in] dpi Display density in DPI passed at assignment (defaults to 96.0 fallback if <= 0).
     */
    void SetImage(const BLImage& img, const std::string& path = "", double dpi = 0.0);

    /**
     * @brief Ensures the Blend2D raster surface is decoded and resident in memory.
     *
     * Working Process:
     *   1. If already loaded and valid, returns true immediately.
     *   2. If embeddedData is populated, decodes via BLImage::read_from_data.
     *   3. If imagePath is populated, resolves against packageRoot via FileManager and
     *      reads binary payload into memory via FileManager::ReadBinary for safe Unicode decoding.
     *   4. Updates naturalWidth, naturalHeight, imageFormat, and isLoaded flag upon success.
     *
     * @param[in] packageRoot Optional base directory of the active notebook package.
     * @return True if the image surface is valid and ready for rendering; false otherwise.
     */
    bool EnsureLoaded(const std::string& packageRoot = "");

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the image surface or fallback placeholder onto the Blend2D context.
     *
     * Working Process:
     *   1. Verifies visibility; returns immediately if hidden.
     *   2. Lazily invokes EnsureLoaded() if the surface is not yet decoded.
     *   3. Saves Blend2D context state and applies 2D affine transformation.
     *   4. If surface is valid, blits cached BLImage into destination rectangle.
     *   5. If surface is missing or unloaded, draws a placeholder card with outline.
     *   6. Restores context state.
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
     *
     * @return Aspect ratio as double (Width / Height). Returns 1.0 if degenerate.
     */
    [[nodiscard]] double GetAspectRatio() const noexcept;

    /**
     * @brief Resizes width while scaling height proportionally to maintain aspect ratio:
     *        worldHeight = newWidth / aspect.
     *
     * @param[in] newWidth Target width in world mm (must be positive).
     */
    void SetWidthPreservingAspect(double newWidth);

    /**
     * @brief Resizes height while scaling width proportionally to maintain aspect ratio:
     *        worldWidth = newHeight * aspect.
     *
     * @param[in] newHeight Target height in world mm (must be positive).
     */
    void SetHeightPreservingAspect(double newHeight);

    // =========================================================================
    // CLONING
    // =========================================================================

    /**
     * @brief Creates an exact polymorphic duplicate of this ImageObject.
     *
     * @return Unique pointer to cloned ImageObject.
     */
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;
};

} // namespace Folio
