# FolioNote Engine

**FolioNote** is a cross-platform, hardware-accelerated spatial canvas engine written in C++20. It integrates Blend2D software rasterization, Dear ImGui, SDL3, OpenGL presentation, and an R-Tree spatial acceleration index.

```mermaid
graph TD
    A[Hardware Digitizer / Pen Events] --> B[Input Manager & State Machine]
    B --> C[Canvas Engine / Live Layer]
    B --> D[Document Session]
    D --> E[Spatial Scene & R-Tree]
    D --> F[Persistence Layer: SQLite + .ink]
    E --> G[Blend2D Software Rasterizer]
    G --> H[OpenGL Texture Composite & Presentation]
