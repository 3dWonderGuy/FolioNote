# Data Flow & Execution Passes

FolioNote separates runtime execution into four asynchronous, decoupled pipelines to ensure that input polling, canvas rasterization, and disk I/O never stall one another.

---

## 1. Live Inking Pipeline (480 Hz Input Polling)

Digitizer hardware events bypass the main scene rasterizer to deliver zero-latency visual feedback on active strokes.

```mermaid
sequenceDiagram
    autonumber
    participant HW as Digitizer Hardware
    participant IM as InputManager
    participant CE as CanvasEngine
    participant LL as LiveLayerPipeline

    HW->>IM: Raw Coordinate & Pressure Stream
    IM->>CE: Convert Screen Px to World mm
    CE->>LL: Append in-flight segment
    LL-->>CE: Immediate preview bake
```

---

## 2. Stroke Committal Pipeline (Pen Lift)

When the pen lifts from the screen, the temporary stroke converts into a permanent, indexed vector object.

```mermaid
sequenceDiagram
    autonumber
    participant IM as InputStateMachine
    participant DS as DocumentSession
    participant SS as StrokeSmoother
    participant CP as CanvasPage
    participant RT as R-Tree Index
    participant OR as ObjectRegistry

    IM->>DS: CommitStroke(rawBuffer)
    DS->>SS: RDP Decimation & Catmull-Rom Spline
    SS-->>DS: Vector Ribbon Mesh (InkStroke)
    DS->>CP: AddObject(InkStroke)
    CP->>OR: Store geometry by UID
    CP->>RT: Insert AABB + UID pair
    DS->>CP: Mark Page Dirty
```

---

## 3. Frame Render Pipeline (Display Refresh)

The render loop dynamically composites static content with active live strokes.

```mermaid
flowchart TD
    A[Begin Frame] --> B[Compute Viewport AABB in mm]
    B --> C{Camera Transformed?}
    
    C -- Yes --> D[Query R-Tree for Visible UIDs]
    D --> E[Fetch Objects from ObjectRegistry]
    E --> F[Rasterize to Static Blend2D Buffer]
    
    C -- No --> G[Reuse Cached Static Buffer]
    
    F --> H[Composite Live Inking Tail]
    G --> H
    H --> I[Upload Texture to OpenGL Framebuffer]
    I --> J[Present via Dear ImGui Canvas Viewport]
```

---

## 4. Background Persistence Pipeline

Disk writes occur off the main UI and rendering threads using asynchronous worker pools.

```mermaid
flowchart LR
    A[Dirty Page Flag] --> B[Serialize Stroke Buffers]
    B --> C[zlib Compression]
    C --> D[(Write pages/*.ink)]
    
    A --> E[Update Metadata]
    E --> F[(SQLite structure.db Commit)]
```
