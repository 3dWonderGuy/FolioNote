#pragma once

/**
 * =========================================================================================
 * @file page_repository.hpp
 * @brief High-Level Document Storage Coordinator & Asynchronous Persistence Engine
 * =========================================================================================
 * 
 * --- ARCHITECTURAL OVERVIEW ---
 * PageRepository serves as the central storage and persistence coordinator for the FolioNote
 * notebook engine. It mediates between the high-speed in-memory document object model
 * (Notebook, SectionGroup, Section, CanvasPage) and persistent storage on disk.
 * 
 * To provide fluid 120+ FPS canvas rendering and zero-latency inking with pen digitizers,
 * all expensive disk I/O, compression algorithms, and database operations are executed
 * through an asynchronous 4-stage pipeline that guarantees the UI/Inking thread is never blocked.
 * 
 * --- ON-DISK PACKAGE STRUCTURE (.notebook / .fn) ---
 * Every FolioNote notebook is encapsulated within a structured package folder on disk:
 * ┌────────────────────────────────────────────────────────────────────────┐
 * │ [NotebookName].notebook/                                               │
 * │  ├── structure.db                 (SQLite metadata, hierarchy & FTS5)  │
 * │  ├── structure.db-wal             (SQLite Write-Ahead Log journal)     │
 * │  ├── pages/                                                            │
 * │  │    ├── {page-uuid-1}.ink       (ZSTD/LZ4 compressed vector payload) │
 * │  │    └── {page-uuid-2}.ink                                            │
 * │  └── imports/                                                          │
 * │       ├── pdfs/                   (Imported PDF reference documents)   │
 * │       └── images/                 (Imported high-res bitmap assets)    │
 * └────────────────────────────────────────────────────────────────────────┘
 * 
 * --- PERSISTENCE & MEMORY MODEL ---
 * 1. Hybrid Storage Strategy:
 *    - Relational Hierarchy & Search Index: Maintained in SQLite (`structure.db`) via DBManager.
 *    - Heavy Vector Stroke & Object Payloads: Stored in isolated `.ink` binary files.
 * 
 * 2. Asynchronous 4-Stage Save Pipeline:
 *    [Main / UI Thread] ──(1. Snapshot Metadata)──> (2. Synchronous Binary Serialization)
 *                                                               │ (Thread-safe memory buffer)
 *                                                               ▼
 *    [Worker ThreadPool] ◄──(3. Background Enqueue)─────────────┘
 *           │
 *           ├── (4a. Disk I/O: Write compressed payload to pages/{guid}.ink)
 *           └── (4b. Database: Atomic Upsert to SQLite structure.db)
 * 
 * 3. Lazy Loading & Working Set Management:
 *    - When opening a notebook, only the lightweight relational hierarchy and page stubs
 *      are loaded into RAM (taking < 1 MB even for thousands of pages).
 *    - Heavy stroke geometry and spatial R-Trees are loaded on-demand when the user views a page.
 * 
 * 4. LRU Cache Eviction & RAM Pruning:
 *    - Inactive pages residing in RAM that exceed an inactivity threshold (e.g., 60s)
 *      are automatically flushed (if modified) and evicted from RAM (`page->EvictFromRAM()`),
 *      reclaiming memory while keeping navigation seamless.
 */

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <SDL3/SDL.h>

#include "core/storage/db_manager.hpp"
#include "core/storage/binary_serializer.hpp"
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "utils/thread_pool.hpp"
#include "utils/logger.hpp"

namespace Folio {

/**
 * @class PageRepository
 * @brief High-level storage coordinator managing document persistence, lazy loading, and RAM caching.
 */
class PageRepository {
public:
    /// SQLite database interface handling structural metadata, foreign keys, and search catalogs.
    std::shared_ptr<DBManager> dbManager;

    /// Root filesystem path of the currently opened notebook package directory.
    std::string currentPackagePath;

    /**
     * @brief Default constructor. Instantiates an internal DBManager instance.
     */
    PageRepository() 
        : dbManager(std::make_shared<DBManager>()) {}

    /**
     * @brief Parameterized constructor allowing injection of a custom or shared DBManager.
     * @param db Shared pointer to an existing DBManager.
     */
    explicit PageRepository(std::shared_ptr<DBManager> db)
        : dbManager(std::move(db)) {}

    /**
     * @brief Connects to a notebook package directory, creates missing folders, and opens the SQLite DB.
     * 
     * Verifies and generates the required subdirectories (`pages/`, `imports/pdfs/`, `imports/images/`)
     * and establishes a connection to the SQLite `structure.db` database inside the package.
     * 
     * @param packagePath Absolute or relative path to the `.notebook` package directory.
     * @return true if the package directory exists/was created and `structure.db` opened successfully; false otherwise.
     */
    bool OpenNotebookPackage(const std::string& packagePath) {
        std::error_code ec;

        // Step 1: Ensure root package directory exists
        if (!std::filesystem::exists(packagePath, ec)) {
            std::filesystem::create_directories(packagePath, ec);
        }

        // Step 2: Ensure internal package subdirectories exist for binary payloads and imports
        std::filesystem::create_directories(std::filesystem::path(packagePath) / "pages", ec);
        std::filesystem::create_directories(std::filesystem::path(packagePath) / "imports" / "pdfs", ec);
        std::filesystem::create_directories(std::filesystem::path(packagePath) / "imports" / "images", ec);

        this->currentPackagePath = packagePath;

        // Step 3: Connect to the embedded SQLite database
        if (!dbManager) dbManager = std::make_shared<DBManager>();
        std::string dbPath = (std::filesystem::path(packagePath) / "structure.db").string();
        return dbManager->Open(dbPath);
    }

    /**
     * @brief Asynchronously serializes, compresses, and writes a CanvasPage to disk and SQLite.
     * 
     * --- THREAD SAFETY & NON-BLOCKING DESIGN ---
     * To ensure zero frame drops on the main UI/inking thread:
     * 1. Metadata (GUID, title, timestamps, hierarchy) is captured synchronously.
     * 2. The page's vector objects and history are serialized synchronously into an immutable
     *    in-memory byte buffer (`blobData`). This snapshotting prevents race conditions with
     *    new ink strokes being actively drawn by the user.
     * 3. The file write (`.ink` compressed file) and SQLite transaction are dispatched to a
     *    background ThreadPool worker thread.
     * 
     * @param page Shared pointer to the CanvasPage to be saved.
     * @param sectionGuid GUID of the parent Section containing this page.
     * @param sortOrder Display position inside the section (0 = use page's current sortOrder).
     * @return std::future<bool> Future indicating completion and success of the background save operation.
     */
    std::future<bool> SavePageAsync(std::shared_ptr<CanvasPage> page, const std::string& sectionGuid, int32_t sortOrder = 0) {
        // Validation check
        if (!page || !dbManager || !dbManager->IsOpen()) {
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }

        // ---------------------------------------------------------------------------------
        // Stage 1: Synchronous Metadata Snapshot
        // Capture all scalar fields on the calling thread before any async dispatch.
        // ---------------------------------------------------------------------------------
        std::string pageGuid = page->guid;
        std::string title = page->title;
        std::string createdDate = page->createdDateStr;
        std::string createdTime = page->createdTimeStr;
        std::string parentGuid = page->parentPageGuid;
        int32_t level = page->nestingLevel;
        bool collapsed = page->isCollapsed;
        int32_t order = (sortOrder != 0) ? sortOrder : page->sortOrder;
        bool isDedicatedPdf = page->isDedicatedPdf;
        std::string dedicatedPdfPath = page->dedicatedPdfPath;
        std::string dedicatedPdfBookmarks = page->dedicatedPdfBookmarks;
        std::string dedicatedPdfHighlights = page->dedicatedPdfHighlights;

        // ---------------------------------------------------------------------------------
        // Stage 2: Synchronous In-Memory Binary Serialization
        // Pack stroke points, canvas objects, and metadata into a compressed memory buffer.
        // Doing this synchronously creates a point-in-time snapshot, guaranteeing thread safety
        // even if the user continues drawing strokes immediately after this call returns.
        // ---------------------------------------------------------------------------------
        auto blobData = std::make_shared<std::vector<uint8_t>>();
        if (!BinarySerializer::SerializePage(*page, *blobData)) {
            LOG_ERROR(PageRepository, "Failed to serialize page: " + pageGuid);
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }

        // Mark page as clean in memory
        page->isModified = false;

        auto db = this->dbManager;
        std::string pkgPath = this->currentPackagePath;

        // ---------------------------------------------------------------------------------
        // Stage 3 & 4: Background ThreadPool Dispatch (Disk I/O & SQLite Write)
        // Offload disk writing and database upserting to a worker thread.
        // ---------------------------------------------------------------------------------
        return GetGlobalThreadPool().Enqueue([db, pkgPath, pageGuid, sectionGuid, title, createdDate, createdTime, order, blobData, parentGuid, level, collapsed, isDedicatedPdf, dedicatedPdfPath, dedicatedPdfBookmarks, dedicatedPdfHighlights]() -> bool {
            // Write compressed binary payload to disk: pages/{pageGuid}.ink
            std::string inkPath = (std::filesystem::path(pkgPath) / "pages" / (pageGuid + ".ink")).string();
            std::ofstream out(inkPath, std::ios::binary);
            bool hasBlob = false;
            if (out) {
                out.write(reinterpret_cast<const char*>(blobData->data()), blobData->size());
                out.close();
                hasBlob = true;
            } else {
                LOG_ERROR(PageRepository, "Failed to write .ink file for page: " + pageGuid);
            }

            // Update SQLite metadata record in 'pages' table
            bool success = db->SavePageMetadata(pageGuid, sectionGuid, title, createdDate, createdTime, order, hasBlob, parentGuid, level, collapsed, isDedicatedPdf, dedicatedPdfPath, dedicatedPdfBookmarks, dedicatedPdfHighlights);
            if (!success) {
                LOG_ERROR(PageRepository, "Asynchronous page metadata write failed for GUID: " + pageGuid);
            }
            return success;
        });
    }

    /**
     * @brief Loads and deserializes a page payload from the .ink binary file into memory on demand.
     * 
     * --- LAZY LOADING STRATEGY ---
     * - If `page->isLoaded` is already true, this function immediately returns true (no-op).
     * - Otherwise, it reads the `.ink` binary file from `pages/{pageGuid}.ink`, decompresses
     *   the payload using `BinarySerializer::DeserializePage`, populates the page's object list,
     *   rebuilds the R-Tree spatial index, and updates the access timestamp (`page->Touch()`).
     * - If no `.ink` file is present (e.g., brand new page), it initializes an empty canvas.
     * 
     * @param page Shared pointer to the CanvasPage stub to load into RAM.
     * @return true on successful load/deserialization; false on I/O or corruption error.
     */
    bool LoadPage(std::shared_ptr<CanvasPage> page) {
        if (!page || !dbManager || !dbManager->IsOpen() || currentPackagePath.empty()) return false;
        
        // Fast-path: Already loaded in RAM, skip I/O
        if (page->isLoaded) return true;

        std::string inkPath = (std::filesystem::path(currentPackagePath) / "pages" / (page->guid + ".ink")).string();
        std::vector<uint8_t> compressedBlob;
        
        // Read the binary file into memory
        std::ifstream in(inkPath, std::ios::binary | std::ios::ate);
        if (in) {
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
            if (size > 0) {
                compressedBlob.resize(size);
                if (!in.read(reinterpret_cast<char*>(compressedBlob.data()), size)) {
                    compressedBlob.clear();
                }
            }
            in.close();
        }

        // Handle case where .ink payload does not exist yet (treat as fresh blank page)
        if (compressedBlob.empty()) {
            LOG_WARN(PageRepository, "No .ink payload found for page: " + page->guid + ", treating as empty page.");
            page->Clear();
            page->isLoaded = true;
            page->isModified = false;
            page->Touch();
            return true;
        }

        // Decompress and deserialize vector strokes, shapes, and spatial index
        if (!BinarySerializer::DeserializePage(compressedBlob.data(), compressedBlob.size(), *page)) {
            LOG_ERROR(PageRepository, "Failed to deserialize compressed .ink payload for page: " + page->guid);
            return false;
        }

        // Mark as resident in memory and update LRU timestamp
        page->isLoaded = true;
        page->isModified = false;
        page->Touch();
        LOG_INFO(PageRepository, "Successfully loaded .ink page from disk: " + page->title + " (" + page->guid + ")");
        return true;
    }

    /**
     * @brief Saves the entire notebook hierarchy (metadata, section groups, sections, and modified pages).
     * 
     * Performs a complete recursive synchronization:
     * 1. Updates notebook metadata (title, theme color) in SQLite `notebook_meta`.
     * 2. Upserts all SectionGroups and nested sub-groups into SQLite `section_groups`.
     * 3. Upserts all Sections into SQLite `sections`.
     * 4. Identifies modified (`isModified == true`) or unloaded pages and triggers `SavePageAsync`.
     * 
     * @param notebook Shared pointer to the active Notebook to persist.
     */
    void SaveNotebookAsync(std::shared_ptr<Notebook> notebook) {
        if (!notebook || !dbManager || !dbManager->IsOpen()) return;

        // -----------------------------------------------------------------------------
        // Step 1: Save Top-Level Notebook Metadata
        // -----------------------------------------------------------------------------
        DBNotebookRecord nbRecord;
        nbRecord.guid = notebook->guid;
        nbRecord.name = notebook->name;
        nbRecord.colorR = notebook->colorTag.x;
        nbRecord.colorG = notebook->colorTag.y;
        nbRecord.colorB = notebook->colorTag.z;
        nbRecord.colorA = notebook->colorTag.w;
        dbManager->UpsertNotebookMeta(nbRecord);

        // -----------------------------------------------------------------------------
        // Helper: Traverses and saves a list of sections and their modified child pages
        // -----------------------------------------------------------------------------
        auto saveSectionList = [this, &notebook](const std::vector<std::shared_ptr<Section>>& secList, const std::string& groupGuid) {
            for (int32_t sIdx = 0; sIdx < static_cast<int32_t>(secList.size()); ++sIdx) {
                const auto& section = secList[sIdx];
                if (!section) continue;

                section->sortOrder = sIdx;
                section->groupGuid = groupGuid;

                // Upsert Section record into SQLite
                DBSectionRecord secRecord;
                secRecord.guid = section->guid;
                secRecord.notebookGuid = notebook->guid;
                secRecord.groupGuid = groupGuid;
                secRecord.name = section->name;
                secRecord.colorR = section->colorTag.x;
                secRecord.colorG = section->colorTag.y;
                secRecord.colorB = section->colorTag.z;
                secRecord.colorA = section->colorTag.w;
                secRecord.sortOrder = sIdx;
                dbManager->UpsertSection(secRecord);

                // Persist modified child pages
                for (int32_t pIdx = 0; pIdx < static_cast<int32_t>(section->pages.size()); ++pIdx) {
                    const auto& page = section->pages[pIdx];
                    if (!page) continue;

                    page->sortOrder = pIdx;

                    // Always persist the new sort_order for every page — this is a
                    // lightweight UPDATE (sort_order + updated_at only) and ensures
                    // drag-and-drop or Move Up/Down reorders survive app restart even
                    // when no ink was drawn on the page (isModified would be false).
                    dbManager->UpdatePageSortOrder(page->guid, pIdx);

                    // Full blob + metadata save: only needed when canvas content changed
                    if (page->isModified || !page->isLoaded) {
                        SavePageAsync(page, section->guid, pIdx);
                    }
                }
            }
        };

        // -----------------------------------------------------------------------------
        // Step 2: Save Section Groups and their nested sub-groups & sections
        // -----------------------------------------------------------------------------
        for (int32_t gIdx = 0; gIdx < static_cast<int32_t>(notebook->sectionGroups.size()); ++gIdx) {
            const auto& group = notebook->sectionGroups[gIdx];
            if (!group) continue;

            group->sortOrder = gIdx;
            DBSectionGroupRecord grpRecord;
            grpRecord.guid = group->guid;
            grpRecord.notebookGuid = notebook->guid;
            grpRecord.parentGroupGuid = group->parentGroupGuid;
            grpRecord.name = group->name;
            grpRecord.colorR = group->colorTag.x;
            grpRecord.colorG = group->colorTag.y;
            grpRecord.colorB = group->colorTag.z;
            grpRecord.colorA = group->colorTag.w;
            grpRecord.sortOrder = gIdx;
            grpRecord.isCollapsed = group->isCollapsed;
            dbManager->UpsertSectionGroup(grpRecord);

            // Save sections residing directly in this group
            saveSectionList(group->sections, group->guid);

            // Save any nested sub-groups
            for (int32_t subIdx = 0; subIdx < static_cast<int32_t>(group->subGroups.size()); ++subIdx) {
                const auto& subGrp = group->subGroups[subIdx];
                if (!subGrp) continue;

                subGrp->sortOrder = subIdx;
                DBSectionGroupRecord subRec;
                subRec.guid = subGrp->guid;
                subRec.notebookGuid = notebook->guid;
                subRec.parentGroupGuid = group->guid;
                subRec.name = subGrp->name;
                subRec.colorR = subGrp->colorTag.x;
                subRec.colorG = subGrp->colorTag.y;
                subRec.colorB = subGrp->colorTag.z;
                subRec.colorA = subGrp->colorTag.w;
                subRec.sortOrder = subIdx;
                subRec.isCollapsed = subGrp->isCollapsed;
                dbManager->UpsertSectionGroup(subRec);

                saveSectionList(subGrp->sections, subGrp->guid);
            }
        }

        // -----------------------------------------------------------------------------
        // Step 3: Save Top-Level Sections (sections not assigned to any group)
        // -----------------------------------------------------------------------------
        saveSectionList(notebook->sections, "");
    }

    /**
     * @brief Reads the entire notebook hierarchy from SQLite and constructs in-memory object graphs.
     * 
     * --- FAST STARTUP & LAZY INITIALIZATION ---
     * - Rebuilds the Notebook, SectionGroup hierarchy, and Section trees from `structure.db`.
     * - Instantiates `CanvasPage` stubs with their titles, GUIDs, and tree levels, but sets
     *   `isLoaded = false` so large vector payloads are NOT read into RAM during startup.
     * - Preloads only the active page (`notebook->GetActivePage()`) into memory for instant display.
     * 
     * @param packagePath Path to the notebook package directory.
     * @return Shared pointer to the reconstructed Notebook, or nullptr on failure.
     */
    std::shared_ptr<Notebook> LoadNotebookHierarchy(const std::string& packagePath) {
        if (!OpenNotebookPackage(packagePath)) {
            return nullptr;
        }

        // Read notebook metadata
        DBNotebookRecord nbMeta;
        if (!dbManager->LoadNotebookMeta(nbMeta)) {
            LOG_ERROR(PageRepository, "Failed to read notebook metadata from: " + packagePath);
            return nullptr;
        }

        auto notebook = std::make_shared<Notebook>(nbMeta.name, ImVec4(nbMeta.colorR, nbMeta.colorG, nbMeta.colorB, nbMeta.colorA));
        notebook->guid = nbMeta.guid;
        notebook->filePath = packagePath;
        notebook->sections.clear();
        notebook->sectionGroups.clear();

        // -----------------------------------------------------------------------------
        // Step 0: Automatic 30-Day Recycle Bin Purge
        // Mathematical Invariant: Cutoff timestamp = CurrentTime - (30 * 86,400 seconds).
        // Any pages or sections soft-deleted more than 30 days ago have their binary .ink
        // files permanently unlinked from disk and their SQLite records deleted.
        // -----------------------------------------------------------------------------
        dbManager->PurgeExpiredRecycleBinItems(notebook->guid, packagePath);

        // -----------------------------------------------------------------------------
        // Step 1: Load Section Groups and resolve nested parent/child relationships
        // -----------------------------------------------------------------------------
        std::unordered_map<std::string, std::shared_ptr<SectionGroup>> groupMap;
        auto groupRecords = dbManager->LoadSectionGroups(notebook->guid);
        for (const auto& gRec : groupRecords) {
            auto grp = std::make_shared<SectionGroup>(gRec.name, gRec.parentGroupGuid);
            grp->guid = gRec.guid;
            grp->notebookGuid = gRec.notebookGuid;
            grp->colorTag = ImVec4(gRec.colorR, gRec.colorG, gRec.colorB, gRec.colorA);
            grp->sortOrder = gRec.sortOrder;
            grp->isCollapsed = gRec.isCollapsed;
            groupMap[grp->guid] = grp;
        }

        // Connect nested subgroups to parents, or add to top-level sectionGroups list
        for (const auto& gRec : groupRecords) {
            auto grp = groupMap[gRec.guid];
            if (!grp) continue;
            if (!grp->parentGroupGuid.empty() && groupMap.find(grp->parentGroupGuid) != groupMap.end()) {
                groupMap[grp->parentGroupGuid]->subGroups.push_back(grp);
            } else {
                notebook->sectionGroups.push_back(grp);
            }
        }

        // -----------------------------------------------------------------------------
        // Step 2: Load Sections and Page Stubs
        // -----------------------------------------------------------------------------
        auto sectionRecords = dbManager->LoadSections(notebook->guid);
        for (const auto& secRec : sectionRecords) {
            auto section = std::make_shared<Section>(secRec.name, "", secRec.groupGuid);
            section->guid = secRec.guid;
            section->colorTag = ImVec4(secRec.colorR, secRec.colorG, secRec.colorB, secRec.colorA);
            section->sortOrder = secRec.sortOrder;
            section->pages.clear();

            // Load lightweight page metadata records from SQLite
            auto pageRecords = dbManager->LoadPagesMetadata(secRec.guid);
            for (const auto& pRec : pageRecords) {
                auto page = std::make_shared<CanvasPage>(pRec.title, pRec.parentPageGuid, pRec.nestingLevel);
                page->guid = pRec.guid;
                page->createdDateStr = pRec.createdDate;
                page->createdTimeStr = pRec.createdTime;
                page->sortOrder = pRec.sortOrder;
                page->isCollapsed = pRec.isCollapsed;
                page->isDedicatedPdf = pRec.isDedicatedPdf;
                page->dedicatedPdfPath = pRec.dedicatedPdfPath;
                page->dedicatedPdfBookmarks = pRec.dedicatedPdfBookmarks;
                page->dedicatedPdfHighlights = pRec.dedicatedPdfHighlights;
                page->isLoaded = false; // Lazy loading: payload will be fetched on-demand
                page->isModified = false;
                section->pages.push_back(page);
            }

            // Fallback: Ensure section has at least one default blank page if empty
            if (section->pages.empty()) {
                section->pages.push_back(std::make_shared<CanvasPage>("Untitled page"));
            }

            // Assign section to parent SectionGroup or top-level notebook sections list
            if (!secRec.groupGuid.empty() && groupMap.find(secRec.groupGuid) != groupMap.end()) {
                groupMap[secRec.groupGuid]->sections.push_back(section);
            } else {
                notebook->sections.push_back(section);
            }
        }

        // Fallback: Ensure notebook has at least one default section
        if (notebook->sections.empty() && notebook->sectionGroups.empty()) {
            notebook->sections.push_back(std::make_shared<Section>("New Section 1"));
        }

        // -----------------------------------------------------------------------------
        // Sort all section/group collections by their saved sortOrder so that the
        // order the user arranged them (and wrote to SQLite) is faithfully restored.
        // Without this, push_back order is driven by the global SQL ORDER BY sort_order
        // across ALL sections regardless of group, which interleaves groups incorrectly.
        // -----------------------------------------------------------------------------
        auto sortBySortOrder = [](auto& vec) {
            std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) {
                return a->sortOrder < b->sortOrder;
            });
        };

        // Sort top-level section groups
        sortBySortOrder(notebook->sectionGroups);

        // Sort root sections
        sortBySortOrder(notebook->sections);

        // Sort sections inside each group and sub-group
        for (auto& grp : notebook->sectionGroups) {
            if (!grp) continue;
            sortBySortOrder(grp->sections);
            for (auto& sub : grp->subGroups) {
                if (sub) sortBySortOrder(sub->sections);
            }
        }

        // -----------------------------------------------------------------------------
        // Step 3: Preload the active page payload
        // -----------------------------------------------------------------------------
        auto activePage = notebook->GetActivePage();
        if (activePage) {
            LoadPage(activePage);
        }

        return notebook;
    }

    /**
     * @brief LRU Memory Working Set Maintenance: Flushes and evicts inactive pages from RAM.
     * 
     * --- RAM WORKING SET MANAGEMENT (PAGE PURGE CYCLE) ---
     * In long editing sessions with large notebooks (hundreds/thousands of pages), keeping
     * all vector paths and R-Tree spatial structures in RAM would lead to excessive memory consumption.
     * 
     * MaintainLRUCache scans through all pages in the active notebook:
     * 1. Skips the currently active/visible page.
     * 2. Checks if an inactive page is loaded in RAM (`isLoaded == true`) and its elapsed
     *    inactivity time (`nowMs - page->lastAccessTimeMs`) exceeds `timeoutMs` (default 60,000 ms).
     * 3. If dirty (`isModified == true`), flushes changes asynchronously via `SavePageAsync(...)`.
     * 4. Calls `page->EvictFromRAM()`, releasing vector paths, undo/redo history, and R-Tree nodes.
     * 5. Sets `page->isLoaded = false`. Page metadata (title, GUID, order) remains intact in RAM
     *    so the sidebar UI continues to display seamlessly.
     * 
     * @param activeNotebook Shared pointer to the notebook being maintained.
     * @param timeoutMs Maximum allowed inactivity duration before eviction (default: 60,000ms / 1 min).
     * @param maxLoadedPages Maximum resident pages allowed in RAM (default: 10 pages).
     */
    void MaintainLRUCache(std::shared_ptr<Notebook> activeNotebook, uint64_t timeoutMs = 60000, uint32_t maxLoadedPages = 10) {
        if (!activeNotebook) return;

        uint64_t nowMs = SDL_GetTicks();
        auto activePage = activeNotebook->GetActivePage();

        struct ResidentEntry {
            std::shared_ptr<CanvasPage> page;
            std::string sectionGuid;
        };
        std::vector<ResidentEntry> residentInactivePages;

        // Helper lambda to evaluate and prune pages inside a section
        auto evaluateSectionPages = [this, nowMs, &activePage, timeoutMs, &residentInactivePages](const std::shared_ptr<Section>& section) {
            if (!section) return;
            for (const auto& page : section->pages) {
                // Never evict the active page currently viewed/edited by user
                if (!page || page == activePage) continue;

                if (page->isLoaded) {
                    if (nowMs - page->lastAccessTimeMs > timeoutMs) {
                        // Policy 1: Time-to-Live (TTL) eviction
                        if (page->isModified) {
                            SavePageAsync(page, section->guid, page->sortOrder);
                        }
                        page->EvictFromRAM();
                        LOG_INFO(PageRepository, "LRU [TTL Expired]: Evicted inactive page from RAM: " + page->title + " (" + page->guid + ")");
                    } else {
                        // Keep candidate for Policy 2 (Capacity Cap)
                        residentInactivePages.push_back({ page, section->guid });
                    }
                }
            }
        };

        // Scan top-level sections
        for (const auto& section : activeNotebook->sections) {
            evaluateSectionPages(section);
        }

        // Scan sections within section groups and sub-groups
        for (const auto& group : activeNotebook->sectionGroups) {
            if (!group) continue;
            for (const auto& section : group->sections) {
                evaluateSectionPages(section);
            }
            for (const auto& subGrp : group->subGroups) {
                if (!subGrp) continue;
                for (const auto& sec : subGrp->sections) {
                    evaluateSectionPages(sec);
                }
            }
        }

        // Policy 2: Enforce Hard Capacity Cap on remaining resident pages
        size_t totalResident = residentInactivePages.size() + (activePage && activePage->isLoaded ? 1 : 0);
        if (maxLoadedPages > 0 && totalResident > maxLoadedPages) {
            // Sort resident inactive pages by lastAccessTimeMs ascending (least recently accessed first)
            std::sort(residentInactivePages.begin(), residentInactivePages.end(),
                      [](const ResidentEntry& a, const ResidentEntry& b) {
                          return a.page->lastAccessTimeMs < b.page->lastAccessTimeMs;
                      });

            for (auto& entry : residentInactivePages) {
                if (totalResident <= maxLoadedPages) break;
                if (entry.page && entry.page->isLoaded) {
                    if (entry.page->isModified) {
                        SavePageAsync(entry.page, entry.sectionGuid, entry.page->sortOrder);
                    }
                    entry.page->EvictFromRAM();
                    --totalResident;
                    LOG_INFO(PageRepository, "LRU [Capacity Cap Enforced]: Evicted page from RAM (" + 
                             std::to_string(totalResident) + "/" + std::to_string(maxLoadedPages) + 
                             " pages resident): " + entry.page->title + " (" + entry.page->guid + ")");
                }
            }
        }
    }

    // =========================================================================
    // RECYCLE BIN & 30-DAY RETENTION MANAGEMENT
    // =========================================================================

    /**
     * @brief Soft-deletes a page asynchronously in SQLite.
     *
     * WORKING PROCESS & INVARIANTS:
     * 1. Marks the page record in SQLite with deleted_at = current_unix_timestamp.
     * 2. Preserves the binary .ink file on disk untouched, guaranteeing full vector fidelity
     *    if restored within the 30-day recovery window.
     * 3. Dispatches the SQLite UPDATE asynchronously to avoid UI frame hitches.
     *
     * @param pageGuid Unique persistent GUID of the page to move to the recycle bin.
     * @return std::future<bool> Future resolving to true if soft-deletion succeeded.
     */
    std::future<bool> SoftDeletePageAsync(const std::string& pageGuid) {
        if (!dbManager || !dbManager->IsOpen()) {
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }
        auto db = dbManager;
        return GetGlobalThreadPool().Enqueue([db, pageGuid]() -> bool {
            return db->SoftDeletePage(pageGuid);
        });
    }

    /**
     * @brief Restores a soft-deleted page asynchronously.
     *
     * Invariants:
     * - Resets deleted_at to NULL in SQLite.
     * - The page becomes immediately retrievable via LoadPagesMetadata.
     *
     * @param pageGuid Unique persistent GUID of the page to restore.
     * @return std::future<bool> Future resolving to true on successful restoration.
     */
    std::future<bool> RestorePageAsync(const std::string& pageGuid) {
        if (!dbManager || !dbManager->IsOpen()) {
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }
        auto db = dbManager;
        return GetGlobalThreadPool().Enqueue([db, pageGuid]() -> bool {
            return db->RestorePage(pageGuid);
        });
    }

    /**
     * @brief Soft-deletes a section and cascades soft-deletion to all its child pages asynchronously.
     *
     * Mathematical Cascade:
     * When a section is soft-deleted at timestamp T, all contained pages are simultaneously
     * stamped with deleted_at = T, preserving hierarchical integrity.
     *
     * @param sectionGuid Unique persistent GUID of the section.
     * @return std::future<bool> Future resolving to true on success.
     */
    std::future<bool> SoftDeleteSectionAsync(const std::string& sectionGuid) {
        if (!dbManager || !dbManager->IsOpen()) {
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }
        auto db = dbManager;
        return GetGlobalThreadPool().Enqueue([db, sectionGuid]() -> bool {
            return db->SoftDeleteSection(sectionGuid);
        });
    }

    /**
     * @brief Restores a soft-deleted section and all its contained pages asynchronously.
     *
     * @param sectionGuid Unique persistent GUID of the section to restore.
     * @return std::future<bool> Future resolving to true on success.
     */
    std::future<bool> RestoreSectionAsync(const std::string& sectionGuid) {
        if (!dbManager || !dbManager->IsOpen()) {
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }
        auto db = dbManager;
        return GetGlobalThreadPool().Enqueue([db, sectionGuid]() -> bool {
            return db->RestoreSection(sectionGuid);
        });
    }

    /**
     * @brief Retrieves all soft-deleted sections currently residing in the notebook's recycle bin.
     * @param notebookGuid GUID of the notebook.
     * @return Vector of DBSectionRecord metadata with non-null deletedAt timestamps.
     */
    [[nodiscard]] std::vector<DBSectionRecord> LoadDeletedSections(const std::string& notebookGuid) const {
        if (!dbManager || !dbManager->IsOpen()) return {};
        return dbManager->LoadDeletedSections(notebookGuid);
    }

    /**
     * @brief Retrieves all soft-deleted pages currently residing in the notebook's recycle bin.
     * @param notebookGuid GUID of the notebook.
     * @return Vector of DBPageRecord metadata with non-null deletedAt timestamps.
     */
    [[nodiscard]] std::vector<DBPageRecord> LoadDeletedPages(const std::string& notebookGuid) const {
        if (!dbManager || !dbManager->IsOpen()) return {};
        return dbManager->LoadDeletedPages(notebookGuid);
    }

    /**
     * @brief Permanently purges a page from the database and removes its binary .ink file from disk.
     *
     * CRITICAL / NON-RECOVERABLE ACTION:
     * Dispatches unlinking of pages/{pageGuid}.ink and deletion from SQLite tables to worker thread.
     *
     * @param pageGuid GUID of the page to purge permanently.
     * @return std::future<bool> Future resolving to true on success.
     */
    std::future<bool> PermanentlyDeletePageAsync(const std::string& pageGuid) {
        if (!dbManager || !dbManager->IsOpen()) {
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }
        auto db = dbManager;
        std::string pkgPath = currentPackagePath;
        return GetGlobalThreadPool().Enqueue([db, pageGuid, pkgPath]() -> bool {
            return db->PermanentlyDeletePage(pageGuid, pkgPath);
        });
    }

    /**
     * @brief Permanently purges a section, its child pages, and their .ink files from disk.
     *
     * @param sectionGuid GUID of the section to purge permanently.
     * @return std::future<bool> Future resolving to true on success.
     */
    std::future<bool> PermanentlyDeleteSectionAsync(const std::string& sectionGuid) {
        if (!dbManager || !dbManager->IsOpen()) {
            std::promise<bool> p;
            p.set_value(false);
            return p.get_future();
        }
        auto db = dbManager;
        std::string pkgPath = currentPackagePath;
        return GetGlobalThreadPool().Enqueue([db, sectionGuid, pkgPath]() -> bool {
            return db->PermanentlyDeleteSection(sectionGuid, pkgPath);
        });
    }

    /**
     * @brief Empties all soft-deleted pages and sections from this notebook package asynchronously.
     *
     * @param notebookGuid GUID of the notebook whose recycle bin is being emptied.
     * @return std::future<size_t> Future resolving to the number of items permanently purged.
     */
    std::future<size_t> EmptyRecycleBinAsync(const std::string& notebookGuid) {
        if (!dbManager || !dbManager->IsOpen()) {
            std::promise<size_t> p;
            p.set_value(0);
            return p.get_future();
        }
        auto db = dbManager;
        std::string pkgPath = currentPackagePath;
        return GetGlobalThreadPool().Enqueue([db, notebookGuid, pkgPath]() -> size_t {
            return db->EmptyRecycleBin(notebookGuid, pkgPath);
        });
    }
};

} // namespace Folio
