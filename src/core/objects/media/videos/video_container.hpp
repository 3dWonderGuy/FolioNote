#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/videos/video_container.hpp
 * @brief Canvas Object Representing a Linked Video File or YouTube Stream
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTERACTION MODEL:
 * -----------------------------------------
 * VideoObject represents a resizable canvas video player element that references either
 * a local video file (MP4, MOV, MKV, WebM) stored in the companion notebook sidecar, or
 * a remote online video URL (e.g. YouTube stream).
 *
 * Key Interaction Principles:
 *   1. Resizable with Aspect Lock:
 *      Corner gizmo handles preserve video aspect ratio (typically 16:9).
 *   2. Local and Remote URL Detection:
 *      Automatically determines whether source is YouTube or local filesystem asset.
 *   3. Vector Placeholder & Playback Overlay:
 *      Renders a dark aesthetic player card with centered play/pause controls, YouTube badge,
 *      and thumbnail support.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <vector>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @class VideoObject
 * @brief Resizable video canvas container.
 */
class VideoObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS & ATTRIBUTES
    // =========================================================================

    std::string sourceUrl;           ///< Local filesystem path or YouTube URL
    std::string displayName;         ///< Label shown in UI or tooltip (filename or video title)
    std::string thumbnailPath;       ///< Path to cached thumbnail poster image (if available)

    double aspectRatio = 16.0 / 9.0; ///< Current aspect ratio (width / height); used for corner lock
    bool isPlaying = false;          ///< Current playback state flag

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    /**
     * @brief Default constructor setting 16:9 standard dimensions (80mm x 45mm).
     */
    VideoObject();

    /**
     * @brief Parameterized constructor initializing URL, label, and dimensions.
     *
     * @param[in] url Source file path or web stream URL.
     * @param[in] name Descriptive label.
     * @param[in] w Initial width in world mm.
     * @param[in] h Initial height in world mm.
     */
    VideoObject(const std::string& url, const std::string& name, double w = 80.0, double h = 45.0);

    // =========================================================================
    // SOURCE TYPE HELPERS
    // =========================================================================

    /**
     * @brief Checks if sourceUrl is recognized as a YouTube video link.
     *
     * @return True if URL contains youtube.com or youtu.be.
     */
    [[nodiscard]] bool IsYouTube() const noexcept;

    /**
     * @brief Extracts the unique YouTube video identifier from the source URL.
     *
     * @return Extracted video ID (e.g. "dQw4w9WgXcQ"), or empty string if invalid.
     */
    [[nodiscard]] std::string GetYouTubeId() const;

    // =========================================================================
    // PLAYBACK CONTROLS
    // =========================================================================

    /**
     * @brief Initiates video playback.
     */
    void Play();

    /**
     * @brief Stops video playback.
     */
    void Stop();

    // =========================================================================
    // TRANSFORM & BAKING
    // =========================================================================

    /**
     * @brief Bakes axis-aligned scale + translation into worldX/Y/W/H and refreshes aspectRatio.
     */
    void BakeTransform() override;

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the video player card, play icon overlay, and badges.
     *
     * @param[in,out] ctx Blend2D rendering context.
     * @param[in] viewport Current camera viewport.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // CLONING
    // =========================================================================

    /**
     * @brief Clones this VideoObject.
     *
     * @return Unique pointer to cloned instance.
     */
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;
};

// Canonical type alias for backwards compatibility
using VideoContainer = VideoObject;

} // namespace Folio
