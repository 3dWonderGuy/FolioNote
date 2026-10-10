# About FolioNote

FolioNote is a modern pen-first note-taking software aiming to be completely cross-platform compatible, user-customizable, and intuitive. Built for the lowest possible pen latency, with infinite canvas support, a dedicated PDF reader, and an upcoming integrated Markdown editor. This software aims to become a capable alternative to known note-taking applications like GoodNotes, OneNote, or Samsung Notes.

---

## ⚠️ Early Development & Alpha Status Warning

> **Use at Your Own Risk:** FolioNote is currently in an active **Alpha** development phase. The core architecture, database schemas, and on-disk file formats are evolving rapidly.
>
> * **Data Safety:** **Do not TRUST IT. ** While I made sure that storage managers and backup managers are fail-safe, this application has to first pass the beta phase and be fully tested to remove any chances of permanent user data loss. Currently, the backup manager creates monthly, weekly, and daily backups deep in operating system folders. There is also page version history tracking, sort of similar to Google Docs.
> * Please always keep external backups of your data.

---

## 🎓 Built for Students & Visual Thinkers

FolioNote is designed for everyone to use:
* Best for creating all kinds of notes, typing, and inking, or just as a well-organized PDF editor and annotator.

---

## 💡 Feature Requests & Community Feedback

While the public GitHub repository is being structured for formal issue tracking and pull requests, we are collecting feedback, bug reports, and roadmap requests directly via Google Forms and live community tracking sheets:

* 📝 **[Submit a Feature Request (Google Form)](https://docs.google.com/forms/d/e/1FAIpQLScfGA2J0oJ1Z1QIIk9O9DbA_I8v5NRboZD7fWB_C-n-vjFH6Q/viewform)** *(Replace with your Google Form URL)*
* 📊 **[View the Public Roadmap & Request Tracker (Google Sheet)](https://docs.google.com/spreadsheets/d/1HbjkbjFGxSBP-3_nPj3hyaNFvNMIcEM9qTW_fi1OgAY/edit?usp=sharing)** *(Replace with your Google Sheet URL)*

---

##  Core Features & Technical Highlights

###  Handwriting & Inking Engine
* **High-Frequency Telemetry:** Ingests hardware digitizer events at 240–480 Hz with sub-frame presentation.
* **Physics-Based Smoothing:** Customizable pen physics

###  Global Instant Search (UNDER DEVELOPMENT)
* **Embedded SQLite FTS5 Engine:** Full-text search index built directly into the `.notebook` package bundle.
* **BM25 Relevance Scoring:** Instant search across typed text boxes, titles, tags, and spatial metadata.
* **Spatial Coordinate Navigation:** Selecting a search result automatically animates the camera viewport directly to the object's exact world-space $(x, y)$ coordinates.

### 🏛️ Document & Notebook Hierarchy (Local-First)

FolioNote is engineered around a local-first, multi-tier document model designed for instant navigation, data sovereignty, and zero cloud dependency:

```text
Library (.foliolib folder / library.meta)
 └── Notebook ([Name].notebook package bundle + structure.db)
      ├── Section Group (Optional nested directory folders for macro organization)
      │    └── Section (Thematic tabs: e.g., "Lectures", "Homework")
      └── Section
           └── Canvas Page (2D infinite canvas surface + independent R-Tree index)
                ├── Subpage (Level 1: nested under parent page)
                │    └── Sub-subpage (Level 2: deep-nested under subpage)
                └── Canvas Objects (Ink, text boxes, images, videos, shapes, PDFs, web embeds)
```

| Entity | Representation & Storage | Description & Invariants |
|---|---|---|
| **Library** | Root directory (`.foliolib` / `library.meta`) | Multi-library workspace container (e.g., *Personal*, *University*, *Work*). Contains notebooks and an isolated non-destructive `.trash/` recycle bin quarantine. |
| **Notebook** | Portable package bundle (`.notebook`) | Standalone folder bundle containing relational metadata (`structure.db`), binary stroke files (`pages/`), and media attachments. Enforces zero-null invariant (always instantiates with a default section). |
| **Section Group** | Relational tree node (`section_groups` table) | Collapsible folder containers for grouping sections (e.g., "Year 1", "Semester A"). Supports recursive nested hierarchies, custom colors, and fold states. |
| **Section** | Thematic tab entity (`sections` table) | Tab-based organization unit. Enforces zero-null invariant (always instantiates with at least one default page). Supports drag-and-drop ordering, soft deletion, and individual palette colors. |
| **Canvas Page** | Independent canvas (`pages` table + `.ink` file) | 2D vector drawing canvas with full transform camera ($x, y, \text{zoom}$), independent undo/redo stack, and spatial R-Tree index. Supports standard ink canvases as well as dedicated standalone PDF and Markdown documents. |
| **2-Level Subpages** | Hierarchical page nesting | OneNote-style indented subpage hierarchy supporting 2 levels of nesting (`level 0`: root page, `level 1`: subpage, `level 2`: sub-subpage) with collapsible parent groups. |
| **Canvas Objects** | Polymorphic `CanvasObject` hierarchy | Positioned in world coordinates: `InkContainer` (vector ink strokes & closed polygon fills), `TextBox` (rich text & typography), `ImageObject` (raster bitmaps), `VideoObject` (libVLC player), `PDFObject` (PDFium annotated sheets), `ShapeObject` (geometric primitives), and `WebEmbedObject` (interactive web & YouTube embeds). |

---

## 🗄️ On-Disk Package Model & Storage Architecture

Each FolioNote notebook is encapsulated as an open, cross-platform directory package bundle (`[NotebookName].notebook`):

```text
[LibraryName].foliolib/
├── library.meta                     # Library identification, name, and version descriptor
├── .trash/                          # Non-destructive recycle bin quarantine directory
└── [NotebookName].notebook/         # Self-contained notebook package bundle
    ├── desktop.ini                  # Windows Shell branding (custom icon & folder tooltip)
    ├── structure.db                 # SQLite database (WAL mode: metadata, hierarchy, FTS5 search)
    ├── structure.db-wal             # SQLite high-concurrency Write-Ahead Log journal
    ├── structure.db-shm             # SQLite shared-memory index
    ├── pages/                       # Isolated per-page binary vector stroke payloads
    │   ├── {page-uuid-1}.ink        # Compressed vector graphics payload (magic 'FINK' header)
    │   ├── {page-uuid-1}.ink.wal    # Uncommitted action journal for instant crash recovery
    │   └── {page-uuid-2}.ink
    └── imports/                     # Managed external asset attachments
        ├── pdfs/                    # Imported reference PDF documents and backing files
        │   └── {asset-uuid}.pdf
        ├── images/                  # High-resolution raster images (PNG, JPEG, WebP)
        │   └── {asset-uuid}.png
        └── media/                   # Embedded video and audio assets for canvas playback
```

### Storage Engine Invariants & Pipeline

1. **Hybrid Storage Architecture:** Lightweight relational hierarchy, section trees, tags, and full-text search indexes are maintained in SQLite (`structure.db`). Heavy vector stroke geometry and object payloads reside in isolated `.ink` binary files, allowing notebooks with thousands of pages to open instantly.
2. **Asynchronous 4-Stage Persistence Pipeline (`PageRepository`):**
   $$\text{[UI Thread]} \xrightarrow{\text{1. Snapshot}} \text{[Memory Serialization]} \xrightarrow{\text{2. ThreadPool Queue}} \text{[Worker Threads]} \xrightarrow{\text{3. Atomic Disk IO + SQLite WAL Upsert}}$$
   Disk writes utilize two-phase staging (`.tmp` files with physical `fsync` / `_commit` flushing) to guarantee zero file corruption during unexpected power loss or crashes.
3. **Dual-Axis LRU Cache Eviction:** Inactive pages in memory exceeding idle thresholds (60s) or memory capacity limits are flushed to disk (if dirty) and automatically evicted from RAM (`EvictFromRAM()`). Navigating back transparently reloads them on-demand.
4. **Crash-Resilient Write-Ahead Logging:** In-flight pen strokes append immediate incremental updates to per-page `.ink.wal` journals, enabling zero-loss recovery in the event of an abnormal termination.

---

For full architectural breakdowns, mathematical proofs, and pipeline diagrams, visit the [FolioNote Documentation Site](https://3dwonderguy.github.io/FolioNote/).

---

## 📦 Vendored Tech Stack

All core third-party dependencies are vendored directly in `third_party/` to guarantee zero package drift, hermetic offline compilation, and maximum runtime performance:

| Component | Library / Engine | Purpose & Integration Details |
|---|---|---|
| **Core Language** | **C++20** (`cxx_std_20`) | RAII lifetime management, concepts, standard `<filesystem>`, `<chrono>`, `<format>`, and zero-cost abstractions across all platforms. |
| **Windowing & Input** | **SDL3** | Multi-backend window creation, raw hardware digitizer & stylus telemetry (pressure, tilt, distance at 240–480 Hz), multi-touch gestures, and audio. |
| **Image Decoding** | **SDL_image** (SDL3_image) | High-performance image ingestion and decoding supporting PNG, WebP, JPEG, AVIF, TIFF, and BMP formats. |
| **2D Vector Engine** | **Blend2D** | High-performance 2D vector software rasterizer utilizing dynamic JIT compilation for multithreaded rendering. |
| **JIT Compiler** | **AsmJit** | Dynamic machine code generation library powering Blend2D's high-speed pipeline compilation on x86/x64 architectures. |
| **Stroke Physics** | **Google Ink Stroke Modeler** | Physics-based stroke smoothing using mass-drag-spring differential equations, velocity-based line weight modulation, and real-time path prediction. |
| **Algorithm Toolkit** | **Abseil-cpp** | Foundational Google C++ algorithms, containers, and mathematical primitives powering the Ink Stroke Modeler pipeline. |
| **UI Framework** | **Dear ImGui (Docking)** | Immediate-mode desktop UI framework powering custom ribbon bars, sidebars, dockable panels, theme styling, and telemetry debug overlays. |
| **Vector Icons & SVG** | **LunaSVG & PlutoVG** | Resolution-independent SVG parsing and rasterization with OpenGL texture caching for UI ribbons, toolbar icons, and vector exports. |
| **Database & Search** | **SQLite3 (WAL + FTS5 + JSON1)** | Embedded transactional storage for document hierarchies, table schemas, metadata, and full-text search with BM25 relevance ranking. |
| **PDF Engine** | **Google PDFium** | High-fidelity native PDF document parser, page renderer, text selection/search engine, and bookmark extraction subsystem. |
| **Media Subsystem** | **libVLC SDK** | Cross-platform hardware-accelerated video and audio decode pipeline enabling native playback inside canvas `VideoObject` containers. |
| **Web Embeds** | **WebView (WebView2)** | Lightweight embedded browser integration for live interactive web embeds, online documentation, and embedded YouTube video players. |
| **Typography & Fonts** | **FreeType 2** | High-grade TrueType/OpenType font rasterization, subpixel anti-aliasing, and typography metrics for text elements. |
| **Configuration Store** | **nlohmann/json** | Header-only JSON serialization engine managing application settings, pen tool presets, usage statistics, and color themes. |

---

## 📁 Repository Layout

```text
FolioNote/
├── CMakeLists.txt                  # Root CMake build specification (C++20, static vendoring, MSVC/Clang/GCC)
├── assets/                         # Packaged runtime assets
│   ├── fonts/                      # UI typography and icon fonts
│   └── icons/                      # Scalable vector graphics (SVG) toolbar and navigation icons
├── config/                         # Application runtime configuration
│   ├── settings.json               # Persistent user preferences, tool presets, and canvas options
│   └── usage_stats.json            # Local privacy-preserving usage and telemetry metrics
├── docs/                           # Technical documentation website source (MkDocs Material)
├── installer/                      # Windows installer manifests and deployment scripts
├── test/                           # CTest automated test suite
│   ├── test_action_scheduler.cpp   # Asynchronous action queue and scheduled task tests
│   ├── test_file_manager.cpp       # Atomic file operations and path resolution tests
│   ├── test_file_saver.cpp         # Two-phase file saving and physical sync verification
│   ├── test_layer_order.cpp        # Canvas z-ordering and layer stacking tests
│   ├── test_physics_model.cpp      # Stylus velocity, acceleration, and spring physics tests
│   ├── test_rtree.cpp              # 2D R-Tree spatial partitioning and query benchmarks
│   ├── test_storage.cpp            # SQLite schema migration and .ink binary serialization tests
│   └── test_thread_pool.cpp        # Concurrency and worker thread pool stress tests
├── third_party/                    # Statically linked vendored dependencies
│   ├── SDL/                        # SDL3 windowing, events, and raw digitizer telemetry
│   ├── SDL_image/                  # SDL3 image format decoding library
│   ├── abseil-cpp/                 # Google Abseil foundation library
│   ├── asmjit/                     # Dynamic JIT assembler for Blend2D
│   ├── blend2d/                    # High-performance 2D vector software rasterizer
│   ├── freetype/                   # FreeType 2 font engine
│   ├── imgui/                      # Dear ImGui (Docking branch) immediate-mode UI
│   ├── ink-stroke-modeler/         # Google Ink physical smoothing and prediction
│   ├── libvlc/                     # libVLC media player SDK (Windows/Linux playback)
│   ├── lunasvg/                    # LunaSVG & PlutoVG vector graphics rendering
│   ├── nlohmann/                   # nlohmann/json modern C++ JSON parser
│   ├── pdfium/                     # Google PDFium native PDF engine
│   ├── sqlite3/                    # SQLite3 embedded database (WAL, FTS5, JSON1)
│   └── webview/                    # Lightweight webview wrapper for web embeds
└── src/
    ├── main.cpp                    # Application bootstrap, CLI argument handling, SDL3 entry point
    ├── folionote.rc                # Win32 application resource definition and icon manifest
    ├── android-project/            # Gradle & Android Studio NDK deployment project
    ├── app/                        # Application coordinator, lifecycle, and desktop window state
    │   ├── actions/                # Global UI action bindings and shortcuts
    │   ├── app.hpp                 # Central application controller and master event loop
    │   ├── context_menu_manager.hpp# Fluent right-click context menu dispatcher
    │   ├── settings_manager.hpp    # Persistent user preferences coordinator
    │   ├── theme_manager.hpp       # Palette themes, dark/light mode, and styling tokens
    │   └── window_state_manager.hpp# Window geometry, DPI scaling, and multi-monitor placement
    ├── core/                       # Core domain logic, rendering engines, and persistence
    │   ├── actions/                # ActionScheduler, undoable user commands, and macro transactions
    │   ├── backup/                 # Fail-safe backup manager (monthly, weekly, daily snapshots, pruning)
    │   ├── canvas_engine/          # Canvas rasterization, viewport transforms, camera LOD, tile grid
    │   ├── clipboard/              # System clipboard bridge and cross-page object serialization
    │   ├── document/               # DocumentSession, Workspace, Notebook, Section, Page models
    │   │   ├── library/            # Multi-library discovery (.foliolib), quarantine (.trash), cloning
    │   │   └── session/            # DocumentSession modular facades (navigation, canvas ops, history)
    │   ├── export/                 # Exporters for PDF, SVG, PNG, and portable .fnb packages
    │   ├── history/                # Per-page isolated CommandHistory undo/redo stacks
    │   ├── import/                 # Importers for external PDFs, images, and .fnb archives
    │   ├── ink_engine/             # Digitizer stroke smoothing, pressure curves, and spline generation
    │   ├── layers/                 # Canvas layer manager, stacking order, visibility, and locking
    │   ├── md_engine/              # Integrated Markdown parser, AST renderer, and document viewer
    │   ├── objects/                # Polymorphic CanvasObject classes (Ink, Text, Image, Video, Shape, PDF, Web)
    │   ├── overlay/                # Interactive canvas overlays (selection boxes, transform gizmos, guides)
    │   ├── pdf_engine/             # Google PDFium wrapper, PDF rendering, annotations, and bookmarks
    │   ├── search/                 # SQLite FTS5 full-text index with BM25 scoring and spatial navigation
    │   ├── spatial/                # 2D R-Tree spatial partitioning index and AABB bounding box math
    │   ├── storage/                # PageRepository, DBManager (SQLite WAL), and BinarySerializer (.ink)
    │   └── text/                   # FontManager, glyph texture caching, and typography layout
    ├── input/                      # Hardware digitizer telemetry, touch gestures, and pen presets
    │   ├── stateMachine/           # Inking state machine (hover, drawing, erasing, selecting)
    │   ├── input_manager.cpp       # SDL3 event dispatcher, digitizer arbitration, and touch filtering
    │   ├── pen_palette.hpp         # Quick-access pen tools, highlighters, and color slots
    │   ├── preset_manager.hpp      # Tool presets (stroke thickness, opacity, smoothing factors)
    │   └── touch_gesture_recognizer.hpp # Multi-touch pinch-to-zoom, two-finger pan, and rotation
    ├── io/                         # Cross-platform filesystem abstraction and atomic persistence
    │   ├── app_directories.cpp     # Resolution of document roots, config, cache, and log paths
    │   ├── file_manager.cpp        # Facade for directory operations, copies, moves, and deletions
    │   ├── file_reader.cpp         # Zero-copy memory-mapped file access (mmap) and hashing
    │   ├── file_writer.cpp         # Two-phase atomic write staging (.tmp) and physical drive sync
    │   ├── package_marker.hpp      # Windows Shell package branding (desktop.ini attributes & icons)
    │   ├── path_utils.cpp          # UTF-8/UTF-16 Unicode conversion and long path (\\?\) handling
    │   └── system_dialogs.cpp      # Native OS file dialogs (Win32, Linux, macOS)
    ├── ui/                         # Dear ImGui presentation layer
    │   ├── components/             # Reusable UI widgets (color wheels, sliders, custom buttons)
    │   ├── framework/              # UI builder, sidebar layout, ribbon toolbar, docking chrome
    │   ├── overlays/               # Floating HUDs, tool pickers, zoom indicators, global search modal
    │   ├── shell/                  # Custom window title bar, status bar, and dialog popups
    │   └── views/                  # Canvas viewports, library organizer view, and settings panel
    └── utils/                      # Thread-safe utilities and foundation helpers
        ├── error_codes.hpp         # Standardized error code enumeration and diagnostics
        ├── guid_generator.hpp      # RFC 4122 Version 4 UUID generation
        ├── logger.hpp              # Thread-safe multi-sink console and file logger
        ├── physics_model.hpp       # Kinematic physics calculations for canvas camera smoothing
        ├── thread_pool.hpp         # Work-stealing background thread pool for async I/O and indexing
        ├── uid_generator.hpp       # Monotonic atomic object UID allocator
        └── usage_tracker.hpp       # Local opt-in performance telemetry and usage tracking
```


## 📄 License

FolioNote is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0).
