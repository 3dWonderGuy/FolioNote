/**
 * =========================================================================================
 * @file core/layers/embedded_app_layer.cpp
 * @brief Implementation of Layer 3 EmbeddedAppLayer Runtime Coordinator
 * =========================================================================================
 *
 * GENERAL ARCHITECTURAL FLOW:
 * ---------------------------
 * This translation unit coordinates active interactive widgets on the canvas:
 * 1. Lifecycle Management:
 *    - Owns `std::unique_ptr<IEmbeddedApp>` instances mapped to object UIDs.
 *    - Guarantees clean resource release through `OnTeardown()` when widgets are unmounted
 *      or the layer is destroyed.
 * 2. Real-Time Spatial Projection:
 *    - Maps world-space physical millimeters to screen-space device pixels using:
 *        p_screen = M_worldToScreen * p_world
 *    - Maps top-left (worldX, worldY) and bottom-right (worldX + worldWidth, worldY + worldHeight)
 *      to calculate the exact bounding box `AppScreenRegion`.
 * 3. Execution Pass:
 *    - Dispatches per-frame simulation via `app->Tick(deltaTime)`.
 *    - Dispatches graphics rendering via `app->RenderApp(region)`.
 */

#include "core/layers/embedded_app_layer.hpp"
#include "core/objects/canvas_object.hpp"

namespace Folio {

EmbeddedAppLayer::~EmbeddedAppLayer() {
    DismountAll();
}

/**
 * @brief Mounts an active runtime widget session onto a specific canvas object UID.
 *
 * If an app was previously mounted under this UID, it is safely dismounted and torn down first.
 *
 * @param[in] objectUid Unique identifier of the host CanvasObject.
 * @param[in] app       Unique pointer to the running widget implementation.
 */
void EmbeddedAppLayer::MountApp(uint32_t objectUid, std::unique_ptr<IEmbeddedApp> app) {
    if (!app) return;
    DismountApp(objectUid);
    m_activeApps[objectUid] = std::move(app);
}

/**
 * @brief Dismounts and terminates an app session associated with a CanvasObject UID.
 *
 * @param[in] objectUid Unique identifier of the CanvasObject to dismount.
 */
void EmbeddedAppLayer::DismountApp(uint32_t objectUid) {
    auto it = m_activeApps.find(objectUid);
    if (it != m_activeApps.end()) {
        if (it->second) {
            it->second->OnTeardown();
        }
        m_activeApps.erase(it);
    }
}

/**
 * @brief Dismounts all active applications and releases all native handles.
 */
void EmbeddedAppLayer::DismountAll() {
    for (auto& [uid, app] : m_activeApps) {
        if (app) {
            app->OnTeardown();
        }
    }
    m_activeApps.clear();
}

/**
 * @brief Checks if a runtime application is currently active for the given object UID.
 *
 * @param[in] objectUid Unique identifier to query.
 * @return true if an app is mounted; false otherwise.
 */
bool EmbeddedAppLayer::HasActiveApp(uint32_t objectUid) const {
    return m_activeApps.find(objectUid) != m_activeApps.end();
}

/**
 * @brief Updates and renders all mounted apps matching visible canvas objects.
 *
 * MATHEMATICAL PROJECTION:
 *   Let M = viewport.worldToScreenMatrix:
 *     topLeftScreen     = M * (worldX, worldY)
 *     bottomRightScreen = M * (worldX + worldWidth, worldY + worldHeight)
 *     region.x      = topLeftScreen.x
 *     region.y      = topLeftScreen.y
 *     region.width  = bottomRightScreen.x - topLeftScreen.x
 *     region.height = bottomRightScreen.y - topLeftScreen.y
 *
 * @param[in] viewport      Current camera viewport with transformation matrices.
 * @param[in] activeObjects Active candidate objects on the current page.
 * @param[in] deltaTime     Elapsed frame time in seconds.
 */
void EmbeddedAppLayer::UpdateAndRender(const Viewport& viewport,
                                      const std::unordered_map<uint32_t, const CanvasObject*>& activeObjects,
                                      double deltaTime) {
    for (auto& [uid, app] : m_activeApps) {
        if (!app) continue;

        auto objIt = activeObjects.find(uid);
        if (objIt == activeObjects.end() || !objIt->second) {
            continue;
        }

        const CanvasObject* obj = objIt->second;

        // Project world mm coordinates to screen pixel region
        BLPoint topLeft = viewport.worldToScreenMatrix.map_point(BLPoint(obj->worldX, obj->worldY));
        BLPoint bottomRight = viewport.worldToScreenMatrix.map_point(
            BLPoint(obj->worldX + obj->worldWidth, obj->worldY + obj->worldHeight));

        AppScreenRegion region;
        region.x      = static_cast<float>(topLeft.x);
        region.y      = static_cast<float>(topLeft.y);
        region.width  = static_cast<float>(bottomRight.x - topLeft.x);
        region.height = static_cast<float>(bottomRight.y - topLeft.y);

        app->Tick(deltaTime);
        app->RenderApp(region);
    }
}

} // namespace Folio