# FolioNote

FolioNote is a high-performance, low-latency infinite-canvas note-taking application written in C++20. Designed for sustained 120+ FPS rendering and responsive stylus inking, it combines continuous geometric smoothing, dynamic spatial bounding acceleration, and an encapsulated document package model across desktop platforms and Android.

---

## ⚡ Key Capabilities

* **Predictive & Continuous Inking:** High-frequency (240–480 Hz) digitizer polling with Google Ink Stroke Modeler spring-damper dynamics, Centripetal Catmull-Rom splines, and Ramer-Douglas-Peucker (RDP) decimation.
* **Vector Rasterization:** JIT-accelerated 2D closed polygon ribbon rendering via Blend2D, composited with active in-flight ink tails and blitted directly to OpenGL / OpenGL ES surfaces.
* **Spatial Acceleration Index:** Custom 2D R-Tree providing $O(\log N)$ frustum culling, lasso selection intersection, and sub-millisecond viewport queries over tens of thousands of objects.
* **Hardware Arbitration & Palm Rejection:** Multi-tier hardware state machine with automatic stylus-over-touch priority, 80ms noise cooldown hysteresis, and Dear ImGui UI focus locking.
* **Hybrid Package Architecture:** Fully self-contained `.notebook` directory bundles containing SQLite3 relational metadata, SQLite FTS5 inverted search with BM25 ranking, and zlib/ZSTD-compressed `.ink` binary vector streams.

---

## 🏗️ Architecture & Component Layout

```
Application (src/app/app.hpp)
├── WindowStateManager ──► DPI scaling, 120 Hz frame pacing, Intel Arc swapchain freeze gate
├── InputManager ────────► InputStateMachine ──► Stylus / Touch / Mouse Priority Decoders
├── CanvasEngine ────────► InkingWorker ──────► Blend2D Vector Context + OpenGL Composite
├── DocumentSession ─────► Workspace ─────────► PageRepository + R-Tree Spatial Acceleration
└── UI Subsystem ────────► Dear ImGui Docking ─► LunaSVG Icon Renderer & Dynamic Themes
```

For full architectural breakdowns, mathematical proofs, and pipeline diagrams, visit the [FolioNote Documentation Site](https://3dwonderguy.github.io/FolioNote/).

---

## 📦 Vendored Tech Stack

All core dependencies are vendored directly in `third_party/` to guarantee zero package drift and reproducible offline builds:

| Component | Library / Engine | Purpose |
|---|---|---|
| **Core Language** | C++20 (`cxx_std_20`) | Zero-cost abstractions, RAII lifecycles, memory safety |
| **Windowing & Input** | SDL3 | Multi-backend window creation, raw digitizer & multi-touch telemetry |
| **Vector Engine** | Blend2D + AsmJit | High-speed 2D software rasterizer with dynamic JIT compilation |
| **Stroke Physics** | Google Ink Stroke Modeler + Abseil | Physical drag/spring smoothing and real-time path prediction |
| **UI Framework** | Dear ImGui (Docking) | Fluent desktop ribbons, collapsibles, and debug telemetry overlays |
| **Icon Pipeline** | LunaSVG | High-resolution scalable vector icons with OpenGL texture caching |
| **Storage & Search** | SQLite3 (WAL mode) | Relational document hierarchy and FTS5 full-text indexing |
| **Vector Serializer** | zlib / ZSTD | Binary `.ink` closed polygon payload compression |

---

## 📁 Repository Layout

```text
FolioNote/
├── CMakeLists.txt              # Cross-platform build script (C++20, static submodules)
├── assets/                     # SVG navigation, notebook, and tool icons
├── config/                     # Runtime JSON theme definitions
├── docs/                       # MkDocs Material technical documentation source
├── third_party/                # Statically linked vendor dependencies
└── src/
    ├── main.cpp                # Runtime bootstrap and SDL3 main entry point
    ├── android-project/        # Gradle / Android Studio NDK deployment project
    ├── app/                    # Application coordinator, window states, and theme manager
    ├── core/
    │   ├── document/           # Document session, workspace, notebook, and page hierarchies
    │   ├── engine/             # Canvas rasterizer, transforms, and inking worker threads
    │   ├── geometry/           # Splines, RDP decimation, and stroke outline builders
    │   ├── history/            # Isolated per-page undo/redo command transaction stacks
    │   ├── objects/            # CanvasObject domain types (InkContainer, TextBox, ImageObject)
    │   ├── search/             # SQLite FTS5 inverted search engine with BM25 scoring
    │   ├── spatial/            # Axis-Aligned Bounding Box (AABB) math and 2D R-Tree index
    │   └── storage/            # 4-stage async save pipeline, DBManager, and binary serializer
    ├── input/                  # Digitizer telemetry, gesture recognizers, and state arbitration
    ├── ui/                     # Dear ImGui panels, ribbons, sidebars, and calibration overlays
    └── utils/                  # Thread pools, RFC 4122 GUID generation, and atomic UID allocators
```

---

## 🗄️ On-Disk Package Model (`.notebook`)

Documents are stored as portable folder bundles containing isolated metadata and binary assets:

```text
[NotebookName].notebook/
├── structure.db                 # SQLite metadata, hierarchy & FTS5 full-text index
├── structure.db-wal             # SQLite Write-Ahead Log journal
├── pages/
│   ├── {page-uuid-1}.ink        # Compressed binary vector stroke stream
│   └── {page-uuid-2}.ink
└── imports/
    ├── pdfs/                    # Imported PDF documents
    └── images/                  # Bitmap attachments
```

---

## 🛠️ Building from Source

### Prerequisites

* **CMake** >= 3.22
* **Ninja** build system (recommended)
* **C++20 compliant compiler:**
  * Windows: MSVC 19.30+ (Visual Studio 2022) or Clang-CL
  * Linux: GCC >= 12 or Clang >= 15
  * macOS: Apple Clang >= 15
  * Android: Android NDK r25c+

### Desktop Compilation (Linux, Windows, macOS)

```bash
# 1. Clone repository with all vendored submodules
git clone --recursive [https://github.com/3dwonderguy/FolioNote.git](https://github.com/3dwonderguy/FolioNote.git)
cd FolioNote

# 2. Configure build tree
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 3. Compile binary
cmake --build build --config Release -j$(nproc)

# 4. Launch FolioNote
./build/bin/FolioNote
```

### Android Build (`libmain.so`)

1. Open `/android-project` or the project root inside **Android Studio**.
2. Verify `local.properties` contains your Android SDK and NDK paths:
   ```properties
   sdk.dir=/path/to/Android/Sdk
   ndk.dir=/path/to/Android/Sdk/ndk/25.x.x
   ```
3. Assemble the build:
   ```bash
   ./gradlew assembleRelease
   ```

---

## 📄 License

FolioNote is distributed under the [MIT License](LICENSE).
