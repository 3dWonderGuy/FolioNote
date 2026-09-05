---
title: Home
---

# FolioNote Engine Documentation

**FolioNote** is a cross-platform, hardware-accelerated spatial canvas engine written in modern C++20. Designed for low-latency digital inking and infinite workspace navigation, it pairs JIT-accelerated vector rasterization via **Blend2D** with **Dear ImGui** interface management, **SDL3/OpenGL** presentation, and multi-scale **R-Tree** spatial acceleration.

---

## 🏗️ Architecture Overview

```mermaid
graph TD
    subgraph Input ["Hardware Ingestion (480Hz)"]
        A[SDL3 Event Loop] --> B[Input State Machine]
        B --> C[Touch Gesture Decoder]
        B --> D[Pen / Stylus Telemetry]
    end

    subgraph Core ["Document & Inking Engine"]
        D --> E[Live Inking Pipeline]
        D -->|Pen Lift| F[Stroke Smoother & Splines]
        F --> G[Spatial Scene]
        G --> H[(R-Tree Spatial Index)]
        G --> I[Object Registry]
    end

    subgraph Render ["Rasterization & Output"]
        H -->|Viewport Query| J[Blend2D Software Rasterizer]
        E -->|In-flight Tail| K[Composite Buffer]
        J --> K
        K --> L[OpenGL Texture / ImGui Canvas]
    end

    subgraph IO ["Persistence Layer"]
        G --> M[Async Worker Pool]
        M --> N[SQLite Metadata: structure.db]
        M --> O[Compressed Geometry: *.ink]
    end
