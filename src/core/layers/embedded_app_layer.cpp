#include "core/layers/embedded_app_layer.hpp"
#include "core/objects/canvas_object.hpp"

namespace Folio {

EmbeddedAppLayer::~EmbeddedAppLayer() {
    DismountAll();
}

void EmbeddedAppLayer::MountApp(uint32_t objectUid, std::unique_ptr<IEmbeddedApp> app) {
    if (!app) return;
    DismountApp(objectUid);
    m_activeApps[objectUid] = std::move(app);
}

void EmbeddedAppLayer::DismountApp(uint32_t objectUid) {
    auto it = m_activeApps.find(objectUid);
    if (it != m_activeApps.end()) {
        if (it->second) {
            it->second->OnTeardown();
        }
        m_activeApps.erase(it);
    }
}

void EmbeddedAppLayer::DismountAll() {
    for (auto& [uid, app] : m_activeApps) {
        if (app) {
            app->OnTeardown();
        }
    }
    m_activeApps.clear();
}

bool EmbeddedAppLayer::HasActiveApp(uint32_t objectUid) const {
    return m_activeApps.find(objectUid) != m_activeApps.end();
}

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
        BLPoint bottomRight = viewport.worldToScreenMatrix.map_point(BLPoint(obj->worldX + obj->worldWidth, obj->worldY + obj->worldHeight));

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