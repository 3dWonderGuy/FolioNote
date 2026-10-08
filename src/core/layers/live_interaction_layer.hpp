#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include <functional>
#include <blend2d/blend2d.h>

#include "core/canvas_engine/canvas_transform.hpp"
#include "core/spatial/aabb.hpp"
#include "core/canvas_engine/stroke_smoother.hpp"
#include "core/canvas_engine/live_layer_pipeline.hpp"

class SelectionGizmo;

namespace Folio {

using ::SelectionGizmo;

/**
 * @struct LivePoint
 * @brief Telemetry sample captured during an active in-flight pen gesture.
 */
struct LivePoint {
    double   worldX    = 0.0;  ///< Pen tip X position in physical canvas millimeters (mm)
    double   worldY    = 0.0;  ///< Pen tip Y position in physical canvas millimeters (mm)
    float    pressure  = 0.5f; ///< Stylus tip normalized pressure [0.0, 1.0]
    uint64_t timestamp = 0;    ///< Monotonic millisecond timestamp
};

/**
 * @struct ActiveStrokeData
 * @brief Non-owning borrowed view into in-flight ink stroke telemetry.
 */
struct ActiveStrokeData {
    std::span<const LivePoint> points;
    BLRgba32                   color{ 0, 0, 0, 255 };
    double                     baseWidthMm   = 0.5;
    bool                       isHighlighter = false;
};

/**
 * @struct ActiveGizmoData
 * @brief Parameters for rendering the interactive object selection & transform gizmo.
 * Rendered in screen space so handles and borders maintain pixel-crisp, zoom-independent sizes.
 */
struct ActiveGizmoData {
    AABB     targetBounds;                                    ///< Enclosing AABB in world millimeters
    BLRgba32 outlineColor{ 0x00, 0x78, 0xD4, 0xDD };          ///< Primary bounding outline color
    BLRgba32 handleFillColor{ 0xFF, 0xFF, 0xFF, 0xFF };       ///< Handle interior fill
    BLRgba32 handleBorderColor{ 0x00, 0x78, 0xD4, 0xFF };     ///< Handle outline stroke
    double   handleRadiusPx         = 5.0;                    ///< Fixed-pixel radius for resize handles
    double   borderWidthPx          = 1.5;                    ///< Fixed-pixel width of bounding frame
    bool     showScaleHandles       = true;                   ///< Whether to draw corner/edge scale handles
    bool     showRotateHandle       = true;                   ///< Whether to draw top rotation stem & circle
    double   rotationHandleOffsetPx = 24.0;                   ///< Screen distance from top edge to rotation handle
    double   rotationAngleDeg       = 0.0;                    ///< Current rotation in degrees
};

/**
 * @struct ActiveLaserData
 * @brief Transient laser pointer trail with chronological decay fading.
 */
struct ActiveLaserData {
    std::span<const LivePoint> points;
    BLRgba32                   color{ 0xFF, 0x17, 0x44, 0xFF }; ///< Vibrant laser red/glow
    double                     radiusMm      = 1.8;             ///< Laser spot radius in world mm
    uint64_t                   currentTimeMs = 0;               ///< Current engine frame clock (ms)
    uint64_t                   trailDecayMs  = 600;             ///< Milliseconds before trail fades completely
};

/**
 * @struct ActiveTextEditorData
 * @brief Transient visual state of the in-flight text editor overlay (caret & selection).
 */
struct ActiveTextEditorData {
    double                caretWorldX    = 0.0;   ///< Caret line base X (world mm)
    double                caretWorldY    = 0.0;   ///< Caret line top Y (world mm)
    double                caretHeightMm  = 5.0;   ///< Caret vertical height (world mm)
    double                caretWidthMm   = 0.4;   ///< Caret thickness (world mm)
    bool                  isCaretVisible = true;  ///< Blink state (true = drawn, false = hidden)
    BLRgba32              caretColor{ 0x00, 0x78, 0xD4, 0xFF };
    std::span<const AABB> selectionRects;         ///< Highlighted text ranges in world space
    BLRgba32              selectionFillColor{ 0x00, 0x78, 0xD4, 0x4D };
};

/**
 * @struct ActiveEraserReticleData
 * @brief Parameters for drawing the interactive eraser circular cursor and crosshair reticle in screen pixels.
 */
struct ActiveEraserReticleData {
    bool   isVisible      = false;
    float  screenX        = 0.0f;
    float  screenY        = 0.0f;
    double radiusPx       = 10.0;
    bool   isDown         = false;
    bool   isStrokeEraser = true;
};

/**
 * @struct ActiveMarqueeData
 * @brief Transient lasso or marquee selection rectangle / polygon.
 */
struct ActiveMarqueeData {
    std::span<const Point2D> polygonPoints;                                ///< World-space points for lasso
    AABB                     rectBounds;                                   ///< World-space bounds for rectangular marquee
    bool                     isLasso       = false;                        ///< True for arbitrary polygon, false for rect
    bool                     isScreenRect  = false;                        ///< True for screen-pixel rectangle
    float                    screenMinX    = 0.0f;
    float                    screenMinY    = 0.0f;
    float                    screenMaxX    = 0.0f;
    float                    screenMaxY    = 0.0f;
    BLRgba32                 fillColor{ 0x00, 0x78, 0xD4, 0x24 };
    BLRgba32                 strokeColor{ 0x00, 0x78, 0xD4, 0xDD };
    double                   strokeWidthPx = 1.5;
};

/**
 * @class LiveInteractionLayer
 * @brief Layer 2 Host: Ephemeral, non-cached rendering overlay for 120Hz+ live interactions.
 *
 * ARCHITECTURAL ROLE:
 * - Rendered directly to the screen's target BLContext every frame on top of Layer 1 raster cache.
 * - Adheres strictly to Zero-Copy Borrowing: borrows tool state via non-owning spans and views.
 * - Holds zero duplicate geometry buffers and performs NO heap allocations during runtime.
 * - Strictly separates world-space rendering (ink, laser, text) from screen-space rendering (gizmos, marquee, eraser).
 */
class LiveInteractionLayer {
public:
    LiveInteractionLayer();
    ~LiveInteractionLayer() = default;

    LiveInteractionLayer(const LiveInteractionLayer&) = delete;
    LiveInteractionLayer& operator=(const LiveInteractionLayer&) = delete;
    LiveInteractionLayer(LiveInteractionLayer&&) noexcept = default;
    LiveInteractionLayer& operator=(LiveInteractionLayer&&) noexcept = default;

    /**
     * @brief Direct pass-through rendering into the compositor frame context.
     * Sequences passes in strict back-to-front visual order:
     * 1. In-Flight Ink Stroke (World Space)
     * 2. Laser Pointer Trail (World Space with time-decay alpha)
     * 3. Selection / Transform Gizmo (Screen Space for fixed-pixel handles)
     * 4. Text Editor Caret & Selection (World Space)
     * 5. Marquee / Lasso Box (Screen / World Space)
     * 6. Eraser Circular Cursor Reticle (Screen Space)
     * 7. Custom Feedback Callback (if registered)
     *
     * @param ctx Destination Blend2D rendering context (screen target).
     * @param viewport Active camera viewport containing transformation matrices and DPI scale.
     */
    void Render(BLContext& ctx, const Viewport& viewport);

    // --- Zero-Copy Borrowing Setters (Transient per-frame bindings) ---
    void SetActiveStrokeView(const ActiveStrokeData& stroke) noexcept { m_borrowedStroke = stroke; m_hasBorrowedStroke = true; }
    void ClearActiveStrokeView() noexcept { m_hasBorrowedStroke = false; }

    void SetActiveGizmo(const ActiveGizmoData& gizmo) noexcept { m_activeGizmo = gizmo; m_hasActiveGizmo = true; }
    void ClearActiveGizmo() noexcept { m_hasActiveGizmo = false; }

    // Direct binding for application SelectionGizmo
    void BindSelectionGizmo(const SelectionGizmo* gizmo, const CanvasTransform* transform) noexcept {
        m_boundGizmo = gizmo;
        m_boundTransform = transform;
    }
    void ClearSelectionGizmo() noexcept {
        m_boundGizmo = nullptr;
        m_boundTransform = nullptr;
    }

    void SetActiveLaser(const ActiveLaserData& laser) noexcept { m_activeLaser = laser; m_hasActiveLaser = true; }
    void ClearActiveLaser() noexcept { m_hasActiveLaser = false; }

    void SetActiveTextEditor(const ActiveTextEditorData& editor) noexcept { m_activeText = editor; m_hasActiveText = true; }
    void ClearActiveTextEditor() noexcept { m_hasActiveText = false; }

    void SetActiveMarquee(const ActiveMarqueeData& marquee) noexcept { m_activeMarquee = marquee; m_hasActiveMarquee = true; }
    void ClearActiveMarquee() noexcept { m_hasActiveMarquee = false; }

    // Screen-space rectangular marquee convenience helper
    void SetScreenMarquee(float minX, float minY, float maxX, float maxY,
                          BLRgba32 fillColor = BLRgba32(0x00, 0x78, 0xD4, 0x24),
                          BLRgba32 strokeColor = BLRgba32(0x00, 0x78, 0xD4, 0xDD),
                          double strokeWidth = 1.5) noexcept {
        m_activeMarquee.isLasso = false;
        m_activeMarquee.isScreenRect = true;
        m_activeMarquee.screenMinX = minX;
        m_activeMarquee.screenMinY = minY;
        m_activeMarquee.screenMaxX = maxX;
        m_activeMarquee.screenMaxY = maxY;
        m_activeMarquee.fillColor = fillColor;
        m_activeMarquee.strokeColor = strokeColor;
        m_activeMarquee.strokeWidthPx = strokeWidth;
        m_hasActiveMarquee = true;
    }
    void ClearScreenMarquee() noexcept { m_hasActiveMarquee = false; }

    // Eraser reticle state
    void SetEraserReticle(float screenX, float screenY, double radiusPx, bool isDown, bool isStrokeEraser) noexcept {
        m_activeEraser.isVisible = true;
        m_activeEraser.screenX = screenX;
        m_activeEraser.screenY = screenY;
        m_activeEraser.radiusPx = radiusPx;
        m_activeEraser.isDown = isDown;
        m_activeEraser.isStrokeEraser = isStrokeEraser;
        m_hasActiveEraser = true;
    }
    void ClearEraserReticle() noexcept {
        m_activeEraser.isVisible = false;
        m_hasActiveEraser = false;
    }

    // --- State Queries ---
    [[nodiscard]] bool HasActiveInteraction() const noexcept;

    // --- High-Performance Modeled Vector Inking Pipeline (Google Ink & Spring Physics) ---
    /**
     * @brief Begins a modeled, physics-simulated continuous vector ink stroke.
     * Integrates Google Ink StrokeModeler, spring-mass-damper ODE solving, wobble filtering,
     * and dynamic pressure/velocity width calculation.
     */
    void BeginStroke(double worldXMm, double worldYMm, float pressure, double timeSec,
                     const PenTool& tool, float zoomScale = 1.0f, float tiltX = 0.0f, float tiltY = 0.0f);

    /**
     * @brief Ingests continuous stylus telemetry, updates spring physics, and regenerates
     * the active 2D ribbon polygon outline in real time.
     */
    void AddStrokePoint(double worldXMm, double worldYMm, float pressure, double timeSec,
                        float zoomScale = 1.0f, float tiltX = 0.0f, float tiltY = 0.0f);

    /**
     * @brief Completes the active in-flight stroke, flushes remaining spring inertia,
     * and packages the finished geometric contour into FinishedStrokeData for document commit.
     */
    [[nodiscard]] FinishedStrokeData FinishStroke();

    /**
     * @brief Aborts the active stroke immediately and discards all pending points without saving.
     */
    void CancelStroke();

    [[nodiscard]] LiveLayerPipeline& GetInkingPipeline() noexcept { return m_livePipeline; }
    [[nodiscard]] const LiveLayerPipeline& GetInkingPipeline() const noexcept { return m_livePipeline; }

    // --- Direct Ingestion API for Fallback / Raw Telemetry Views ---
    /**
     * @brief Begins a new internal in-flight stroke sample sequence.
     * @param[in] startPoint Initial stylus telemetry point (world mm, pressure, timestamp).
     */
    void BeginStroke(const LivePoint& startPoint);

    /**
     * @brief Appends a telemetry point to the in-flight stroke.
     * @param[in] point Consecutive stylus sample.
     */
    void AppendPoint(const LivePoint& point);

    /**
     * @brief Finalizes inking gesture and extracts raw recorded telemetry points.
     * Resets inking state and pre-reserves buffer capacity for zero allocation on next stroke.
     * @return Vector of recorded LivePoints.
     */
    [[nodiscard]] std::vector<LivePoint> FinalizeStroke();

    /**
     * @brief Finalizes inking gesture and converts raw points into swept 1D segments for ink creation.
     * @param[in] baseWidthMm Nominal pen width in millimeters (defaults to 0.5mm).
     * @return Vector of constructed Segment1D items with pressure-scaled widths.
     */
    [[nodiscard]] std::vector<Segment1D> FinalizeStrokeAsSegments(float baseWidthMm = 0.5f);

    /**
     * @brief Sets pen styling for internal inking buffer.
     * @param[in] argbColor 32-bit ARGB packed color.
     * @param[in] widthMm   Base line thickness in world millimeters.
     */
    void SetPenStyle(uint32_t argbColor, double widthMm) noexcept;

    /**
     * @brief Accesses current uncommitted in-flight stroke samples.
     * @return Const reference to internal telemetry points.
     */
    [[nodiscard]] const std::vector<LivePoint>& GetPoints() const noexcept { return m_internalPoints; }

    /**
     * @brief Begins an interactive freehand lasso selection contour.
     * @param[in] worldXMm Initial point world X in mm.
     * @param[in] worldYMm Initial point world Y in mm.
     */
    void BeginLasso(double worldXMm, double worldYMm);

    /**
     * @brief Appends a vertex to the active lasso selection polygon.
     * Filters micro-movements (<0.5mm) to constrain polygon node complexity.
     * @param[in] worldXMm Next point world X in mm.
     * @param[in] worldYMm Next point world Y in mm.
     */
    void AddLassoPoint(double worldXMm, double worldYMm);

    /**
     * @brief Finalizes the lasso gesture and returns the completed polygon contour.
     * @return Vector of polygon vertices in world millimeters.
     */
    [[nodiscard]] std::vector<Point2D> FinishLasso();

    /**
     * @brief Accesses active lasso polygon vertices.
     * @return Const reference to internal lasso points.
     */
    [[nodiscard]] const std::vector<Point2D>& GetLassoPoints() const noexcept { return m_internalLassoPoints; }

    /**
     * @brief Resets and clears all transient state bindings (strokes, gizmos, marquees, eraser, custom hooks).
     */
    void Clear() noexcept;

    /**
     * @brief Registers an optional custom rendering callback executed during Pass 7.
     * Used for developer debug visualizers (e.g. Rnote-style AABB debugger) and snapping guides.
     * @param[in] renderer Rendering lambda receiving the screen context and active camera viewport.
     */
    void SetCustomFeedbackRenderer(std::function<void(BLContext&, const Viewport&)> renderer) {
        m_customRenderer = std::move(renderer);
    }

private:
    void RenderInFlightInk(BLContext& ctx, const Viewport& viewport);
    void RenderLaserPointer(BLContext& ctx, const Viewport& viewport);
    void RenderGizmo(BLContext& ctx, const Viewport& viewport);
    void RenderTextEditor(BLContext& ctx, const Viewport& viewport);
    void RenderMarquee(BLContext& ctx, const Viewport& viewport);
    void RenderEraser(BLContext& ctx);

    // Borrowed Tool Payloads
    ActiveStrokeData     m_borrowedStroke{};
    ActiveGizmoData      m_activeGizmo{};
    ActiveLaserData      m_activeLaser{};
    ActiveTextEditorData m_activeText{};
    ActiveMarqueeData    m_activeMarquee{};

    bool m_hasBorrowedStroke = false;
    bool m_hasActiveGizmo    = false;
    bool m_hasActiveLaser    = false;
    bool m_hasActiveText     = false;
    bool m_hasActiveMarquee  = false;

    // Bound External Tools & Reticle
    const SelectionGizmo*  m_boundGizmo      = nullptr;
    const CanvasTransform* m_boundTransform  = nullptr;
    ActiveEraserReticleData m_activeEraser{};
    bool                   m_hasActiveEraser = false;

    // Preallocated internal buffers for direct ingestion (zero runtime allocation)
    std::vector<LivePoint> m_internalPoints;
    uint32_t               m_penColor   = 0xFF000000;
    double                 m_penWidthMm = 0.5;
    bool                   m_isInking   = false;

    std::vector<Point2D>   m_internalLassoPoints;
    bool                   m_isLassoing = false;

    // High-performance physics-modeled inking pipeline
    LiveLayerPipeline      m_livePipeline;

    std::function<void(BLContext&, const Viewport&)> m_customRenderer;
};

} // namespace Folio