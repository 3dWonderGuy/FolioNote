#pragma once

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include <blend2d/blend2d.h>
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container/ink_container.hpp"
#include "core/objects/media/images/image_container.hpp"
#include "core/objects/media/images/image_decoder.hpp"
#include "core/objects/media/audio/audio_container.hpp"
#include "core/objects/primitives/shape_container.hpp"
#include "core/objects/pdf_container.hpp"
#include "core/objects/connectors/smart_arrow_container.hpp"
#include "core/objects/attachment_container/attachment_container.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/objects/text/text_editor_state.hpp"
#include "core/overlay/interactive_overlay_host.hpp"
#include "core/overlay/web_overlay.hpp"
#include "core/storage/pdf_storage.hpp"
#include "core/canvas_engine/canvas_transform.hpp"
#include "core/layers/layer_compositor_manager.hpp"
#include "core/actions/action_scheduler.hpp"
#include "core/canvas_engine/selection_gizmo.hpp"
#include "core/document/document_session.hpp"
#include "core/history/canvas_command.hpp"
#include "utils/usage_tracker.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"
#include "utils/uid_generator.hpp"
#include "utils/guid_generator.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <chrono>
#include <SDL3/SDL_dialog.h>

struct PageTemplateDefaults {
    PaperStyle paperStyle = PaperStyle::Grid;
    double gridSpacingMm = 5.0;
    BLRgba32 normalBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
    BLRgba32 invertedBgColor = BLRgba32(0x1E, 0x20, 0x26);
    BLRgba32 normalLineColor = BLRgba32(0xEB, 0xEE, 0xF2);
    BLRgba32 invertedLineColor = BLRgba32(0x34, 0x38, 0x44);
    bool showBorder = false;
    BLRgba32 borderColor = BLRgba32(0xD0, 0xD4, 0xDC);
    double borderWidth = 1.5;
    PageBorderType borderType = PageBorderType::Automatic;
    PageBorderStyle borderStyle = PageBorderStyle::Continuous;
    PageSizeFormat pageSizeFormat = PageSizeFormat::Letter;
    bool pageIsLandscape = false;
    CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity;
    double calibrationDpi = 96.0;
};

/**
 * @struct EphemeralStroke
 * @brief Represents a transient presentation stroke (e.g. Laser Pointer) that decays quadratically over time.
 */
struct EphemeralStroke {
    BLPath outlinePath;
    BLRgba32 baseColor;
    uint64_t startTimeMs = 0;
    uint32_t durationMs = 2500;
};

class CanvasEngine {
public:
    CanvasTransform transform;
    Folio::LayerCompositorManager layerCompositor;
    Folio::ActionManager actionManager;
    SelectionGizmo selectionGizmo;
    Folio::TextEditorState textEditor;
    Folio::InteractiveOverlayHost interactiveOverlayHost;

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

    // Ephemeral Presentation Ink / Laser Pointer storage
    std::vector<EphemeralStroke> ephemeralStrokes;

    void AddEphemeralStroke(BLPath path, BLRgba32 color, uint32_t durationMs = 2500);
    void ClearEphemeralStrokes();

    // Tracking state
    Point2D lastInkingWorldMm{0.0, 0.0};
    bool isCurrentlyInking = false;

    char pageTitle[128] = "New Untitled";
    std::string pageDateStr = "Tuesday, August 18, 2026";
    std::string pageTimeStr = "9:54 PM";
    PaperStyle currentPaperStyle = PaperStyle::Grid;

    // Grid spacing standard: 5.0 mm rule
    double gridSpacingMm = 5.0;

    // Theme-driven canvas paper colors (defaults to light mode paper)
    BLRgba32 canvasBgColor = BLRgba32(0xFF, 0xFF, 0xFF);
    BLRgba32 gridLineColor = BLRgba32(0xEB, 0xEE, 0xF2);

    // Page Border settings
    bool showPageBorder = false;
    BLRgba32 pageBorderColor = BLRgba32(0xD0, 0xD4, 0xDC);
    double pageBorderWidth = 1.5;
    PageBorderType pageBorderType = PageBorderType::Automatic;
    PageBorderStyle pageBorderStyle = PageBorderStyle::Continuous;
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

    // Canvas-level ink color invert (dark mode trick: keeps ink readable without changing presets)
    bool inkColorInverted = false;

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
