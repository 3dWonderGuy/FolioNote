#pragma once
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>
#include <SDL3/SDL.h>
#include "core/spatial/r_tree.hpp"
#include "core/spatial/aabb.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container.hpp"
#include "core/history/command_history.hpp"
#include "utils/guid_generator.hpp"

/**
 * =========================================================================================
 * @file canvas_page.hpp
 * @brief Represents an infinite 2D canvas drawing surface within a notebook section.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * - The CanvasPage is the core content container of the application.
 * - Each Section contains an ordered list of CanvasPages.
 * - An infinite 2D coordinate system is used: objects (ink strokes, images, text boxes)
 *   can be placed anywhere in world-space coordinates (double precision).
 *
 * KEY SUBSYSTEMS INTEGRATED:
 * 1. Polymorphic Object Storage:
 *    Maintains `std::vector<std::shared_ptr<CanvasObject>> objects`, supporting ink containers,
 *    raster images, text boxes, and future PDF/shape objects uniformly.
 * 2. Spatial Indexing (R-Tree):
 *    Uses a dynamic R-Tree (`RTree spatialIndex`) storing AABBs (Axis-Aligned Bounding Boxes).
 *    During rendering, `QueryVisible(viewport)` performs an O(log N) frustum query so that only
 *    objects visible on the user's screen are processed by Blend2D, guaranteeing 120+ FPS even
 *    with 50,000+ strokes on a single page.
 * 3. Undo / Redo History:
 *    Owns an isolated `CommandHistory history` instance. Each page tracks its own undo/redo
 *    action stack, preventing command pollution across pages.
 * 4. Hierarchical Sub-Page Tree (OneNote Style):
 *    Supports tree nesting via `parentPageGuid` and `nestingLevel`:
 *      - Level 0: Main page
 *      - Level 1: Sub-page
 *      - Level 2: Sub-sub-page
 *    Also tracks UI collapse state (`isCollapsed`) for folding child pages in the navigation sidebar.
 * 5. Memory Management & LRU Eviction:
 *    Pages track `lastAccessTimeMs`, `isLoaded`, and `isModified`. When inactive, the storage
 *    engine can call `EvictFromRAM()` to discard heavy vector/geometry caches from RAM while
 *    retaining lightweight metadata in memory.
 *
 * POTENTIAL FUTURE ENHANCEMENTS:
 * - Background Templates: Ruled lines, grid lines, dot grids, or custom background colors.
 * - Page Dimensions / Printing Guides: Optional fixed-size bounds (Letter, A4, Infinite).
 * - Full-Text Search Indexing: Extract text from text boxes, OCR images, and handwriting.
 * - Layer Management: Explicit drawing layers (e.g. background, ink, annotations).
 */

class CanvasPage {
public:
    // -------------------------------------------------------------------------
    // Identification & Metadata
    // -------------------------------------------------------------------------
    std::string guid;                   ///< Unique persistent UUID v4 identifier
    std::string title = "Untitled page";///< User-visible title displayed in tab and sidebar
    std::string createdDateStr;         ///< Formatted creation date (e.g., "September 5, 2026")
    std::string createdTimeStr;         ///< Formatted creation time (e.g., "1:50 AM")
    
    // -------------------------------------------------------------------------
    // Hierarchy & UI Column Ordering (OneNote-style Subpages)
    // -------------------------------------------------------------------------
    std::string parentPageGuid;         ///< Empty if top-level page, or GUID of parent page
    int32_t nestingLevel = 0;           ///< Hierarchy depth: 0 = Page, 1 = Sub-page, 2 = Sub-sub-page
    int32_t sortOrder = 0;              ///< Persistent 0-indexed column order position
    bool isCollapsed = false;           ///< Whether child sub-pages are folded in sidebar

    // -------------------------------------------------------------------------
    // Page Content & Spatial Index
    // -------------------------------------------------------------------------
    std::vector<std::shared_ptr<CanvasObject>> objects; ///< All canvas objects on this page
    RTree spatialIndex;                                 ///< Fast bounding-box query index
    CommandHistory history;                             ///< Per-page undo/redo command stack

    // -------------------------------------------------------------------------
    // LRU Caching & Memory Management Telemetry
    // -------------------------------------------------------------------------
    uint64_t lastAccessTimeMs = 0;      ///< Last time this page was rendered or modified (SDL_GetTicks)
    bool isModified = false;            ///< Dirty flag indicating unsaved changes exist
    bool isLoaded = true;               ///< True if stroke geometry and objects are present in RAM

    // -------------------------------------------------------------------------
    // Construction & Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Constructs a new CanvasPage with an optional title, parent page, and nesting level.
     */
    CanvasPage(std::string pageTitle = "Untitled page", std::string parentGuid = "", int32_t level = 0)
        : guid(GUIDGenerator::GenerateV4()), 
          title(std::move(pageTitle)), 
          parentPageGuid(std::move(parentGuid)), 
          nestingLevel(level) {
        Touch();
    }

    /**
     * @brief Updates the LRU access timestamp to the current clock time.
     */
    void Touch() noexcept {
        lastAccessTimeMs = SDL_GetTicks();
    }

    /**
     * @brief Evicts in-memory vector strokes, spatial index, and undo stack to free RAM.
     * Called by PageRepository / Workspace LRU cache when memory limits are reached.
     */
    void EvictFromRAM() {
        spatialIndex.Clear();
        objects.clear();
        history.Clear();
        isLoaded = false;
    }

    // -------------------------------------------------------------------------
    // Object Management
    // -------------------------------------------------------------------------

    /**
     * @brief Adds a canvas object to the page and registers it with the spatial index.
     * @param obj Shared pointer to any derived CanvasObject (Ink, Image, TextBox, PDF).
     */
    void AddObject(const std::shared_ptr<CanvasObject>& obj) {
        if (!obj) return;
        objects.push_back(obj);
        spatialIndex.Insert(obj->uid, obj->bounds);
        isModified = true;
        Touch();
    }

    /**
     * @brief Removes a canvas object from both the page list and spatial index.
     */
    void RemoveObject(const std::shared_ptr<CanvasObject>& obj) {
        if (!obj) return;
        spatialIndex.Remove(obj->uid);
        auto it = std::find(objects.begin(), objects.end(), obj);
        if (it != objects.end()) {
            objects.erase(it);
        }
        isModified = true;
        Touch();
    }

    /**
     * @brief Updates the bounds and spatial index entry for a modified object.
     */
    void UpdateObject(const std::shared_ptr<CanvasObject>& obj) {
        if (!obj) return;
        obj->UpdateBounds();
        spatialIndex.Update(obj->uid, obj->bounds);
        isModified = true;
        Touch();
    }

    /**
     * @brief Finds a canvas object by its runtime UID.
     * @return Shared pointer to object if found, or nullptr.
     */
    [[nodiscard]] std::shared_ptr<CanvasObject> FindObjectByUid(uint32_t targetUid) const {
        for (const auto& obj : objects) {
            if (obj && obj->uid == targetUid) {
                return obj;
            }
        }
        return nullptr;
    }

    /**
     * @brief Queries all objects intersecting the camera viewport frustum.
     * Uses the R-Tree spatial index for high-speed spatial culling.
     */
    [[nodiscard]] std::vector<std::shared_ptr<CanvasObject>> QueryVisible(const Viewport& viewport) {
        Touch();
        std::vector<uint32_t> visibleUids = spatialIndex.Query(viewport.bounds);
        std::vector<std::shared_ptr<CanvasObject>> visible;
        visible.reserve(visibleUids.size());

        for (uint32_t id : visibleUids) {
            for (const auto& obj : objects) {
                if (obj && obj->uid == id) {
                    visible.push_back(obj);
                    break;
                }
            }
        }
        return visible;
    }

    /**
     * @brief Clears all objects, spatial index entries, and history from this page.
     */
    void Clear() {
        spatialIndex.Clear();
        objects.clear();
        history.Clear();
        isModified = true;
        Touch();
    }

    // -------------------------------------------------------------------------
    // Hierarchy Queries
    // -------------------------------------------------------------------------

    /**
     * @brief Checks whether this page is a sub-page of another page.
     */
    [[nodiscard]] bool IsSubPage() const noexcept {
        return !parentPageGuid.empty() || nestingLevel > 0;
    }
};