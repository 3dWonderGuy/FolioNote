# Runtime Component Graph

This diagram illustrates the ownership hierarchy and memory references among active subsystems during a running session.

---

## Ownership Hierarchy

```mermaid
classDiagram
    class Application {
        +WindowStateManager windowSM
        +ThemeManager themeManager
        +InputManager inputManager
        +CanvasEngine canvasEngine
        +DocumentSession session
        +DBManager dbManager
    }

    class DocumentSession {
        +Workspace workspace
        +CommitStroke()
        +QueryVisible(AABB)
    }

    class Workspace {
        +vector~Notebook~ notebooks
        +LRUCache pageCache
    }

    class CanvasPage {
        +SpatialScene scene
        +CommandHistory history
        +PageMetadata metadata
    }

    class SpatialScene {
        +RTree spatialIndex
        +ObjectRegistry registry
    }

    Application *-- DocumentSession
    Application *-- CanvasEngine
    DocumentSession *-- Workspace
    Workspace *-- CanvasPage
    CanvasPage *-- SpatialScene
