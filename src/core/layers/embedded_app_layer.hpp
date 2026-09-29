#pragma once

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <blend2d/blend2d.h>

#include "core/engine/canvas_transform.hpp"

class CanvasObject;

namespace Folio {

struct AppScreenRegion {
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;
};

/**
 * @brief Base contract for an active runtime session (video decoder, web engine, live text session).
 */
class IEmbeddedApp {
public:
    virtual ~IEmbeddedApp() = default;
    virtual void Tick(double deltaTime) = 0;
    virtual void RenderApp(const AppScreenRegion& targetRegion) = 0;
    virtual void OnTeardown() = 0;
};

/**
 * @brief Layer 3 Host: Tracks active runtime sessions, projects world millimeters to screen pixels,
 * and manages worker lifecycle.
 */
class EmbeddedAppLayer {
public:
    EmbeddedAppLayer() = default;
    ~EmbeddedAppLayer();

    EmbeddedAppLayer(const EmbeddedAppLayer&) = delete;
    EmbeddedAppLayer& operator=(const EmbeddedAppLayer&) = delete;

    void MountApp(uint32_t objectUid, std::unique_ptr<IEmbeddedApp> app);
    void DismountApp(uint32_t objectUid);
    void DismountAll();

    void UpdateAndRender(const Viewport& viewport,
                         const std::unordered_map<uint32_t, const CanvasObject*>& activeObjects,
                         double deltaTime);

    [[nodiscard]] bool HasActiveApp(uint32_t objectUid) const;

private:
    std::unordered_map<uint32_t, std::unique_ptr<IEmbeddedApp>> m_activeApps;
};

} // namespace Folio