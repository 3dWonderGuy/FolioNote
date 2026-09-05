---
title: Home
---

# FolioNote

**FolioNote** is a cross-platform, infinite-canvas note-taking engine and document workspace built in C++20. Engineered from the ground up for low-latency active stylus input, fluid infinite navigation, and rich spatial organization. Perfect tool for anyone wanting to take digital notes of any kind.

---

## 💡 What is FolioNote?

Traditional note-taking tools force your thoughts into rigid constraints: fixed A4 page boundaries, sluggish web-based raster engines, proprietary cloud locks, or sluggish stylus tracking.

**FolioNote changes this paradigm:**

* **Boundless Spatial Freedom:** An infinite 2D canvas with seamless pan and zoom capabilities, free from artificial page margins or canvas borders.
* **Direct-to-Hardware Inking:** Built in native modern C++20 to bypass heavy web runtimes (Electron/WebTech), achieving native 120+ FPS canvas rendering and direct 480 Hz digitizer polling.
* **Organized Hierarchy:** Combines freeform spatial canvases with a structured document model—organizing your work into **Notebooks**, **Sections**, and **Pages**.
* **Open & Local-First:** Your data belongs entirely to you. FolioNote operates locally with lightweight file package bundles (`.notebook`) combining open SQLite metadata with compressed vector stroke streams.

---

## ✨ What Makes FolioNote Special

<div class="grid cards" markdown>

-   :material-pen: **Paper-Like Inking**
    
    ---
    
    Writing feels natural, instant, and fluid. The pen engine responds to your stylus with zero noticeable delay and smooths out jitters so handwriting always looks crisp and clean.

-   :material-folder-multiple: **Effortless Organization**
    
    ---
    
    Keep your thoughts structured without losing flexibility. Group related ideas into clear **Notebooks**, **Sections**, and **Pages**, making even massive projects easy to navigate.

-   :material-magnify: **Notebook-Wide Search**
    
    ---
    
    Find what you need in seconds. Search across all your notebooks and pages instantly, whether you're looking for typed text, headings, or organized topics.

-   :material-file-document-outline: **Flexible Import & Export**
    
    ---
    
    Your notes aren't trapped. Import existing PDF documents to annotate directly on the canvas, or export your work cleanly to Markdown and PDF to share with anyone.

-   :material-infinity: **Truly Infinite Canvas**
    
    ---
    
    Never run out of room or get forced onto the next page mid-thought. Pan and zoom infinitely in any direction with stutter-free navigation, no matter how much you write or draw.

-   :material-shield-lock-outline: **Private & Local-First**
    
    ---
    
    Your data stays on your device. Everything saves locally in open, portable formats without mandatory logins, subscriptions, or cloud lock-in.

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
