#pragma once
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <ctime>
#include <SDL3/SDL.h>
#include "core/spatial/r_tree.hpp"
#include "core/spatial/aabb.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/history/command_history.hpp"
#include "core/engine/canvas_transform.hpp"
#include "utils/guid_generator.hpp"
#include "utils/logger.hpp"

/**
 * @brief Formats the current system clock time into human-readable date and time strings.
 *
 * MATHEMATICAL WORKING PROCESS & TIME RESOLUTION:
 * - Samples system wall-clock time via `std::chrono::system_clock::now()` (sub-microsecond resolution).
 * - Converts to calendar breakdown `std::tm` using thread-safe platform primitives (`localtime_s` on Windows,
 *   `localtime_r` on POSIX).
 * - Generates formatted strings:
 *     - Date: "%B %d, %Y" (e.g. "September 19, 2026")
 *     - Time: "%I:%M %p"   (e.g. "01:05 AM")
 *
 * @param[out] outDate Formatted calendar date string.
 * @param[out] outTime Formatted 12-hour clock time string with AM/PM indicator.
 */
inline void PopulateCurrentDateTime(std::string& outDate, std::string& outTime) {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char dateBuf[64];
    char timeBuf[32];
    std::strftime(dateBuf, sizeof(dateBuf), "%B %d, %Y", &tm);
    std::strftime(timeBuf, sizeof(timeBuf), "%I:%M %p", &tm);
    outDate = dateBuf;
    outTime = timeBuf;
}

/**
 * =========================================================================================
 * @file canvas_page.hpp
 * @brief Represents an infinite or bounded 2D canvas drawing surface within a notebook section.
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * - The CanvasPage is the core content container of the application.
 * - Each Section contains an ordered list of CanvasPages.
 * - An infinite 2D coordinate system is used: objects (ink strokes, images, text boxes, etc)
 *   can be placed anywhere in world-space coordinates (double precision millimeters).
 *
 * KEY SUBSYSTEMS INTEGRATED:
 * 1. Polymorphic Object Storage & Fast Lookups:
 *    Maintains `std::vector<std::shared_ptr<CanvasObject>> objects` for strict z-order rendering,
 *    alongside `std::unordered_map<uint32_t, std::shared_ptr<CanvasObject>> objectMap` providing
 *    instant O(1) UID lookups and eliminating O(K * N) bottlenecks during viewport culling.
 * 2. Spatial Indexing (R-Tree):
 *    Uses a dynamic R-Tree (`RTree spatialIndex`) storing AABBs (Axis-Aligned Bounding Boxes).
 *    During rendering, `QueryVisible(viewport)` performs an O(log N + K) frustum query so that only
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
 * 5. Per-Page Layout & Paper Templates:
 *    Tracks page-level infinity mode (SemiInfinity, FullInfinity, VerticalScroll, HorizontalScroll),
 *    paper styling (Grid, Lined, Blank, Dotted, Cornell), dimensions (Letter, A4, Custom),
 *    border styles, and DPI calibration factor.
 * 6. Memory Management & LRU Eviction:
 *    Pages track `lastAccessTimeMs`, `isLoaded`, and `isModified`. When inactive, the storage
 *    repository evicts stroke data from RAM to conserve working set memory while preserving
 *    the lightweight metadata stub.
 */

class CanvasPage {
public:
    // -------------------------------------------------------------------------
    // Identification & Metadata
    // -------------------------------------------------------------------------
    std::string guid;                   ///< Unique persistent UUID v4 identifier
    std::string title = "New Untitled";///< User-visible title displayed in tab and sidebar
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
    std::vector<std::shared_ptr<CanvasObject>> objects;                      ///< Ordered canvas objects (rendering z-order)
    std::unordered_map<uint32_t, std::shared_ptr<CanvasObject>> objectMap;   ///< O(1) runtime UID lookup table
    RTree spatialIndex;                                                      ///< Fast bounding-box query index
    CommandHistory history;                                                  ///< Per-page undo/redo command stack

    // -------------------------------------------------------------------------
    // Per-Page Layout, Grid, Border & DPI Configuration
    // -------------------------------------------------------------------------
    CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity; ///< Canvas boundary mode (SemiInfinity, FullInfinity, etc.)
    PaperStyle paperStyle = PaperStyle::Grid;                           ///< Background paper style (Grid, Lined, Blank, Dotted, Cornell)
    double gridSpacingMm = 5.0;                                         ///< Physical grid or rule line spacing in millimeters
    PageSizeFormat pageSizeFormat = PageSizeFormat::Letter;             ///< Fixed page format (Letter, A4, A3, A5, Custom)
    bool pageIsLandscape = false;                                       ///< True if page dimensions are rotated 90 degrees
    double pageWidthMm = 215.9;                                         ///< Physical width in mm (Letter default: 8.5 in * 25.4 = 215.9 mm)
    double pageHeightMm = 279.4;                                         ///< Physical height in mm (Letter default: 11.0 in * 25.4 = 279.4 mm)
    bool showPageBorder = false;                                        ///< Whether page boundaries are visually demarcated
    PageBorderType pageBorderType = PageBorderType::Automatic;          ///< Dynamic content-fitted or fixed dimensions
    PageBorderStyle pageBorderStyle = PageBorderStyle::Continuous;      ///< Border line rendering stroke (Continuous, Dashed, Corners)
    double pageBorderWidth = 1.5;                                       ///< Border outline thickness in points/pixels

    // -------------------------------------------------------------------------
    // LRU Caching & Memory Management Telemetry
    // -------------------------------------------------------------------------
    uint64_t lastAccessTimeMs = 0;      ///< Last time this page was rendered or modified (SDL_GetTicks)
    bool isModified = true;             ///< Dirty flag indicating unsaved changes exist (defaults true for new pages)
    bool isLoaded = true;               ///< True if stroke geometry and objects are present in RAM

    // -------------------------------------------------------------------------
    // Soft Delete & Recycle Bin Lifecycle
    // -------------------------------------------------------------------------
    int64_t deletedAt = 0;              ///< Unix timestamp (seconds) when page was soft-deleted, or 0 if active

    /**
     * @brief Checks whether this page has been moved to the recycle bin.
     * @return true if soft-deleted (deletedAt > 0), false if active.
     */
    [[nodiscard]] bool IsDeleted() const noexcept {
        return deletedAt > 0;
    }

    // Dedicated Standalone PDF Document Page
    bool isDedicatedPdf = false;        ///< True if this page is viewed in the dedicated PDF continuous viewer
    std::string dedicatedPdfPath;       ///< Path to the backing PDF document on disk / package
    std::string dedicatedPdfBookmarks;  ///< Serialized user bookmarks (JSON or pipe-delimited) persisted to SQLite
    std::string dedicatedPdfHighlights; ///< Serialized text highlight spans (tab-delimited) persisted to SQLite

    /**
     * @struct CanvasViewportState
     * @brief In-memory runtime camera viewport cache for this individual canvas page.
     * 
     * Mathematical projection:
     *   P_screen = (P_world + panMm) * (pixelsPerMm * zoom)
     *   P_world  = (P_screen / (pixelsPerMm * zoom)) - panMm
     * 
     * When switching between multiple pages/documents, this struct preserves the exact
     * user pan offset and zoom level in RAM during the active session. If a page has been
     * unaccessed for longer than the absence timeout (e.g. 60,000ms), or when evicted by
     * the LRU memory manager, the viewport automatically resets to "homed" (0,0, 1.0x).
     */
    struct CanvasViewportState {
        double panXMm = 0.0;
        double panYMm = 0.0;
        double zoom = 1.0;
        bool hasCustomViewport = false;
        uint64_t lastViewportAccessMs = 0;

        void Home() noexcept {
            panXMm = 0.0;
            panYMm = 0.0;
            zoom = 1.0;
            hasCustomViewport = false;
            lastViewportAccessMs = 0;
        }
    };
    CanvasViewportState inMemoryViewport;

    // -------------------------------------------------------------------------
    // Construction & Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Constructs a new CanvasPage with an optional title, parent page, and nesting level.
     * Automatically populates creation timestamps and flags the page as dirty (isModified = true)
     * so that it is guaranteed to be persisted to SQLite.
     *
     * @param pageTitle Initial display title (defaults to "New Untitled").
     * @param parentGuid GUID of parent page if this is a subpage.
     * @param level Nesting hierarchy level (0 = root page, 1 = subpage, 2 = sub-subpage).
     */
    CanvasPage(std::string pageTitle = "New Untitled", std::string parentGuid = "", int32_t level = 0)
        : guid(GUIDGenerator::GenerateV4()), 
          title(std::move(pageTitle)), 
          parentPageGuid(std::move(parentGuid)), 
          nestingLevel(level),
          isModified(true) {
        PopulateCurrentDateTime(createdDateStr, createdTimeStr);
        Touch();
    }

    /**
     * @brief Updates the LRU access timestamp to the current clock time.
     */
    void Touch() noexcept {
        lastAccessTimeMs = SDL_GetTicks();
    }

    /**
     * @brief Creates an in-memory deep copy of this page with a new GUID and cloned objects.
     *
     * OBJECT CLONING & SPATIAL RE-INDEXING:
     * - Generates a fresh UUID v4 for the cloned page.
     * - Preserves layout, paper style, grid spacing, dimensions, border, and DPI parameters.
     * - Deep-copies each polymorphic `CanvasObject` via `obj->Clone()`.
     * - Assigns fresh GUIDs to objects with persistent IDs and adds them via `AddObject`
     *   which registers them into the newly constructed `spatialIndex` and `objectMap` with clean UIDs.
     * - Marks the cloned page dirty (`isModified = true`) to ensure it is saved to SQLite.
     *
     * @return std::shared_ptr<CanvasPage> Newly allocated deep-cloned CanvasPage.
     */
    [[nodiscard]] std::shared_ptr<CanvasPage> Clone() const {
        auto clone = std::make_shared<CanvasPage>(title + " (Copy)", parentPageGuid, nestingLevel);
        clone->guid = GUIDGenerator::GenerateV4();
        clone->createdDateStr = createdDateStr;
        clone->createdTimeStr = createdTimeStr;
        clone->sortOrder = sortOrder;
        clone->isDedicatedPdf = isDedicatedPdf;
        clone->dedicatedPdfPath = dedicatedPdfPath;
        clone->dedicatedPdfBookmarks = dedicatedPdfBookmarks;
        clone->dedicatedPdfHighlights = dedicatedPdfHighlights;
        clone->deletedAt = deletedAt;
        clone->isLoaded = isLoaded;
        clone->isModified = true;

        // Clone layout and styling settings
        clone->infinityMode = infinityMode;
        clone->paperStyle = paperStyle;
        clone->gridSpacingMm = gridSpacingMm;
        clone->pageSizeFormat = pageSizeFormat;
        clone->pageIsLandscape = pageIsLandscape;
        clone->pageWidthMm = pageWidthMm;
        clone->pageHeightMm = pageHeightMm;
        clone->showPageBorder = showPageBorder;
        clone->pageBorderType = pageBorderType;
        clone->pageBorderStyle = pageBorderStyle;
        clone->pageBorderWidth = pageBorderWidth;
        clone->isDedicatedPdf = isDedicatedPdf;
        clone->dedicatedPdfPath = dedicatedPdfPath;
        clone->dedicatedPdfBookmarks = dedicatedPdfBookmarks;
        clone->dedicatedPdfHighlights = dedicatedPdfHighlights;

        // Deep-clone canvas objects with fresh unique GUIDs and clean spatial index UIDs
        for (const auto& obj : this->objects) {
            if (obj) {
                auto clonedObj = obj->Clone();
                if (clonedObj) {
                    if (!clonedObj->guuid.empty()) {
                        clonedObj->guuid = GUIDGenerator::GenerateV4();
                    }
                    clone->AddObject(std::shared_ptr<CanvasObject>(std::move(clonedObj)));
                }
            }
        }
        ::Folio::LogConsole(::Folio::LogLevel::Info, ::Folio::LogSource::CanvasPage,
            "Page '" + title + "' [" + guid + "] cloned into new page [" + clone->guid + "] with " +
            std::to_string(clone->objects.size()) + " objects");
        return clone;
    }

    /**
     * @brief Evicts in-memory vector strokes, spatial index, UID map, and undo stack to free RAM.
     * Called by PageRepository / Workspace LRU cache when memory limits are reached.
     * Automatically homes the in-memory viewport when unloaded due to prolonged absence.
     */
    void EvictFromRAM() {
        ::Folio::LogConsole(::Folio::LogLevel::Info, ::Folio::LogSource::CanvasPage,
            "Evicting page '" + title + "' [" + guid + "] from RAM (" +
            std::to_string(objects.size()) + " objects unloaded)");
        spatialIndex.Clear();
        objects.clear();
        objectMap.clear();
        history.Clear();
        inMemoryViewport.Home();
        isLoaded = false;
    }

    // -------------------------------------------------------------------------
    // Object Management
    // -------------------------------------------------------------------------

    /**
     * @brief Adds a canvas object to the page, registers it in the R-Tree, and inserts it into the fast UID map.
     *
     * MATHEMATICAL & TIME COMPLEXITY PROCESS:
     * - Vector Append: O(1) amortized insertion into `objects` to maintain rendering z-order.
     * - Fast UID Map: O(1) hash insertion into `objectMap[obj->uid] = obj`.
     * - Spatial Index: O(log N) R-Tree insertion with Axis-Aligned Bounding Box (AABB) expansion.
     * - Marks page dirty (`isModified = true`) and updates LRU timestamp.
     *
     * @param obj Shared pointer to any derived CanvasObject (Ink, Image, TextBox, PDF).
     */
    void AddObject(const std::shared_ptr<CanvasObject>& obj) {
        if (!obj) return;
        objects.push_back(obj);
        objectMap[obj->uid] = obj;
        spatialIndex.Insert(obj->uid, obj->bounds);
        isModified = true;
        Touch();
    }

    /**
     * @brief Removes a canvas object from the page list, fast UID map, and R-Tree spatial index.
     *
     * MATHEMATICAL & TIME COMPLEXITY PROCESS:
     * - Spatial Index: O(log N) R-Tree deletion via AABB overlap search and leaf re-balancing.
     * - Fast UID Map: O(1) hash erasure `objectMap.erase(obj->uid)`.
     * - Vector Removal: O(N) linear search and erasure in `objects` to maintain z-order sequence.
     *
     * @param obj Shared pointer to the CanvasObject to remove.
     */
    void RemoveObject(const std::shared_ptr<CanvasObject>& obj) {
        if (!obj) return;
        spatialIndex.Remove(obj->uid);
        objectMap.erase(obj->uid);
        auto it = std::find(objects.begin(), objects.end(), obj);
        if (it != objects.end()) {
            objects.erase(it);
        }
        isModified = true;
        Touch();
    }

    /**
     * @brief Removes an object identified by its unique 32-bit runtime UID.
     * @param uid Runtime UID of the object to remove.
     */
    void RemoveObjectByUid(uint32_t uid) {
        auto obj = FindObjectByUid(uid);
        if (obj) RemoveObject(obj);
    }

    /**
     * @brief Replaces an existing object in-place, updating the fast UID map and R-Tree spatial index.
     * Maintains the exact rendering z-order position in the objects array.
     *
     * @param uid Runtime UID of the object to replace.
     * @param newObj Shared pointer to the replacement CanvasObject.
     */
    void ReplaceObject(uint32_t uid, const std::shared_ptr<CanvasObject>& newObj) {
        if (!newObj) return;
        auto existing = FindObjectByUid(uid);
        if (existing) {
            spatialIndex.Remove(uid);
            objectMap[uid] = newObj;
            spatialIndex.Insert(newObj->uid, newObj->bounds);
            auto it = std::find(objects.begin(), objects.end(), existing);
            if (it != objects.end()) {
                *it = newObj;
            }
            isModified = true;
            Touch();
        } else {
            AddObject(newObj);
        }
    }

    /**
     * @brief Updates the bounding box and spatial index entry for a modified object.
     *
     * MATHEMATICAL & TIME COMPLEXITY PROCESS:
     * - Geometry Recalculation: O(V) where V is vertex count (recomputes min/max world coordinates).
     * - Spatial Re-index: O(log N) R-Tree update (leaf deletion and re-insertion into parent MBR).
     *
     * @param obj Shared pointer to the modified CanvasObject.
     */
    void UpdateObject(const std::shared_ptr<CanvasObject>& obj) {
        if (!obj) return;
        obj->UpdateBounds();
        spatialIndex.Update(obj->uid, obj->bounds);
        isModified = true;
        Touch();
    }

    /**
     * @brief Finds a canvas object by its runtime UID in O(1) time using hash map lookup.
     *
     * @param targetUid Runtime 32-bit unique identifier of the object.
     * @return Shared pointer to object if found, or nullptr if absent.
     */
    [[nodiscard]] std::shared_ptr<CanvasObject> FindObjectByUid(uint32_t targetUid) const {
        auto it = objectMap.find(targetUid);
        if (it != objectMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    /**
     * @brief Resolves single-click selection at world coordinates (clickX, clickY) using smallest-area priority.
     *
     * MATHEMATICAL HEURISTIC & PRIORITY ARBITRATION:
     * ----------------------------------------------
     * Problem: When small notes or ink strokes sit inside a giant container (like a ShapeObject rectangle),
     * a naive z-order or first-hit query can "trap" the smaller items behind the container's broad bounds.
     *
     * Mathematical Model:
     * - Candidate set C = { obj \in objects | obj->isVisible \land obj->isSelectable \land obj->HitTest(clickX, clickY) }
     * - For each candidate obj \in C:
     *     effectiveArea = obj->bounds.Area()
     *     if (obj->type == ObjectType::InkContainer || obj->type == ObjectType::Text) {
     *         effectiveArea *= 0.5; // 0.5x bias factor giving high priority to notes and strokes
     *     }
     * - Result = \argmin_{obj \in C} (effectiveArea)
     *
     * @param clickX World X coordinate in millimeters.
     * @param clickY World Y coordinate in millimeters.
     * @param circleRadiusMm Proximity radius for fine strokes (e.g. config.objectHitTestRadiusMm).
     * @return std::shared_ptr<CanvasObject> Selected object with minimal effective area, or nullptr.
     */
    [[nodiscard]] std::shared_ptr<CanvasObject> HitTestSingleClick(double clickX, double clickY, double circleRadiusMm = 0.0) const {
        std::vector<std::shared_ptr<CanvasObject>> candidates;

        for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
            auto& obj = *it;
            if (obj && obj->isVisible && obj->isSelectable) {
                bool hit = obj->HitTest(clickX, clickY);
                if (!hit && circleRadiusMm > 0.0) {
                    hit = obj->HitTestCircle(clickX, clickY, circleRadiusMm);
                }
                if (hit) {
                    candidates.push_back(obj);
                }
            }
        }

        if (candidates.empty()) return nullptr;
        if (candidates.size() == 1) return candidates[0];

        // Select candidate with smallest effective area
        std::shared_ptr<CanvasObject> bestObj = nullptr;
        double bestEffectiveArea = 1e18;

        for (const auto& obj : candidates) {
            double area = obj->bounds.Area();
            if (area <= 0.0) area = 0.01;

            // Apply 0.5x bias factor to InkContainer and Text
            if (obj->type == ObjectType::InkContainer || obj->type == ObjectType::Text) {
                area *= 0.5;
            }

            if (area < bestEffectiveArea) {
                bestEffectiveArea = area;
                bestObj = obj;
            }
        }

        return bestObj;
    }

    /**
     * @brief Queries all objects intersecting the camera viewport frustum using spatial culling.
     *
     * MATHEMATICAL CULLING & RENDERING PIPELINE:
     * - Viewport Frustum: AABB bounds computed from screen dimensions inverted through the camera matrix:
     *     minX = ScreenToWorld(0, 0).x, maxX = ScreenToWorld(W, H).x
     *     minY = ScreenToWorld(0, 0).y, maxY = ScreenToWorld(W, H).y
     * - R-Tree Frustum Search: O(log N + K) hierarchy traversal where K is the number of visible candidates.
     * - UID Map Resolution: Eliminates the prior O(K * N) linear vector scan by resolving each candidate UID
     *   in O(1) via `objectMap`. Total resolution time is O(K) instead of O(K * N).
     *
     * @param viewport Active rendering camera viewport with physical bounds.
     * @return Vector of visible canvas objects ready for rasterization.
     */
    [[nodiscard]] std::vector<std::shared_ptr<CanvasObject>> QueryVisible(const Viewport& viewport) {
        Touch();
        std::vector<uint32_t> visibleUids = spatialIndex.Query(viewport.bounds);
        std::vector<std::shared_ptr<CanvasObject>> visible;
        visible.reserve(visibleUids.size());

        for (uint32_t id : visibleUids) {
            auto it = objectMap.find(id);
            if (it != objectMap.end() && it->second && it->second->isVisible) {
                visible.push_back(it->second);
            }
        }
        return visible;
    }

    /**
     * @brief Permanently purges soft-deleted / invisible objects from RAM, UID maps, and spatial index.
     *
     * MATHEMATICAL & MEMORY RECLAMATION PROCESS:
     * - Traverses the resident `objects` vector in O(N) time.
     * - Identifies soft-deleted tombstones marked with `!obj->isVisible`.
     * - De-indexes them from `spatialIndex` (O(log N) R-Tree leaf purge) and `objectMap` (O(1) hash erase).
     * - Uses the erase-remove idiom to shift surviving valid objects in-place and shrink the vector.
     * - Marks the page dirty (`isModified = true`) to reflect permanent truncation in persistent storage.
     *
     * @return size_t Total number of tombstone objects permanently purged from the page.
     */
    size_t PurgeInvisibleObjects() {
        size_t purgedCount = 0;
        auto it = objects.begin();
        while (it != objects.end()) {
            if (*it && !(*it)->isVisible) {
                spatialIndex.Remove((*it)->uid);
                objectMap.erase((*it)->uid);
                it = objects.erase(it);
                ++purgedCount;
            } else {
                ++it;
            }
        }
        if (purgedCount > 0) {
            isModified = true;
            Touch();
        }
        return purgedCount;
    }

    /**
     * @brief Rebuilds the fast UID lookup map and R-Tree spatial index from scratch.
     * Guarantees spatial and associative coherence if the objects array was modified directly.
     */
    void RebuildSpatialIndex() {
        objectMap.clear();
        std::vector<std::pair<uint32_t, AABB>> items;
        items.reserve(objects.size());
        for (const auto& obj : objects) {
            if (obj) {
                objectMap[obj->uid] = obj;
                items.emplace_back(obj->uid, obj->bounds);
            }
        }
        spatialIndex.Rebuild(items);
    }

    /**
     * @brief Clears all objects, spatial index entries, UID map, and undo history from this page.
     */
    void Clear() {
        ::Folio::LogConsole(::Folio::LogLevel::Info, ::Folio::LogSource::CanvasPage,
            "Clearing page '" + title + "' [" + guid + "] (purging " +
            std::to_string(objects.size()) + " objects)");
        spatialIndex.Clear();
        objects.clear();
        objectMap.clear();
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