# Runtime Component Graph

This page describes the ownership hierarchy, runtime object graph, and memory lifecycles across the FolioNote engine subsystems.

---

## 🏛️ Subsystem Ownership Hierarchy

The runtime enforces clear, single-owner hierarchies using modern C++ RAII semantics and smart pointers (`std::unique_ptr` for exclusive ownership, `std::shared_ptr` for shared resource caches).

```text
┌────────────────────────────────────────────────────────┐
│                      Application                       │
│  - WindowStateManager windowSM                         │
│  - ThemeManager themeManager                           │
│  - InputManager inputManager                           │
│  - CanvasEngine canvasEngine                           │
│  - DocumentSession session                             │
│  - DBManager dbManager                                 │
└───────────────────────────┬────────────────────────────┘
                            │ owns
                            ▼
┌────────────────────────────────────────────────────────┐
│                    DocumentSession                     │
│  - Workspace workspace                                 │
│  + CommitStroke(rawPoints)                             │
│  + QueryVisible(AABB)                                  │
└───────────────────────────┬────────────────────────────┘
                            │ owns
                            ▼
┌────────────────────────────────────────────────────────┐
│                       Workspace                        │
│  - std::vector<Notebook> notebooks                     │
│  - LRUCache<UUID, CanvasPage> pageCache                │
└───────────────────────────┬────────────────────────────┘
                            │ owns
                            ▼
┌────────────────────────────────────────────────────────┐
│                      CanvasPage                        │
│  - SpatialScene scene                                  │
│  - CommandHistory history                              │
│  - PageMetadata metadata                               │
└───────────────────────────┬────────────────────────────┘
                            │ owns
                            ▼
┌────────────────────────────────────────────────────────┐
│                     SpatialScene                       │
│  - RTree spatialIndex         (AABB + UID pairs)       │
│  - ObjectRegistry registry    (UID -> CanvasObject)    │
└────────────────────────────────────────────────────────┘
