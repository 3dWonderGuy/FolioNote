#pragma once

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include <blend2d/blend2d.h>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <SDL3/SDL_dialog.h>

#include "core/objects/canvas_object.hpp"
#include "core/objects/text/text_editor_state.hpp"
#include "core/overlay/interactive_overlay_host.hpp"
#include "core/storage/pdf_storage.hpp"
#include "core/canvas_engine/transform/canvas_transform.hpp"
#include "core/layers/layer_compositor_manager.hpp"
#include "core/actions/action_scheduler.hpp"
#include "core/canvas_engine/gizmo/selection_gizmo.hpp"
#include "core/objects/primitives/shape_types.hpp"
#include "core/objects/connectors/connector_types.hpp"
#include "core/ink_engine/ink_engine.hpp"
#include "core/canvas_engine/tools/ruler_tool.hpp"

// Forward declarations
class DocumentSession;
struct SDL_Window;

namespace Folio {
    class ShapeObject;
    class SmartArrowObject;
    class TextBoxObject;
}

/**
 * @struct PageTemplateDefaults
 * @brief Default paper styling, encapsulated border configuration, and spatial dimensions for new pages.
 *
 * Theme-driven colors (such as dark/inverted paper and grid lines) are managed globally via
 * ThemeManager / ObjectConfig. Templates specify structural paper style and encapsulated border metrics.
 */
struct PageTemplateDefaults {
    PaperSettings paper;                                          ///< Encapsulated paper style, grid spacing, and colors
    PageBorderSettings border;                                    ///< Encapsulated border configuration

    // Compatibility aliases for legacy direct access
    PaperStyle& paperStyle = paper.style;
    double& gridSpacingMm = paper.gridSpacingMm;
    BLRgba32& paperColor = paper.paperColor;
    BLRgba32& gridColor  = paper.gridColor;

    // Compatibility aliases for legacy direct access
    bool& showBorder = border.isVisible;
    BLRgba32& borderColor = border.color;
    double& borderWidth = border.width;
    PageBorderType& borderType = border.type;
    PageBorderStyle& borderStyle = border.style;

    PageSizeFormat pageSizeFormat = PageSizeFormat::Letter;
    bool pageIsLandscape = false;
    CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity;
    double calibrationDpi = 96.0;
};

class CanvasEngine {
public:
    CanvasTransform transform;
    Folio::LayerCompositorManager layerCompositor;
    Folio::ActionManager actionManager;
    SelectionGizmo selectionGizmo;
    Folio::TextEditorState textEditor;
    Folio::InteractiveOverlayHost interactiveOverlayHost;
    Folio::RulerTool ruler;                                       ///< Interactive digital straightedge ruler
    [[nodiscard]] Folio::InkEngine& GetInkEngine() noexcept {
        return layerCompositor.GetLiveInteractionLayer().GetInkEngine();
    }
    [[nodiscard]] const Folio::InkEngine& GetInkEngine() const noexcept {
        return layerCompositor.GetLiveInteractionLayer().GetInkEngine();
    }

    // -------------------------------------------------------------------------
    // DEFAULT TYPOGRAPHY SETTINGS (Basic Text Ribbon Group & Click-to-Type)
    // -------------------------------------------------------------------------
    std::string defaultTextFontFamily = "Segoe UI";
    float defaultTextFontSize = 14.0f;
    bool defaultTextBold = false;
    bool defaultTextItalic = false;
    bool defaultTextUnderline = false;
    bool defaultTextStrikethrough = false;
    BLRgba32 defaultTextColor{0x1F, 0x29, 0x37, 0xFF};
    BLRgba32 defaultTextHighlightColor{0x00, 0x00, 0x00, 0x00};
    uint8_t defaultTextAlignment = 0; // 0: Left, 1: Center, 2: Right

    // Ephemeral Presentation Ink / Laser Pointer (delegated to LiveInteractionLayer's inkEngine)
    using EphemeralStroke = Folio::EphemeralStroke;
    [[nodiscard]] const std::vector<EphemeralStroke>& GetEphemeralStrokes() const noexcept {
        return layerCompositor.GetLiveInteractionLayer().GetInkEngine().GetEphemeralStrokes();
    }
    void AddEphemeralStroke(BLPath path, BLRgba32 color, uint32_t durationMs = 2500);
    void ClearEphemeralStrokes();

    // Tracking state
    Point2D lastInkingWorldMm{0.0, 0.0};
    bool isCurrentlyInking = false;

    char pageTitle[128] = "New Untitled";
    std::string pageDateStr = "Tuesday, August 18, 2026";
    std::string pageTimeStr = "9:54 PM";
    // Encapsulated paper style, grid intervals, and coloring
    PaperSettings paperSettings;

    // Compatibility aliases for direct member access
    PaperStyle& currentPaperStyle = paperSettings.style;
    double& gridSpacingMm = paperSettings.gridSpacingMm;
    BLRgba32& canvasBgColor = paperSettings.paperColor;
    BLRgba32& gridLineColor = paperSettings.gridColor;

    // Encapsulated page border settings
    PageBorderSettings pageBorder;

    // Compatibility aliases for direct member access
    bool& showPageBorder = pageBorder.isVisible;
    BLRgba32& pageBorderColor = pageBorder.color;
    double& pageBorderWidth = pageBorder.width;
    PageBorderType& pageBorderType = pageBorder.type;
    PageBorderStyle& pageBorderStyle = pageBorder.style;

    PageSizeFormat pageSizeFormat = PageSizeFormat::Letter;
    bool pageIsLandscape = false;
    double customPageWidthMm = 215.9;
    double customPageHeightMm = 279.4;

    // Content extents tracking (for Automatic border calculation)
    double contentMaxXMm = 0.0;
    double contentMaxYMm = 0.0;

    // Canvas Infinity Mode: SemiInfinity, FullInfinity, VerticalScroll, HorizontalScroll
    CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity;

    // Dev Mode (Rnote-style AABB & Collision Debugger)
    bool devMode = false;
    bool debugShowObjectAABB = true;
    bool debugShowSegmentAABB = true;
    bool debugShowQueryAABB = true;
    bool debugShowLabels = true;

    struct DebugCollisionInfo {
        bool active = false;
        Point2D queryCenter{0.0, 0.0};
        double queryRadius = 0.0;
        AABB queryBox{0.0, 0.0, 0.0, 0.0};
        std::vector<uint32_t> candidateUids;
        std::vector<uint32_t> hitUids;
    } debugCollision;

    struct EraserVisualState {
        bool isVisible = false;
        float screenX = 0.0f;
        float screenY = 0.0f;
        double radiusMm = 3.0;
        bool isDown = false;
        bool isStrokeEraser = true;
    } eraserVisual;

    void SetEraserCursor(float screenX, float screenY, double radiusMm, bool isDown, bool isStroke);
    void HideEraserCursor();

    void SetInfinityMode(CanvasInfinityMode mode);
    void GetStandardPageDimensionsMm(double& outW, double& outH) const;
    void GetCalculatedPageBoundsMm(double& outW, double& outH) const;

    // Page Template Defaults (applied to every newly created page)
    PageTemplateDefaults defaultTemplate;
    void ApplyDefaultTemplate();

    bool isDirty = true;
    bool needsFullRebake = true;      // Background grid + all objects

    // Canvas-level ink color invert and dark mode state
    bool isDarkMode = false;
    bool inkColorInverted = false;

    void SetDarkMode(bool dark) noexcept;
    void SetInkColorInverted(bool inverted) noexcept;
    void UpdateThemeColors();

    BLImage compositeSurface;
    GLuint glTexture = 0;
    int viewportW = 0;
    int viewportH = 0;
    SDL_Window* sdlWindow = nullptr;

    // Dynamic capacity tracking to prevent repeated buffer allocations during resize
    int allocatedCapacityW = 0;
    int allocatedCapacityH = 0;

    void Init(int initialW, int initialH, float displayDpi = 96.0f);
    void Resize(int width, int height);
    void SetDPI(float dpi) noexcept;
    void Pan(double screenDx, double screenDy) noexcept;
    void ZoomAt(double screenX, double screenY, double factor) noexcept;
    [[nodiscard]] Viewport GetViewport() const noexcept;
    void HomeOrCenterPage() noexcept;

    /**
     * @brief Instantly marks the active canvas layer dirty for full viewport re-bake.
     * Fires immediately into LayerCompositorManager so the layer is never skipped.
     */
    void InvalidateLayer() noexcept;

    /**
     * @brief Instantly marks a localized sub-region dirty on the baked canvas layer.
     */
    void InvalidateLayerRect(const AABB& dirtyBounds) noexcept;

    /**
     * @brief Constructs an ActionContext tying together CanvasEngine, LayerManager, and Session.
     */
    Folio::ActionContext CreateActionContext(DocumentSession* session, double frameTimeSec = 0.0) noexcept;

    /**
     * @brief Advances temporal canvas actions and synchronizes layer state for the frame.
     */
    void Update(double dt, DocumentSession* session);

    // -------------------------------------------------------------
    // LIVE INGESTION HOOKS (Screen Px -> World mm)
    // -------------------------------------------------------------
    void OnPointerDown(float screenX, float screenY, float pressure, double timeSec, const PenTool& tool, float tiltX = 0.0f, float tiltY = 0.0f);
    void OnPointerMove(float screenX, float screenY, float pressure, double timeSec, float tiltX = 0.0f, float tiltY = 0.0f);
    void OnPointerUp(DocumentSession& session, const PenTool& tool);

    void OnLassoDown(float screenX, float screenY);
    void OnLassoMove(float screenX, float screenY);
    std::vector<Point2D> OnLassoUp(DocumentSession* session = nullptr);

    struct MarqueeBoxState {
        bool isActive = false;
        Point2D startWorld{0.0, 0.0};
        Point2D currentWorld{0.0, 0.0};
        float startScreenX = 0.0f;
        float startScreenY = 0.0f;
        float currentScreenX = 0.0f;
        float currentScreenY = 0.0f;
    } marqueeBox;

    void OnBoxSelectDown(float screenX, float screenY);
    void OnBoxSelectMove(float screenX, float screenY);
    void OnBoxSelectUp(DocumentSession* session = nullptr);

    enum class SelectionMode {
        Box,
        Lasso
    };
    SelectionMode selectionMode = SelectionMode::Box;

    void ClearSelection(DocumentSession* session = nullptr);
    void OnActivePageChanged();
    bool DeleteSelectedObjects(DocumentSession* session = nullptr);
    size_t SelectAll(DocumentSession* session = nullptr);

    struct ShapeCreationState {
        bool isActive = false;
        bool isDragging = false;
        Folio::ShapeType shapeType = Folio::ShapeType::Rectangle;
        bool lockDrawingMode = false;
        bool lockToGrid = false;
        int polygonSides = 6;

        int ellipseStep = 0;
        Point2D ellipseCenter{0.0, 0.0};
        Point2D ellipseMajorPoint{0.0, 0.0};
        double ellipseMajorRadius = 0.0;
        double ellipseMinorRadius = 0.0;

        Point2D startWorld{0.0, 0.0};
        Point2D currentWorld{0.0, 0.0};
        float startScreenX = 0.0f;
        float startScreenY = 0.0f;
        float currentScreenX = 0.0f;
        float currentScreenY = 0.0f;

        Folio::ShapeFillType defaultFillType = Folio::ShapeFillType::None;
        BLRgba32 defaultFillColor = BLRgba32(0x00, 0x78, 0xD4, 0x40);
        Folio::ShapeOutlineType defaultOutlineType = Folio::ShapeOutlineType::Solid;
        BLRgba32 defaultOutlineColor = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
        double defaultStrokeWidth = 1.0;
        Folio::ArrowHeadType defaultEndArrow = Folio::ArrowHeadType::Triangle;
        bool isSnapped = false;
        Point2D snapAnchorPoint{0.0, 0.0};
        Folio::ConnectorStyle defaultConnectorStyle = Folio::ConnectorStyle::Straight;
    } shapeCreation;

    Point2D SnapToGridIfNeeded(const Point2D& pt) const;
    void StartShapeCreation(Folio::ShapeType type, bool lockMode = false, DocumentSession* session = nullptr);
    void CancelShapeCreation();
    void OnShapeDrawDown(float screenX, float screenY, DocumentSession* session = nullptr);
    void OnShapeDrawMove(float screenX, float screenY, DocumentSession* session = nullptr);
    std::shared_ptr<CanvasObject> OnShapeDrawUp(DocumentSession* session);

    std::shared_ptr<Folio::ShapeObject> GetSelectedShape(DocumentSession* session) const;
    std::shared_ptr<Folio::SmartArrowObject> GetSelectedConnector(DocumentSession* session) const;
    std::shared_ptr<Folio::TextBoxObject> GetSelectedTextBox(DocumentSession* session) const;
    std::shared_ptr<Folio::TextBoxObject> InsertTextBox(DocumentSession* session, double worldX = 0.0, double worldY = 0.0);
    std::shared_ptr<Folio::ShapeObject> InsertShape(Folio::ShapeType type, DocumentSession* session, double worldX = 0.0, double worldY = 0.0);

    void SyncSelectionToSpatialIndex(DocumentSession* session);

    static std::string DeduplicateAndSaveImage(const std::string& srcPath, const void* data, size_t size, DocumentSession* session, const std::string& ext = ".png");

    struct ImageFileDialogContext {
        CanvasEngine* canvas = nullptr;
        DocumentSession* session = nullptr;
        Point2D insertPosWorld{0.0, 0.0};
    };

    static void SDLCALL OnImageFileSelected(void* userdata, const char* const* filelist, int filter);
    void OpenImageFileDialog(SDL_Window* parentWin, DocumentSession* session);

    bool m_attachModalOpen = false;
    std::string m_pendingAttachPath;
    std::string m_pendingAttachName;
    DocumentSession* m_pendingAttachSession = nullptr;

    struct AttachmentFileDialogContext {
        CanvasEngine* canvas   = nullptr;
        DocumentSession* session = nullptr;
    };

    static void SDLCALL OnAttachmentFileSelected(void* userdata, const char* const* filelist, int filter);
    void OpenAttachmentFileDialog(SDL_Window* parentWin, DocumentSession* session);
    void CommitAttachment(bool embed, DocumentSession* session);

    struct PdfFileDialogContext {
        CanvasEngine* canvas = nullptr;
        DocumentSession* session = nullptr;
        Point2D insertPosWorld{0.0, 0.0};
        bool asBackground = false;
        Folio::PdfImportMode importMode = Folio::PdfImportMode::LocalCopy;
    };

    std::function<void(const std::string& filePath, DocumentSession* session)> onPdfImportRequested = nullptr;
    static void SDLCALL OnPdfFileSelected(void* userdata, const char* const* filelist, int filter);
    void OpenPdfFileDialog(SDL_Window* parentWin, DocumentSession* session, bool asBackground = false, Folio::PdfImportMode mode = Folio::PdfImportMode::LocalCopy);

    bool InsertImageFromClipboard(DocumentSession* session);
    bool InsertVideoFromFile(const std::string& filePath, DocumentSession* session);
    bool InsertWebEmbed(const std::string& url, const std::string& label, DocumentSession* session);
    bool InsertVideoFromUrl(const std::string& url, const std::string& label, DocumentSession* session);

    struct VideoFileDialogContext {
        CanvasEngine* canvas   = nullptr;
        DocumentSession* session = nullptr;
    };

    static void SDLCALL OnVideoFileSelected(void* userdata, const char* const* filelist, int filter);
    void OpenVideoFileDialog(SDL_Window* parentWin, DocumentSession* session);

    bool InsertAudioFromFile(const std::string& filePath, DocumentSession* session);

    struct AudioFileDialogContext {
        CanvasEngine* canvas   = nullptr;
        DocumentSession* session = nullptr;
    };

    static void SDLCALL OnAudioFileSelected(void* userdata, const char* const* filelist, int filter);
    void OpenAudioFileDialog(SDL_Window* parentWin, DocumentSession* session);

    bool EraseSegment(float screenX0, float screenY0, float screenX1, float screenY1,
                      double radiusMm, DocumentSession& session, bool isStrokeEraser = true);
    bool EraseAt(float screenX, float screenY, double radiusMm, DocumentSession& session, bool isStrokeEraser = true);

    void Render(const std::vector<std::shared_ptr<CanvasObject>>& visibleBakedObjects, DocumentSession* session = nullptr, double deltaTime = 1.0 / 60.0);

private:
    void RenderDevModeAABBs(BLContext& ctx, const std::vector<std::shared_ptr<CanvasObject>>& visibleObjects, const CanvasTransform& tr);
};
