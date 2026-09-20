# FolioNote Diagnostic Error Codes

This document serves as the central registry for standardized error codes in FolioNote.
Every error code consists of a **domain prefix** and a **4-digit numeric code**.

---

## Error Code Partitions

| Range | Domain | Subsystems Involved |
|---|---|---|
| **1000 – 1999** | **System & OS** | File I/O, memory allocations, path resolution |
| **2000 – 2999** | **Storage & Database** | SQLite (`DBManager`), page binary blobs (`PageRepository`) |
| **3000 – 3999** | **Document Subsystem** | `Workspace`, `Notebook`, `SectionGroup`, `Section`, `CanvasPage`, `LibraryManager` |
| **4000 – 4999** | **Input Subsystem** | `InputManager`, `InputStateMachine`, touch gestures, stylus telemetry |
| **5000 – 5999** | **Canvas Engine & Spatial** | `CanvasEngine`, `CanvasTransform`, `RTree`, `Blend2D` context |

---

## 1. System & OS Codes (1000 – 1999)

### `ERR-1001: SysOutOfMemory`
- **Description:** System RAM allocation failed.
- **Cause:** Large raster image import, excessive undo history, or stroke buffer exhaustion.
- **Action:** Evict inactive pages via LRU cache (`MaintainWorkingSetLRU`) and purge memory.

### `ERR-1002: SysFileNotFound`
- **Description:** Target file does not exist on disk.
- **Cause:** Attempted to access an asset, `.ink` page blob, or PDF that was moved or deleted.
- **Action:** Check file paths and verify package integrity.

### `ERR-1004: SysFileAccessDenied`
- **Description:** OS denied file system read/write permissions.
- **Cause:** Read-only storage volume, file opened exclusively by another process, or sandbox limits.
- **Action:** Check folder permissions and ensure file is not locked.

---

## 2. Storage & Database Codes (2000 – 2999)

### `ERR-2001: DbOpenFailed`
- **Description:** Could not open SQLite database (`structure.db`).
- **Cause:** File locked by another process, invalid path, or missing directory.
- **Action:** Verify package folder exists and no other instance of FolioNote has the file open.

### `ERR-2002: DbSchemaMigrationFailed`
- **Description:** Failed to execute DDL schema migrations.
- **Cause:** Out-of-order index creation or conflicting column types on an older database schema.
- **Action:** Ensure migrations run before dependent indexes are constructed.

### `ERR-2004: DbDisconnected`
- **Description:** SQLite operation attempted while database connection is closed.
- **Cause:** Schema initialization failed on open, or database was closed during workspace unload.
- **Action:** Re-open the database before dispatching page save or load tasks.

### `ERR-2101: RepoBlobWriteFailed`
- **Description:** Failed to serialize or compress `.ink` page vector blob.
- **Cause:** Disk write error, full disk, or invalid page object data.
- **Action:** Verify disk space and check write permissions.

---

## 3. Document Subsystem Codes (3000 – 3999)

### `ERR-3001: DocWorkspaceLoadFailed`
- **Description:** Root workspace directory failed to initialize.
- **Cause:** Root path invalid or inaccessible.
- **Action:** Check `LoadWorkspace` directory path argument.

### `ERR-3002: DocLibraryNotFound`
- **Description:** Target `.foliolib` library bundle missing or not registered.
- **Cause:** Library folder was renamed, moved, or deleted externally.
- **Action:** Re-scan libraries with `LibraryDiscovery` or recreate `Default.foliolib`.

### `ERR-3010: DocNotebookNotFound`
- **Description:** Requested notebook does not exist in workspace or library.
- **Cause:** Bad notebook GUID or notebook folder missing.
- **Action:** Verify notebook GUID in workspace list.

### `ERR-3011: DocNotebookLoadFailed`
- **Description:** Notebook package failed to deserialize.
- **Cause:** Corrupt SQLite database or missing package folders.
- **Action:** Inspect `structure.db` and check folder structure.

### `ERR-3020: DocSectionNotFound`
- **Description:** Section GUID not found in notebook.
- **Cause:** Section was deleted, moved, or requested with an invalid GUID.
- **Action:** Refresh section navigation sidebar.

### `ERR-3040: DocPageNotFound`
- **Description:** CanvasPage GUID not found in section.
- **Cause:** Page removed, GUID mismatch, or section emptied.
- **Action:** Verify page exists within `section->pages`.

### `ERR-3042: DocPageUnloaded`
- **Description:** Operation requested on a page that was evicted from RAM.
- **Cause:** Page was evicted by LRU memory cache and attempted to draw without re-hydrating.
- **Action:** Trigger `repository.LoadPage(page)` before accessing geometry.

---

## 4. Input Subsystem Codes (4000 – 4999)

### `ERR-4001: InputWindowUnstable`
- **Description:** Input event discarded because window is resizing or moving.
- **Cause:** WindowStateManager detected rapid SDL window resize/move events.
- **Action:** Normal behavior during window drag; prevents coordinate distortion.

### `ERR-4002: InputTargetPageNull`
- **Description:** Input event cannot be dispatched because no active CanvasPage is selected.
- **Cause:** Active section has 0 pages or document session is empty.
- **Action:** Ensure active section creates a fallback blank page.

### `ERR-4010: InputGestureUnrecognized`
- **Description:** Multi-finger touch sequence did not resolve to a known gesture.
- **Cause:** Ambiguous finger movement or stray palm contact.
- **Action:** Palm rejection or touch gesture canceled.

### `ERR-4040: InputActionTargetMissing`
- **Description:** Special action (Undo, Redo, Gizmo duplicate) lacks active target.
- **Cause:** Undo triggered on an empty page history stack, or selection action with 0 objects selected.
- **Action:** Safely ignored or status bar warning displayed.
