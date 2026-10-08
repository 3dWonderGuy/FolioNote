# Document Subsystem (`src/core/document`)

## Overview

The `document` subsystem is FolioNote's **Source of Truth**. It models the user's notes and canvases, coordinates hierarchical navigation, enforces data integrity invariants, and manages memory lifetimes via on-demand lazy loading and dual-axis LRU eviction.

---

## 🏛️ Document Hierarchy Model

FolioNote organizes content into a four-tier spatial and semantic tree:

```
Workspace
 └── Notebook (.notebook folder bundle + SQLite structure.db)
      ├── SectionGroup (Optional nested folders)
      │    └── Section
      └── Section (Thematic tabs: e.g., "Lectures", "Homework")
           └── CanvasPage (2D drawing canvas + R-Tree + Undo/Redo stack)
                └── CanvasObject (Ink strokes, images, text, math curves, PDFs)
```

| Entity | Class | Physical Storage | Core Invariant |
|---|---|---|---|
| **Workspace** | `Workspace` (`workspace.hpp`) | Documents root folder | Owns `PageRepository`; manages all mounted notebooks. |
| **Notebook** | `Notebook` (`notebook.hpp`/`.cpp`) | `.notebook/` directory | Always guaranteed to have at least one section upon creation. |
| **SectionGroup** | `SectionGroup` (`section_group.hpp`) | Relational SQLite table | Recursive tree folders; soft-deletable. |
| **Section** | `Section` (`section.hpp`) | Relational SQLite table | Always guaranteed to have at least one page upon creation. |
| **CanvasPage** | `CanvasPage` (`canvas_page.hpp`) | Compressed `.ink` binary file | Owns independent `RTree` spatial index and `CommandHistory`. |

---

## 🛡️ Data Integrity & Error-Proofing Invariants

### 1. Zero-Null Document Guarantee
* Instantiating a `Notebook` automatically creates a default `"New Section 1"`.
* Instantiating a `Section` automatically creates a default `"Untitled page"`.
* **Result:** Navigation, render loops, and tool managers can never encounter an uninitialized or empty document tree.

### 2. Cycle-Free Lifetime Management
* Tree nodes reference parents and notebooks using **UUID strings** (`parentPageGuid`, `notebookGuid`, `groupGuid`) rather than bidirectional `std::shared_ptr`.
* **Result:** Zero circular reference memory leaks, safe copy-on-write cloning, and direct serialization into SQLite foreign key columns.

### 3. Dual-Axis LRU Cache Eviction & Zero Data Loss
* Canvas pages track access timestamps (`lastAccessTimeMs`) and modification state (`isModified`).
* In [`Workspace::MaintainWorkingSetLRU`](file:///home/odysseus/Documents/GitHub/FolioNote/src/core/document/workspace.hpp), inactive pages exceeding timeout limits (60s) or capacity caps are evicted from RAM.
* **Invariant:** Any dirty page is persisted to disk **before** its RAM objects are unloaded. When navigated back to, `Workspace::GetActivePage` transparently reloads it from disk on-demand.

### 4. Non-Destructive Recycle Bin Quarantine
* Notebook deletions move package bundles to `.trash/` within their library instead of issuing immediate filesystem unlinks.
* Page and Section deletions assign `deletedAt = unix_timestamp` (soft delete), enabling instant restoration and preventing accidental data loss.

### 5. Defensive UID & Spatial Synchronization
* `CanvasPage::AddObject` verifies runtime UID validity (`uid > 0`) and enforces uniqueness: if a UID collision occurs, a fresh monotonic ID is automatically assigned via `UIDGenerator::Next()`.
* Every addition, removal, or replacement synchronously updates:
  1. `objects` vector (rendering z-order)
  2. `objectMap` hash table (O(1) runtime UID lookup)
  3. `spatialIndex` R-Tree (frustum culling bounding boxes)

---

## 📂 Subsystem Directory Breakdown

```
src/core/document/
├── canvas_page.hpp          # Canvas surface model, object map, R-Tree spatial index
├── document_observer.hpp    # IDocumentSessionObserver interface
├── document_session.hpp     # Facade coordinating input, engine, and document state
├── notebook.hpp / .cpp      # Notebook package entity and active section resolution
├── section_group.hpp        # Hierarchical section folders and recursive cloning
├── section.hpp              # Section tab entity and page management
├── workspace.hpp            # Workspace root, PageRepository owner, and LRU eviction
├── session/                 # Modular DocumentSession implementations
│   ├── session_canvas_ops.cpp   # Inking commits, clipboard, grouping, template locks
│   ├── session_history.cpp      # Undo/Redo dispatch, eraser transaction aggregation
│   ├── session_lifecycle.cpp    # Session initialization, cold launch restore
│   ├── session_metadata.cpp     # Page styling, paper rules, observer dispatch, autosave
│   └── session_navigation.cpp   # Sequential & deep GUID page transitions, camera cache
└── library/                 # Multi-library packaging and discovery
    ├── library.hpp / .cpp       # Library bundle registration and routing
    ├── library_discovery.cpp   # Disk scanning (.foliolib and library.meta markers)
    ├── library_trash.cpp       # .trash/ quarantine, collision-free moves, permanent purge
    └── notebook_cloner.cpp     # Binary package replication
```

