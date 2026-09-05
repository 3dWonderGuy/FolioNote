---
title: Home
---

# FolioNote

**FolioNote** is a cross-platform, infinite-canvas note-taking engine and document workspace built in C++20. Engineered from the ground up for low-latency active stylus input, fluid infinite navigation, and rich spatial organization, it bridges the tactile immediacy of physical paper with the boundless flexibility of a digital whiteboard.

---

## 💡 What is FolioNote?

Traditional note-taking tools force your thoughts into rigid constraints: fixed A4 page boundaries, sluggish web-based raster engines, proprietary cloud locks, or sluggish stylus tracking.

**FolioNote changes this paradigm:**

* **Boundless Spatial Freedom:** An infinite 2D canvas with seamless pan and zoom capabilities, free from artificial page margins or canvas borders.
* **Direct-to-Hardware Inking:** Built in native modern C++20 to bypass heavy web runtimes (Electron/WebTech), achieving native 120+ FPS canvas rendering and direct 480 Hz digitizer polling.
* **Organized Hierarchy:** Combines freeform spatial canvases with a structured document model—organizing your work into **Notebooks**, **Sections**, and **Pages**.
* **Open & Local-First:** Your data belongs entirely to you. FolioNote operates locally with lightweight file package bundles (`.notebook`) combining open SQLite metadata with compressed vector stroke streams.

---

## ✨ Why It’s Unique

<div class="grid cards" markdown>

-   :material-feather: **Ultra-Low Latency Inking**
    
    ---
    
    A dedicated live-inking tail renders in-flight stylus inputs ahead of the main scene baking pass, giving immediate, tactile feedback with zero perceptible lag.

-   :material-vector-polyline: **Fluid Geometric Smoothing**
    
    ---
    
    Raw digitizer telemetry is processed through real-time noise decimation and centripetal Catmull-Rom splines, creating crisp, natural vector strokes that look sharp at any zoom level.

-   :material-lightning-bolt: **R-Tree Spatial Indexing**
    
    ---
    
    Canvases scale indefinitely without frame drops. Viewport queries run in $O(\log N)$ time, selectively drawing only what is on-screen even on massive documents with tens of thousands of strokes.

-   :material-cellphone-link: **True Cross-Platform Engine**
    
    ---
    
    A unified C++ core driving desktop environments via SDL3/OpenGL and mobile tablet devices via the Android NDK, ensuring identical rendering fidelity everywhere.

</div>

---

## 🛠️ Technology Stack at a Glance

| Layer | Technology | Purpose |
|---|---|---|
| **Language Core** | C++20 | Native performance, memory safety via RAII, and deterministic execution. |
| **Raster Engine** | Blend2D | High-speed, JIT-accelerated software 2D vector graphics. |
| **User Interface** | Dear ImGui | Immediate-mode UI orchestration, telemetry HUDs, and modular toolbars. |
| **Windowing & GL** | SDL3 + OpenGL 3.3 | Cross-platform event ingestion, DPI awareness, and frame presentation. |
| **Data & Storage** | SQLite3 + zlib | Local-first relational document hierarchy and compressed vector binary storage. |

---

## 🚀 Explore the Documentation

<div class="grid cards" markdown>

-   :material-layers-triple: **[Architecture Overview](architecture/overview.md)**
    
    ---
    
    Discover the internal subsystems, decoupled execution passes, and the component graph.

-   :material-calculator-variant: **[Mathematics & Foundations](math/overview.md)**
    
    ---
    
    Dive into the geometry behind stroke polygon construction, spline smoothing, and R-Tree spatial partitioning.

-   :material-hammer-wrench: **[Build & Setup Guide](dev/build.md)**
    
    ---
    
    Step-by-step instructions for compiling FolioNote on desktop systems and Android targets.

-   :material-git: **[Contributing Guidelines](dev/contributing.md)**
    
    ---
    
    Branching rules, nametag standards, and pull request workflows targeting the `develop` branch.

</div>
