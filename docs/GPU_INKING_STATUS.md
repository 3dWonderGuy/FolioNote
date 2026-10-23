# GPU Inking Architecture & Blend2D Dependencies Status

## 1. Overview
FolioNote now performs ink rasterization via hardware acceleration using `SDL_GPU` (Vulkan on Linux / D3D12 on Windows / Metal on macOS). The stroke modeling engine (`Folio-Sketch`) and live inking pipeline are decoupled from Blend2D `BLPath`.

---

## 2. Active GPU Inking Pipeline
- **Live In-Flight Strokes**:
  - Sampled input points are modeled by Google Ink Stroke Modeler in real-time.
  - Geometry is tessellated directly into `Folio::InkMesh` (indexed triangle strip + vertex AA skirt).
  - Submitted directly to `Folio::GpuInkManager` during interaction without converting to CPU paths.
- **Baked Canvas Strokes**:
  - Committed strokes in `InkContainer` cache their `InkMesh`.
  - During canvas baking or region patches, meshes are batched and submitted to the GPU via `GpuInkManager::SubmitMesh`.

---

## 3. What Still Uses Blend2D
Blend2D remains in the application for non-ink elements and composition:
1. **Compositing Surface Target**:
   - `GpuInkManager::FlushToSurface(...)` executes GPU draws onto an offscreen texture and transfers the rendered pixels back to `BLImage` via `SDL_DownloadFromGPUTexture`.
   - Backdrop rendering (page background, paper textures, grid/ruling lines, PDF pages, marquee selection bounds).
2. **Non-Ink Canvas Primitives**:
   - `ShapeObject` (rectangles, ellipses, triangles, polygons, stars).
   - `SmartArrowObject` (straight, elbow, and curved connectors).
   - `TextBoxObject` / text glyph layouts.
   - `ImageObject` (imported bitmap images).

---

## 4. Deactivated / Fallback Features (Phase 2)
The following features were impacted when `BLPath` was removed from the ink engine:

| Feature | Previous Method | Current State | Future GPU Roadmap |
| :--- | :--- | :--- | :--- |
| **Textured Pencil Strokes** | `InkGenerator::RenderPencilStroke` using Blend2D stippling / CPU noise along `BLPath`. | **Deactivated** (renders as a solid GPU stroke mesh). | Procedural noise / texture sampler in `GpuInkManager` fragment shader. |
| **Dashed / Dotted Outlines** | Generated stroke ribbons via `InkGenerator::GenerateStroke()` converted to `BLPath`. | **Fallback** (renders as standard solid strokes). | GPU dash-pattern strip tessellator or shader-based alpha discards. |
| **Vector Export (`BLPath`)** | `FinishedStrokeData::outlinePath` cached closed ribbon polygons. | **Removed from container**. Strokes store `modeledPoints` and `mesh`. | SVG / PDF vector exporters should generate paths directly from `modeledPoints` on demand. |
| **CPU Fallback Inking** | CPU branch rendering `stroke.outlinePath` with `ctx.fill_path()` when GPU unavailable. | **Removed**. All stroke rendering uses GPU mesh submission. | Simplified CPU triangle rasterizer or software fallback pipeline. |
