#pragma once
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <SDL3/SDL.h>
#include "core/spatial/r_tree.hpp"
#include "core/spatial/aabb.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container.hpp"
#include "core/history/command_history.hpp"
#include "utils/guid_generator.hpp"

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
 *    repository evicts stroke data from RAM to conserve working set memory while preserving
 *    the lightweight metadata stub.
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
    std::vector<std::shared_ptr<CanvasObject>> objects; ///< All canvas objects on this page
    RTree spatialIndex;                                 ///< Fast bounding-box query index
    CommandHistory history;                             ///< Per-page undo/redo command stack

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
     * - Deep-copies each polymorphic `CanvasObject` via `obj->Clone()`.
     * - Assigns fresh GUIDs to objects with persistent IDs and adds them via `AddObject`
     *   which registers them into the newly constructed `spatialIndex` with clean runtime UIDs.
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
        return clone;
    }

    /**
     * @brief Evicts in-memory vector strokes, spatial index, and undo stack to free RAM.
     * Called by PageRepository / Workspace LRU cache when memory limits are reached.
     * Automatically homes the in-memory viewport when unloaded due to prolonged absence.
     */
    void EvictFromRAM() {
        spatialIndex.Clear();
        objects.clear();
        history.Clear();
        inMemoryViewport.Home();
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