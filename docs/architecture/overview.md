# Architecture Overview

FolioNote is engineered as a decoupled, multi-pass spatial engine designed for real-time digital inking, scalable infinite canvas operations, and zero-stall persistence.

---

## High-Level Subsystems

- **Presentation & Window Management (`src/app/`):** Orchestrates SDL3 window lifecycles, native high-DPI scaling, and OpenGL 3.3 context presentation.
- **Input Pipeline (`src/input/`):** Decouples 480 Hz hardware polling from frame pacing, arbitrating between active stylus telemetry, touch gestures, and mouse events.
- **Spatial Scene & Indexing (`src/core/spatial/`, `src/core/scene/`):** Manages bounding boxes in physical world millimeters ($mm$) and queries visible geometry via a dynamic R-Tree.
- **Rendering Pipeline (`src/core/engine/`):** Dual-layer software rasterizer using Blend2D for static baking and an in-flight live layer for sub-frame stroke latency.
- **Persistence Subsystem (`src/core/storage/`):** SQLite metadata storage (`structure.db`) paired with asynchronous, zlib-compressed binary geometry streams (`.ink`).
