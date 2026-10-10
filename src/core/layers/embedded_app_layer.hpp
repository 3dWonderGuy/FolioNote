#pragma once
/**
 * =========================================================================================
 * @file core/layers/embedded_app_layer.hpp
 * @brief Layer 3 Host: Runtime Coordinator for Running Embedded Widgets and Native Controls
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE & COORDINATE PROJECTION:
 * -------------------------------------------
 * In FolioNote's 3-layer compositing system, `EmbeddedAppLayer` serves as Layer 3:
 * 1. Runtime Isolation:
 *    - Manages live interactive applications embedded within canvas documents, such as
 *      video playback surfaces (libVLC), web browser views (WebView2), or live code editors.
 *    - Decouples app rendering execution from the static vector baking pipeline (Layer 1).
 *
 * 2. Coordinate Mapping:
 *    - Each mounted app is associated with a document `CanvasObject` defining spatial bounds
 *      in physical canvas world millimeters (worldX, worldY, worldWidth, worldHeight).
 *    - Layer 3 projects these coordinates through `viewport.worldToScreenMatrix` every frame
 *      to produce an integer or sub-pixel screen viewport `AppScreenRegion` in device pixels:
 *        p_screen = M_worldToScreen * p_world
 *
 * 3. Lifecycle & Memory Safety:
 *    - Apps are registered via `MountApp(uid, unique_ptr<IEmbeddedApp>)`.
 *    - Controlled teardown via `DismountApp(uid)` and `OnTeardown()` prevents resource leaks
 *      when objects are deleted from the active page.
 */

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <blend2d/blend2d.h>

#include "core/canvas_engine/transform/canvas_transform.hpp"

class CanvasObject;

namespace Folio {

/**
 * @struct AppScreenRegion
 * @brief Screen-pixel bounding box calculated for an active embedded widget.
 */
struct AppScreenRegion {
    float x      = 0.0f; ///< Left edge position in device screen pixels
    float y      = 0.0f; ///< Top edge position in device screen pixels
    float width  = 0.0f; ///< Width in device screen pixels
    float height = 0.0f; ///< Height in device screen pixels
};

/**
 * @class IEmbeddedApp
 * @brief Abstract interface defining lifecycle and rendering hooks for running widgets.
 */
class IEmbeddedApp {
public:
    virtual ~IEmbeddedApp() = default;

    /**
     * @brief Per-frame simulation and logic update.
     * @param[in] deltaTime Elapsed frame time in seconds.
     */
    virtual void Tick(double deltaTime) = 0;

    /**
     * @brief Renders the widget directly into the projected screen pixel bounds.
     * @param[in] targetRegion Bounding rectangle in device screen pixels.
     */
    virtual void RenderApp(const AppScreenRegion& targetRegion) = 0;

    /**
     * @brief Invoked upon app dismounting or destruction to release native handles.
     */
    virtual void OnTeardown() = 0;
};

/**
 * @class EmbeddedAppLayer
 * @brief Layer 3 Host: Tracks runtime sessions, projects world coordinates, and manages workers.
 */
class EmbeddedAppLayer {
public:
    EmbeddedAppLayer() = default;
    ~EmbeddedAppLayer();

    EmbeddedAppLayer(const EmbeddedAppLayer&) = delete;
    EmbeddedAppLayer& operator=(const EmbeddedAppLayer&) = delete;
    EmbeddedAppLayer(EmbeddedAppLayer&&) noexcept = default;
    EmbeddedAppLayer& operator=(EmbeddedAppLayer&&) noexcept = default;

    /**
     * @brief Attaches an active running application session to a canvas object UID.
     *
     * @param[in] objectUid Unique identifier of the host CanvasObject.
     * @param[in] app       Unique ownership pointer to the running application instance.
     */
    void MountApp(uint32_t objectUid, std::unique_ptr<IEmbeddedApp> app);

    /**
     * @brief Detaches and tears down a running app session for a specific object.
     *
     * @param[in] objectUid Unique identifier of the CanvasObject to dismount.
     */
    void DismountApp(uint32_t objectUid);

    /**
     * @brief Tears down and destroys all active running app sessions.
     */
    void DismountAll();

    /**
     * @brief Updates simulation ticks and renders all mounted apps matching active visible objects.
     *
     * WORKING PROCESS & MATHEMATICAL PROJECTION:
     * - Iterates through mounted apps.
     * - Resolves the associated CanvasObject from `activeObjects`.
     * - Projects the object's world millimeter bounds (worldX, worldY, worldWidth, worldHeight)
     *   through `viewport.worldToScreenMatrix` into device pixel rectangle `AppScreenRegion`.
     * - Invokes `app->Tick(deltaTime)` followed by `app->RenderApp(region)`.
     *
     * @param[in] viewport      Current camera viewport containing projection matrices.
     * @param[in] activeObjects Map of candidate active CanvasObjects on the current page.
     * @param[in] deltaTime     Elapsed frame time in seconds.
     */
    void UpdateAndRender(const Viewport& viewport,
                         const std::unordered_map<uint32_t, const CanvasObject*>& activeObjects,
                         double deltaTime);

    /**
     * @brief Checks if a specific object has an active mounted app session.
     *
     * @param[in] objectUid Unique identifier to query.
     * @return true if an app is mounted and active; false otherwise.
     */
    [[nodiscard]] bool HasActiveApp(uint32_t objectUid) const;

private:
    std::unordered_map<uint32_t, std::unique_ptr<IEmbeddedApp>> m_activeApps; ///< Active mounted app sessions
};

} // namespace Folio