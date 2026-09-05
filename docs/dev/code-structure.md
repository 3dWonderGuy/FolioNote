# Detailed Codebase Structure & Runtime Component Graph

This document provides a comprehensive map of FolioNote’s architectural layout, source directory responsibilities, class dependency hierarchy, and cross-subsystem execution lifecycles.

---

## 🏗️ Architectural Class & Component Graph

The engine enforces strict unidirectional data and control flow. High-level windowing and user interface layers interface exclusively through domain session facades, which coordinate geometry processing, spatial indexing, multi-threaded rendering, and asynchronous persistence.

```mermaid
classDiagram
    direction TB

    class Application {
        +WindowStateManager windowSM
        +ThemeManager themeManager
        +unique_ptr~InputManager~ inputManager
        +unique_ptr~CanvasEngine~ canvas
        +unique_ptr~DocumentSession~ session
        +Run()
        +ProcessEvents()
        +RenderFrame()
    }

    class InputManager {
        +InputStateMachine stateMachine
        +InputTracker telemetryTracker
        +ProcessSDLEvent(SDL_Event)
        +PollHighFrequencyDigitizer()
    }

    class InputStateMachine {
        -TouchGestureRecognizer gestureRecognizer
        -PenPalette palette
        +OnPointerDown()
        +OnPointerMove()
        +OnPointerUp()
    }

    class CanvasEngine {
        -CanvasTransform transform
        -LiveLayerPipeline liveLayer
        -InkingWorker inkingWorker
        -StrokeOutlineBuilder outlineBuilder
        -BLImage staticCanvasLayer
        -BLImage compositeSurface
        -GLuint glTexture
        +BakeStaticLayer(vector~CanvasObject~ visibleObjects)
        +RenderLiveTail(span~WorldPoint~ points)
        +UploadToOpenGL() GLuint
    }

    class DocumentSession {
        -Workspace workspace
        +CommitStroke(InkGeometry geom)
        +QueryVisible(AABB viewport) vector~CanvasObject~
        +GetActivePage() CanvasPage*
        +SaveWorkspace()
    }

    class Workspace {
        -PageRepository repository
        -DBManager dbManager
        -NotebookSearchIndex searchIndex
        -vector~shared_ptr~Notebook~~ notebooks
        +LoadNotebook(string path)
        +EvictLRUPages()
    }

    class CanvasPage {
        -string pageGuid
        -RTree spatialIndex
        -vector~shared_ptr~CanvasObject~~ objects
        -CommandHistory history
        -bool isModified
        +AddObject(shared_ptr~CanvasObject~ obj)
        +QuerySpatialBounds(AABB box) vector~uint32_t~
    }

    class CanvasObject {
        <<Polymorphic Interface>>
        +uint32_t uid
        +string guid
        +AABB boundingBox
        +virtual Render(BLContext& ctx) = 0
        +virtual HitTest(WorldPoint pt) bool = 0
    }

    class InkContainer {
        -BLPath bakedOutline
        -vector~StrokeSegment~ segments
        -vector~WorldPoint~ centerline
        +Render(BLContext& ctx)
        +HitTest(WorldPoint pt) bool
    }

    class PageRepository {
        -ThreadPool workerPool
        -FileSaver fileSaver
        -BinarySerializer serializer
        +EnqueuePageSave(shared_ptr~CanvasPage~ page)
        +LoadPageAsync(string pageGuid)
    }

    class DBManager {
        -sqlite3* dbHandle
        +ExecuteTransaction(string sql)
        +UpsertPageMetadata(PageMetadata meta)
    }

    class NotebookSearchIndex {
        +IndexText(string guid, string text, AABB bounds)
        +Search(string query) vector~SearchResult~
    }

    Application --> InputManager : Ingests Telemetry
    Application --> CanvasEngine : Coordinates Display Pipeline
    Application --> DocumentSession : Dispatches User Intent

    InputManager --> InputStateMachine : Dispatches Pointer Events
    InputStateMachine --> CanvasEngine : Directs Live Tail

    DocumentSession --> Workspace : Manages Working Set
    Workspace --> CanvasPage : Retains Active Canvases
    Workspace --> PageRepository : 4-Stage Async Save Queue
    Workspace --> DBManager : SQLite structure.db (WAL)
    Workspace --> NotebookSearchIndex : FTS5 Search & BM25

    CanvasPage --> CanvasObject : Stores Polymorphic Geometry
    CanvasObject <|-- InkContainer : Implements Vector Strokes
    CanvasPage --> RTree : Spatial Acceleration O(log N)
    
    CanvasEngine --> DocumentSession : Queries Viewport AABB
```

---

## 📂 Source Code Layout & Responsibilities

```text
FolioNote/
├── CMakeLists.txt                      # C++20, SDL3, Blend2D, ImGui, LunaSVG, SQLite3, Google Ink Stroke Modeler
├── assets/
│   └── icons/                          # SVG tool & navigation icons
│       ├── Navigation/                 # arrow-left.svg, arrow-right.svg
│       └── Sections_Notebooks/         # Color-coded notebook & section SVG icons
├── config/
│   └── theme_custom.json               # Exported/imported runtime theme configurations
├── third_party/
│   ├── SDL/                            # SDL3 core + OpenGL / GLES bindings
│   ├── blend2d/                        # 2D vector software rasterizer (JIT-accelerated)
│   ├── imgui/                          # Dear ImGui core + SDL3 / OpenGL3 / GLES3 backends
│   ├── lunasvg/                        # Vector icon rendering engine
│   ├── sqlite3/                        # Embedded metadata & FTS5 search storage engine
│   ├── ink-stroke-modeler/             # Google Ink Stroke Modeler (predictive & physical stroke smoothing)
│   ├── abseil-cpp/                     # Abseil C++ baseline library (required by ink-stroke-modeler)
│   ├── asmjit/                         # Just-in-time machine code generation for Blend2D
│   ├── SDL_image/                      # Image loading backend for textures/bitmaps
│   └── freetype/                       # Font rasterization engine
└── src/
    ├── main.cpp                        # Entry point: bootstrap runtime, engine init, main loop
    │
    ├── android-project/                # Android NDK / Gradle deployment package
    │   ├── app/                        # Android application module (Java SDLActivity, JNI, Manifest)
    │   └── gradle/                     # Gradle wrapper configuration
    │
    ├── app/                            # Top-level application coordinator
    │   ├── app.hpp                     # Owns window, GL context, sessions, 120Hz frame pacing, layout orchestration
    │   ├── theme_manager.hpp           # Runtime theme palette loader & JSON parser (Dark/Light/Custom)
    │   └── window_state_manager.hpp    # Window state, DPI scale events, frame pacing, freeze gate (Intel Arc mitigation)
    │
    ├── core/
    │   ├── spatial/                    # Pure spatial math & acceleration indices
    │   │   ├── aabb.hpp                # Axis-Aligned Bounding Box math (World Millimeters)
    │   │   ├── r_tree.hpp / .cpp       # Dynamic R-Tree spatial index for O(log N) viewport culling & hit-testing
    │   │   └── undo_redo_manager.hpp   # Spatial command / undo-redo manager
    │   │
    │   ├── objects/                    # Domain objects (Geometry payloads & bounds)
    │   │   ├── canvas_object.hpp       # Base polymorphic interface (UID, GUID, Type, Bounds, Transform, Flags)
    │   │   ├── ink_container.hpp       # Baked vector ink strokes (closed polygon hulls, centerline, hit-testing)
    │   │   ├── text_box.hpp            # Text container, formatting, caret layout, spatial bounds
    │   │   └── image_object.hpp        # Embedded image frame referencing GPU textures
    │   │
    │   ├── document/                   # Document hierarchy & session management
    │   │   ├── document_session.hpp    # Controller facade: routes stroke committals, queries, and active page
    │   │   ├── canvas_page.hpp         # Infinite canvas unit (owns R-Tree + Object list + History + Sub-pages + LRU cache)
    │   │   ├── section.hpp             # Group of canvas pages with sorting order and color tag
    │   │   ├── section_group.hpp       # Hierarchical folder structure for grouping sections and nested sub-groups
    │   │   ├── notebook.hpp            # Root container for sections, section groups, metadata, and .notebook package path
    │   │   └── workspace.hpp           # Working set manager tracking active notebooks, lazy loading, and LRU eviction
    │   │
    │   ├── engine/                     # Low-level rasterization & transform pipeline
    │   │   ├── canvas_engine.hpp       # Blend2D software context + OpenGL texture presentation (multi-layer)
    │   │   ├── canvas_transform.hpp    # Physical mm <-> Screen px transformations (Pan/Zoom/DPI)
    │   │   ├── inking_worker.hpp       # High-frequency async background inking thread with spring-damper physics
    │   │   ├── live_layer_pipeline.hpp # In-flight live stroke prediction, tip rendering, and lasso selection loop
    │   │   ├── stroke_outline_builder.hpp # Procreate/Google-grade 2D closed polygon ribbon builder with round/square caps
    │   │   └── stroke_smoother.hpp     # Multi-stage smoothing, Catmull-Rom splines, RDP decimation, velocity shaping
    │   │
    │   ├── search/                     # Full-text indexing & retrieval engine
    │   │   └── notebook_search_index.hpp / .cpp # SQLite FTS5 inverted search engine with BM25 ranking & spatial AABB coordinates
    │   │
    │   ├── storage/                    # Package Bundle & Persistence Engine
    │   │   ├── db_manager.hpp / .cpp   # SQLite connection (structure.db): Metadata, hierarchy, WAL mode, atomic transactions
    │   │   ├── page_repository.hpp     # 4-stage async persistence pipeline, background save queue, lazy loader, LRU eviction
    │   │   └── binary_serializer.hpp / .cpp # Binary serialization & zlib compression for .ink file generation
    │   │
    │   └── history/                    # Undo / Redo command pattern
    │       ├── canvas_command.hpp      # Base command interface (Execute, Undo, GetTargetBounds)
    │       └── command_history.hpp     # Isolated per-page undo/redo transaction stacks
    │
    ├── input/                          # Hardware input & gesture decoding
    │   ├── input_manager.hpp / .cpp    # SDL3 event receiver, high-frequency digitizer polling (240-480Hz)
    │   ├── input_state_machine.hpp / .cpp # Priority arbitration (Stylus > Touch > Mouse) and tool routing
    │   ├── input_tracker.hpp           # Raw telemetry structs (PenState, MouseState, TouchSlot, KeyState)
    │   ├── touch_gesture_recognizer.hpp# Direct manipulation decoder (Pinch-zoom, two-finger pan, tap, hold)
    │   └── pen_palette.hpp             # Active pen slot, color, tip shape, physical thickness, highlighter blending
    │
    ├── ui/                             # Dear ImGui frontend
    │   ├── imgui_theme.hpp             # Fluent theme tokens, modern typography, rounding rules
    │   ├── icon_manager.hpp            # LunaSVG rasterizer and OpenGL texture cache
    │   ├── components/
    │   │   ├── ribbon_bar.hpp          # Top toolbar (Home, Draw, Insert, View, Settings)
    │   │   ├── modern_nav_panel.hpp    # Dynamic collapsible 3-tier sidebar (Notebooks -> Section Groups -> Sections -> Pages)
    │   │   ├── notebook_nav.hpp        # Navigation bar component for section and page switching
    │   │   ├── text_container_view.hpp # In-place floating text editor for text box objects
    │   │   ├── debug_overlay.hpp       # [F3] Telemetry HUD (FPS, PCIe transfer, memory, sampling rate)
    │   │   ├── tuning_overlay.hpp      # [F5] Handwriting pipeline & stroke modeler live calibration studio
    │   │   └── dialogs.hpp             # Generic dialog modals & Theme Editor [F4]
    │   └── views/
    │       └── notebook_hub.hpp        # Fullscreen notebook gallery & package manager
    │
    └── utils/                          # Common utilities
        ├── file_loader.hpp             # Cross-platform read streams & safe buffer loading
        ├── file_logger.hpp             # Lock-free disk & UI ring-buffer logger
        ├── file_saver.hpp              # Cross-platform write streams & safe atomic directory creation
        ├── guid_generator.hpp          # RFC 4122 UUIDv4 generator for persistent entities
        ├── logger.hpp                  # Central logging macros (LOG_INFO, LOG_WARN, LOG_ERROR) routing to FileLogger
        ├── thread_pool.hpp             # Async worker pool for disk I/O, asset decoding, compression, and persistence
        └── uid_generator.hpp           # Atomic uint32_t ID generator for transient/runtime objects
```

---

## 📦 On-Disk Package Architecture (`.notebook` Bundle)

Every FolioNote document is stored as a self-contained directory bundle on disk, cleanly isolating relational metadata, full-text search indexes, compressed vector payloads, and external binary assets:

```text
┌────────────────────────────────────────────────────────────────────────┐
│ [NotebookName].notebook/                                               │
│   ├── structure.db                 (SQLite metadata, hierarchy & FTS5) │
│   ├── structure.db-wal             (SQLite Write-Ahead Log journal)    │
│   ├── pages/                                                           │
│   │   ├── {page-uuid-1}.ink        (ZSTD/zlib compressed vector payload)│
│   │   └── {page-uuid-2}.ink                                            │
│   └── imports/                                                         │
│       ├── pdfs/                    (Imported PDF reference documents)  │
│       └── images/                  (Imported high-res bitmap assets)   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## ⚡ Runtime Data Flow Pipelines

### 1. Live Inking Pass (240–480 Hz Input $\to$ 120 FPS Ingest)

```mermaid
flowchart TD
    HW["Hardware Digitizer (Stylus)"] -->|Raw SDL_Event: x, y, pressure, tilt| IM["InputManager.cpp"]
    IM --> ISM["InputStateMachine.cpp"]
    ISM -->|Intent: OnPointerMove| CE["CanvasEngine.cpp"]
    CE --> IW["InkingWorker.cpp / LiveLayerPipeline.cpp"]
    IW -->|Convert Screen Px -> World mm via CanvasTransform| SOB["StrokeOutlineBuilder.cpp"]
    SOB -->|Compute Closed 2D Ribbon Hull| LiveOut["Live Tail Buffer (Low Latency)"]
```

---

### 2. Stroke Committal Pass (Pen Lift)

```mermaid
flowchart TD
    Up["InputStateMachine (Detects Pen-Up)"] --> CEUp["CanvasEngine::OnPointerUp()"]
    CEUp -->|Completed Raw Stroke in mm| Commit["DocumentSession::CommitStroke()"]

    subgraph GeometryBake ["Bake & Modeling Pipeline"]
        Commit --> Model["StrokeSmoother / InkStrokeModeler"]
        Model --> Poly["StrokeOutlineBuilder (Builds Closed BLPath)"]
        Poly --> UID["UIDGenerator (Assigns uint32_t UID)"]
        UID --> InkObj["Instantiate InkContainer"]
    end

    InkObj --> PageAdd["CanvasPage::AddObject()"]
    PageAdd --> Vec["Append to Objects Vector"]
    PageAdd --> RTInsert["RTree::Insert(AABB, UID)"]
    RTInsert --> Dirty["CanvasEngine (Mark isDirty = true, needsFullRebake = true)"]
```

---

### 3. Frame Render Pass (120 FPS Composite)

```mermaid
flowchart TD
    App["Application / Main Loop"] --> ReqVP["1. Query Viewport AABB (mm) via CanvasEngine::GetViewport()"]
    ReqVP --> Query["2. DocumentSession::QueryVisible(viewport)"]
    Query --> RTreeCull["CanvasPage -> RTree Frustum Culling -> Resolves Visible CanvasObjects in O(log N)"]
    
    RTreeCull --> Render["CanvasEngine::Render(visibleObjects)"]
    
    subgraph MultiLayerComposite ["Multi-Layer Composition"]
        Render --> Static["Static Layer (Rebake via Blend2D if isDirty)"]
        Render --> Dynamic["Live Layer (Blits Static Layer + Live Inking Tail + Lasso Overlay)"]
    end

    Dynamic --> Upload["GPU Upload (glTexSubImage2D PRGB32 pixels to OpenGL / GLES Texture)"]
    Upload --> ImGuiDraw["Dear ImGui Canvas Window (ImGui::Image)"]
    ImGuiDraw --> Swap["SDL_GL_SwapWindow()"]
```

---

### 4. Storage & Persistence Pass (Asynchronous 4-Stage Save)

```mermaid
flowchart TD
    Trigger["Workspace / PageRepository (Page Modified or LRU Eviction Requested)"] --> S1["Stage 1: Metadata Snapshot (Captured synchronously on main thread)"]
    S1 --> S2["Stage 2: Binary Serialization (BinarySerializer compresses vector objects into buffer)"]
    S2 --> S3["Stage 3: Enqueue Job to Background ThreadPool Worker"]
    
    subgraph DiskThread ["Background ThreadPool Execution"]
        S3 --> S4A["FileSaver: Write compressed stream to .notebook/pages/{GUID}.ink"]
        S3 --> S4B["DBManager: Atomic SQLite upsert to structure.db in WAL mode"]
    end
```

---

### 5. Search & Spatial Retrieval Pass

```mermaid
flowchart TD
    UI["Ribbon / Global Search UI (User inputs keyword, tag, or phrase)"] --> Exec["NotebookSearchIndex::Search()"]
    Exec --> FTS["Execute SQLite FTS5 MATCH Query with BM25 Ranking"]
    FTS --> Res["Result Set: Page GUID, Section Title, Highlighted Text Snippet, World AABB"]
    Res --> Cam["CanvasTransform / Camera Pan & Zoom: Smoothly animate viewport to Target AABB"]
```
