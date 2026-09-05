#pragma once
#include <memory>
#include <vector>
#include <string>
#include "core/document/workspace.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container.hpp"
#include "core/objects/image_object.hpp"
#include "core/objects/text_box.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "core/engine/live_layer_pipeline.hpp"
#include "input/pen_palette.hpp"
#include "utils/uid_generator.hpp"

/**
 * =========================================================================================
 * @file document_session.hpp
 * @brief Top-level document controller and input/render facade for the active session.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * - DocumentSession is the central coordinator between runtime engine systems
 *   (InputManager, CanvasEngine, PenPalette) and the underlying document model (Workspace).
 * - It simplifies interacting with the document by eliminating boilerplate:
 *     1. Handles the stroke completion workflow: converts raw/finished stylus stroke data
 *        into persistent InkContainer canvas entities and commits them to the active page.
 *     2. Provides convenience helpers to insert Images, TextBoxes, and other CanvasObjects.
 *     3. Exposes camera viewport spatial queries (`QueryVisible`) directly to the render pipeline.
 *
 * MULTI-MEDIA OBJECT SUPPORT:
 * - Ink Strokes: Variable-width Bézier smoothed strokes and highlighters via `CommitStroke`.
 * - Images: Raster bitmaps (PNG, JPEG, WebP, external file references) via `AddImage` / `AddObject`.
 * - Text Boxes: Editable formatted text containers via `AddTextBox` / `AddObject`.
 * - PDFs / Shapes: Future PDF pages and geometry primitives derive from CanvasObject and can be
 *   added directly via `AddObject`.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Active Page Switching Events / Callbacks: Notify UI and renderer when switching pages.
 * - Undo/Redo Proxy Methods: Expose `Undo()` and `Redo()` delegates directly on DocumentSession.
 * - Multi-Object Selection & Transformation: Move, scale, and rotate selected canvas objects.
 * - PDF Import Pipeline: Multi-page PDF import converting pages into background CanvasObjects.
 */
class DocumentSession {
public:
    Workspace workspace; ///< Owns the notebook collection and PageRepository persistence

    // -------------------------------------------------------------------------
    // Session Initialization
    // -------------------------------------------------------------------------

    /**
     * @brief Initializes the document session by scanning or creating the workspace directory.
     * @param workspaceDirectory Path to directory containing .notebook packages (e.g. "FolioNote").
     */
    void Init(const std::string& workspaceDirectory) {
        workspace.LoadWorkspace(workspaceDirectory);
    }

    // -------------------------------------------------------------------------
    // Active Page Access
    // -------------------------------------------------------------------------

    /**
     * @brief Resolves the currently active CanvasPage from the workspace hierarchy.
     * Triggers on-demand lazy loading from SQLite if the page is not in RAM.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> GetActivePage() const {
        return workspace.GetActivePage();
    }

    // -------------------------------------------------------------------------
    // Ink Stroke Commit Workflow
    // -------------------------------------------------------------------------

    /**
     * @brief Commits finished stroke data (already containing pre-computed outline geometry).
     * @param data Finished stroke outline path and live segment records.
     * @param tool Active pen tool settings (color, base size, highlighter mode).
     */
    void CommitStroke(FinishedStrokeData&& data, const PenTool& tool) {
        auto activePage = GetActivePage();
        if (!activePage || (data.outlinePath.is_empty() && data.liveSegments.empty())) return;

        auto container = std::make_shared<InkContainer>();
        container->uid = UIDGenerator::Next();
        container->isHighlighter = (tool.penType == PenType::Highlighter);
        
        Stroke stroke;
        stroke.outlinePath = std::move(data.outlinePath);
        stroke.segments = std::move(data.liveSegments);
        stroke.color = tool.color;
        stroke.baseWidth = tool.baseSize;
        container->AddStroke(stroke);

        activePage->AddObject(container);
    }

    /**
     * @brief Commits raw 1D stroke segments by computing polygon outline hulls on the fly.
     * @param segments Vector of 1D interpolated segments with pressure/width values.
     * @param tool Active pen tool settings (color, base size, cap type).
     */
    void CommitStroke(std::vector<Segment1D>&& segments, const PenTool& tool) {
        auto activePage = GetActivePage();
        if (!activePage || segments.empty()) return;

        auto container = std::make_shared<InkContainer>();
        container->uid = UIDGenerator::Next();
        container->isHighlighter = (tool.penType == PenType::Highlighter);
        
        Stroke stroke;
        std::vector<StrokeOutlineBuilder::InputPoint> pts;
        pts.reserve(segments.size() + 1);
        pts.push_back({ segments[0].p0.x, segments[0].p0.y, segments[0].width });
        for (const auto& s : segments) {
            pts.push_back({ s.p1.x, s.p1.y, s.width });
        }
        stroke.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, tool.capType);
        stroke.segments = std::move(segments);
        stroke.color = tool.color;
        stroke.baseWidth = tool.baseSize;
        container->AddStroke(stroke);

        activePage->AddObject(container);
    }

    // -------------------------------------------------------------------------
    // Polymorphic Object Management (Images, Text Boxes, Generic Objects)
    // -------------------------------------------------------------------------

    /**
     * @brief Adds any CanvasObject (Image, TextBox, PDF, Ink) to the active page.
     * Automatically assigns a runtime UID if unassigned and updates the page R-Tree.
     */
    void AddObject(const std::shared_ptr<CanvasObject>& obj) {
        auto activePage = GetActivePage();
        if (!activePage || !obj) return;

        if (obj->uid == 0) {
            obj->uid = UIDGenerator::Next();
        }
        activePage->AddObject(obj);
    }

    /**
     * @brief Convenience helper to add an ImageObject to the active page.
     */
    void AddImage(const std::shared_ptr<Folio::ImageObject>& img) {
        AddObject(img);
    }

    /**
     * @brief Convenience helper to add a TextBoxObject to the active page.
     */
    void AddTextBox(const std::shared_ptr<Folio::TextBoxObject>& textBox) {
        AddObject(textBox);
    }

    // -------------------------------------------------------------------------
    // Viewport Spatial Query
    // -------------------------------------------------------------------------

    /**
     * @brief Queries all objects on the active page that intersect the camera viewport.
     * @param viewport Current camera viewport (frustum bounds and zoom).
     * @return Vector of visible canvas objects to be rendered by Blend2D.
     */
    [[nodiscard]] std::vector<std::shared_ptr<CanvasObject>> QueryVisible(const Viewport& viewport) const {
        auto activePage = GetActivePage();
        if (!activePage) return {};
        return activePage->QueryVisible(viewport);
    }
};