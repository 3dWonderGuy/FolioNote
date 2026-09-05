# System Architecture Overview

FolioNote is engineered as a decoupled, multi-pass spatial engine designed for ultra-low latency inking, fluid infinite canvas navigation, and non-blocking background persistence.

---

## 🏛️ High-Level Design Principles

1. **Hardware-Decoupled Latency:** Hardware digitizer sampling (up to 480 Hz) is isolated from frame presentation pacing (120+ FPS) via a dedicated, immediate live layer.
2. **Readability & Maintainability First:** Clear geometric decomposition, explicit phase separation, and modern C++20 standard libraries are favored over brittle pointer micro-optimizations.
3. **Decoupled Spatial Indexing:** Spatial keys (bounding boxes) are held independently in a flat R-Tree, allowing cache-friendly viewport culling without touching heavy mesh memory.
4. **Local-First & Non-Blocking I/O:** Document hierarchy and spatial strokes are saved asynchronously using thread pools to avoid frame hitches.

---

## 🧩 Subsystem Decomposition

FolioNote is split into five core subsystems across the codebase:
```
┌────────────────────────────────────────────────────────┐
│                   Application Layer                    │
│   Window Management | DPI Handling | Theme Palette     │
└───────────┬────────────────────────────────┬───────────┘
│                                │
▼                                ▼
┌────────────────────────┐      ┌────────────────────────┐
│     Input Pipeline     │      │   Dear ImGui Frontend  │
│  State Machine & Stylus│      │  Toolbars, Hub & HUDs  │
└───────────┬────────────┘      └────────────┬───────────┘
│                                │
▼                                ▼
┌────────────────────────────────────────────────────────┐
│               Document Session (Source of Truth)       │
│    Workspace ──> Notebooks ──> Sections ──> Pages      │
└───────────┬────────────────────────────────┬───────────┘
│                                │
▼                                ▼
┌────────────────────────┐      ┌────────────────────────┐
│     Canvas Engine      │      │   Storage Subsystem    │
│  Blend2D + R-Tree Cull │      │  SQLite3 + Binary .ink │
└────────────────────────┘      └────────────────────────┘
```
---

## 📁 Source Code Organization

| Subsystem | Source Path | Core Responsibility |
|---|---|---|
| **App Coordination** | `src/app/` | Window lifecycles, OpenGL context, DPI events, frame pacing. |
| **Input Pipeline** | `src/input/` | Hardware telemetry, touch gestures, pen priority arbitration. |
| **Document Domain** | `src/core/document/` | Working set manager, notebook hierarchy, dirty flag tracking. |
| **Spatial Indexing** | `src/core/spatial/` | Axis-Aligned Bounding Box ($mm$) math and R-Tree culling. |
| **Object Registry** | `src/core/scene/` | Flat $O(1)$ UID-to-object store and runtime ID allocations. |
| **Canvas Engine** | `src/core/engine/` | Blend2D vector rasterization, live tail builder, GPU upload. |
| **Storage Engine** | `src/core/storage/` | SQLite database transactions and compressed `.ink` serializers. |
| **UI Components** | `src/ui/` | Navigation sidebar, top ribbon bar, canvas viewport wrapper. |

---

## 🧭 In-Depth Architecture Pages

* 📊 **[Runtime Component Graph](component-graph.md)** — Detailed class hierarchy, memory ownership, and lifetime models.
* 🔄 **[Data Flow & Pipelines](data-flow.md)** — Sequence diagrams for live inking, stroke commits, and render loops.
* 🖊️ **[Input Pipeline & Arbitration](input.md)** — Stylus priority arbitration, touch gesture decoding, and digitizer smoothing.
* 💾 **[Storage Model & Persistence](storage.md)** — `.notebook` bundle directory structure, SQLite relational schema, and compressed `.ink` binary streams
