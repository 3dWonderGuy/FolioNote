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
