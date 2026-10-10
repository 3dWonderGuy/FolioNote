# FolioNote I/O & User Data Persistence Workflow Specification

> **Module:** `src/io/`  
> **Subsystem Classification:** Hardware Storage Abstraction & Critical Persistence Pipeline  
> **Target Audience:** Engine Core Developers, Security Auditors, Platform Engineers  
> **Primary Directive:** Absolute Data Protection, Crash Resilience, and Zero In-Place Overwrites

---

## 1. Executive Summary & Purpose

The `src/io/` subsystem is the most mission-critical component of the FolioNote application runtime. While rendering, UI animations, and vector rasterization can be reconstructed if interrupted, **user data written to storage cannot be recovered if corrupted**.

FolioNote operates as a **local-first, package-based digital notebook system**. Each notebook is stored as an on-disk folder package (`.notebook`) housing SQLite databases, binary vector strokes (`.ink`), JSON manifests, and external multimedia attachments. 

This document details the operational workflows, multi-threaded safety invariants, error recovery protocols, and platform integration pipelines implemented across the `src/io/` module.

---

## 2. Global Architecture & Pipeline Topology

The I/O subsystem sits between the domain persistence coordinators and the native operating system kernels:

```mermaid
graph TB
    subgraph Domain_Callers ["Application Domain Layer"]
        PageRepo["PageRepository<br/>(Background Stroke Persistence)"]
        LibMgr["LibraryManager<br/>(Discovery, Cloning, Trash)"]
        DocSession["DocumentSession<br/>(Asset Resolution & Linking)"]
        ExportMgr["ExportManager<br/>(PDF, Markdown, HTML Tarballs)"]
        BackupMgr["BackupManager<br/>(Version Snapshots & Compaction)"]
        LogRouter["Folio::Logger<br/>(Diagnostic Telemetry)"]
    end

    subgraph IO_Facade ["Subsystem Facade (src/io/facade/)"]
        FM["Folio::FileManager<br/>(Master Coordinator)"]
    end

    subgraph Domain_Services ["Domain Services"]
        subgraph Paths_Domain ["Paths & Environment (src/io/paths/)"]
            PU["PathUtils<br/>• Normalization<br/>• Unicode UTF-8/16<br/>• Win32 \\?\ Long Paths"]
            AD["AppDirectories<br/>• Document Roots<br/>• Package Roots<br/>• Asset Paths"]
        end

        subgraph Storage_Domain ["Disk Persistence (src/io/storage/)"]
            FW["FileWriter<br/>• Two-Phase .tmp Staging<br/>• _commit / fsync<br/>• Atomic Rename Swap"]
            FR["FileReader<br/>• MemoryMappedView<br/>• Zero-Copy FNV-1a<br/>• Virtual Streams"]
            FL["FileLogger<br/>• Rotating Sinks<br/>• In-Memory Ring Buffer"]
        end

        subgraph Platform_Domain ["Platform Shell (src/io/platform/)"]
            SD["SystemDialogs<br/>• Win32 GetOpenFileName<br/>• Linux Zenity/KDialog<br/>• macOS NSOpenPanel"]
            PM["PackageMarker<br/>• desktop.ini Generator<br/>• System Attributes<br/>• Shell Icon Cache Sync"]
        end
    end

    subgraph OS_Hardware ["Operating System & Hardware"]
        NTFS["Win32 NTFS / FAT32"]
        EXT4["POSIX ext4 / APFS"]
        NAND["Physical SSD / NVMe Storage Device"]
    end

    PageRepo -->|Persist Chunk| FM
    LibMgr -->|Tree Operations| FM
    DocSession -->|Resolve Assets| AD
    ExportMgr -->|Write Bundle| FW
    BackupMgr -->|Snapshot Package| FM
    LogRouter -->|Log Sink| FL

    FM --> FW
    FM --> FR
    FM --> PU
    FM --> AD
    FM --> SD

    FW -->|Raw Staging| NTFS
    FW -->|Raw Staging| EXT4
    FR -->|mmap Virtual Memory| NTFS
    FR -->|mmap Virtual Memory| EXT4
    NTFS -->|_commit Flush| NAND
    EXT4 -->|fsync Flush| NAND
```

---

## 3. Operational Workflows

### 3.1 Workflow 1: Crash-Resilient Page Persistence (`.ink`)

This workflow governs how handwritten strokes and page metadata are persisted from memory onto disk without risk of truncation.

```mermaid
sequenceDiagram
    autonumber
    actor Worker as Background Thread (ThreadPool)
    participant PR as PageRepository
    participant FW as FileWriter (src/io/storage/file_writer.cpp)
    participant PU as PathUtils (src/io/paths/path_utils.cpp)
    participant FS as Physical Filesystem (NTFS / POSIX)

    Worker->>PR: SavePageAsync(pageId, binaryPayload)
    PR->>FW: WriteBuffer(targetPath, binaryPayload)
    
    FW->>PU: Utf8ToNativePath(targetPath)
    PU-->>FW: nativeTargetPath (handles \\?\ prefix)
    
    FW->>FW: CreateParentDirectories(nativeTargetPath)
    Note over FW: Generate Staging Name:<br/>targetPath + ".tmp." + pid + "_" + timestamp + "_" + rand
    
    FW->>FS: fopen(stagingPath, "wb")
    alt File Open Failed (Permissions or Disk Full)
        FW-->>PR: return false (Target file untouched!)
    end
    
    FW->>FS: fwrite(payload.data(), 1, payload.size())
    FW->>FS: fflush(filePtr) (Flush C runtime buffer to OS kernel cache)
    
    Note over FW,FS: Hardware Flush Barrier
    alt Windows OS
        FW->>FS: _commit(_fileno(filePtr))
    else POSIX / macOS / Android
        FW->>FS: fsync(fileno(filePtr))
    end
    FW->>FS: fclose(filePtr)
    
    Note over FW: Integrity Verification
    FW->>FS: std::filesystem::file_size(stagingPath)
    alt Size != payload.size()
        FW->>FS: std::filesystem::remove(stagingPath)
        FW-->>PR: return false (Disk full during write!)
    end
    
    Note over FW,FS: Atomic Replace Operation
    opt On Windows
        FW->>PU: EnsureTargetWritable(targetPath) (Strips FILE_ATTRIBUTE_READONLY if cloud synced)
    end
    
    loop Max 5 Retries (Exponential Backoff: 10ms, 20ms, 40ms...)
        FW->>FS: MoveFileExW(staging, target, MOVEFILE_REPLACE_EXISTING) / rename()
        alt Success
            FW-->>PR: return true (File Safely Replaced)
        else Lock Contention (Antivirus / Cloud Sync)
            FW->>FW: std::this_thread::sleep_for(...)
        end
    end
```

---

### 3.2 Workflow 2: Zero-Copy Asset Ingestion & Content-Addressable Storage

When importing large images, background PDF documents, or audio clips, the application ingests files without memory duplication:

```mermaid
sequenceDiagram
    autonumber
    actor User as User Action (Drop File / Import)
    participant UI as Canvas Engine
    participant FR as FileReader (src/io/storage/file_reader.cpp)
    participant OS as OS Virtual Memory Manager (mmap)
    participant FM as FileManager (src/io/facade/file_manager.cpp)

    User->>UI: Drop Media File (e.g. "Lecture_Notes.pdf", 45 MB)
    UI->>FR: MapReadOnly("Lecture_Notes.pdf")
    
    alt Windows OS
        FR->>OS: CreateFileW(GENERIC_READ, FILE_SHARE_READ)
        FR->>OS: CreateFileMappingW(PAGE_READONLY)
        FR->>OS: MapViewOfFile(FILE_MAP_READ)
    else POSIX OS
        FR->>OS: open(O_RDONLY)
        FR->>OS: mmap(PROT_READ, MAP_SHARED)
    end
    
    OS-->>FR: MemoryMappedView (ptr to virtual address space, size = 45MB)
    Note over FR: 0 bytes copied into heap RAM!
    
    UI->>FR: ComputeHash64(view.data, view.size)
    Note over FR: Single pass FNV-1a 64-bit calculation (~1.5 GB/s)
    FR-->>UI: hash64 = 0x8a3f91bc7d20e14a
    
    Note over UI: Content-Addressable Dedup Check:<br/>assets/pdf_8a3f91bc7d20e14a.pdf
    alt Asset Already Exists in Notebook Package
        UI->>UI: Reuse existing asset reference
    else New Asset
        UI->>FM: CopyFileAtomic(sourcePath, packageAssetPath)
    end
    
    UI->>FR: view.Reset() (UnmapViewOfFile / munmap)
```

---

### 3.3 Workflow 3: Notebook Directory Package Branding (`PackageMarker`)

In FolioNote, `.notebook` folders must appear as genuine application notebooks in the Windows Shell rather than standard yellow folders.

```mermaid
sequenceDiagram
    autonumber
    participant App as LibraryManager
    participant PM as PackageMarker (src/io/platform/package_marker.hpp)
    participant FS as Windows NTFS Driver
    participant Shell as Windows Shell (Explorer.exe)

    App->>PM: MarkPackageDirectory("C:/.../Math.notebook", "FolioNote Math Notebook")
    
    Note over PM: Step 1: Write desktop.ini Descriptor
    PM->>FS: WriteFile("C:/.../Math.notebook/desktop.ini", contents)
    Note right of PM: [.ShellClassInfo]<br/>IconResource=FolioNote.exe,0<br/>InfoTip=FolioNote Math Notebook<br/>[ViewState]<br/>FolderType=Generic
    
    Note over PM: Step 2: Apply System & Hidden Attributes to desktop.ini
    PM->>FS: SetFileAttributesW("desktop.ini", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)
    
    Note over PM: Step 3: Apply READONLY Flag to Directory Object
    PM->>FS: SetFileAttributesW("Math.notebook", FILE_ATTRIBUTE_READONLY)
    Note right of PM: On Windows directory objects, READONLY indicates<br/>to Explorer: 'Parse desktop.ini customization!'
    
    Note over PM: Step 4: Shell Icon Cache Invalidation
    PM->>Shell: SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, "Math.notebook")
    Shell-->>User: Explorer immediately updates folder icon to FolioNote notebook icon!
```

---

### 3.4 Workflow 4: Recursive Safe Directory Tree Cloning & Moves

When a user duplicates a notebook or restores a backup snapshot, `FileManager::CopyDirectoryTree` ensures deep recursive duplication with conflict avoidance:

```mermaid
flowchart TD
    Start(["FileManager::CopyDirectoryTree(src, dst)"]) --> CheckSrc{"Source exists<br/>& is directory?"}
    CheckSrc -- No --> RetFalse["return false"]
    CheckSrc -- Yes --> CreateDst["CreateDirectoriesRecursive(dst)"]
    CreateDst --> Iterate["Iterate std::filesystem::recursive_directory_iterator"]
    
    Iterate --> EntryType{"Entry Type?"}
    EntryType -- Directory --> MakeSub["CreateDirectoriesRecursive(subDst)"]
    EntryType -- Regular File --> CopyFile["FileWriter::WriteBuffer(subDst, FileReader::ReadBinary(subSrc))"]
    EntryType -- Other --> Skip["Skip symlinks / sockets"]
    
    MakeSub --> NextEntry{"More entries?"}
    CopyFile --> VerifyCopy{"WriteBuffer succeeded?"}
    VerifyCopy -- No --> LogErr["Log error & abort"]
    VerifyCopy -- Yes --> NextEntry
    Skip --> NextEntry
    
    NextEntry -- Yes --> Iterate
    NextEntry -- No --> RetTrue["return true (Tree perfectly mirrored)"]
```

---

## 4. Concurrency & Multi-Thread Safety Model

| Component | Concurrency Guarantee | Synchronization Mechanism | Thread Safety Rules |
| :--- | :--- | :--- | :--- |
| `PathUtils` | **Thread-Safe & Reentrant** | None (Pure Stateless Functions) | Operates strictly on function parameters and local stack frames. Can be called simultaneously from hundreds of threads. |
| `AppDirectories` | **Thread-Safe** | `std::mutex s_packageRootMutex`, `std::mutex s_appRootMutex` | Setting or querying global roots or active package assets is protected by fine-grained mutexes. |
| `FileWriter` | **Thread-Safe** | Process & Thread Unique Staging File Tokens | Each atomic write generates a unique staging path incorporating process ID, thread ID, monotonic timestamp, and random numbers. Concurrent writes to *distinct* files proceed in parallel without locking. Concurrent writes to the *same* file are resolved by OS atomic swap serialization. |
| `FileReader` | **Thread-Safe & Lock-Free** | OS File Mapping Share Mode (`FILE_SHARE_READ`) | Multiple threads can simultaneously memory-map or read the same file on disk without contention. |
| `FileLogger` | **Thread-Safe** | `std::mutex m_mutex` | Log entry formatting, size tracking, rotation checks, and in-memory ring-buffer pushes are fully synchronized. |
| `SystemDialogs` | **Main Thread Only** | OS UI Thread Requirement | Native OS modal file dialogs (`GetOpenFileNameW`, AppleScript, Zenity) pump OS event loops and must only be invoked from the application's primary UI thread. |

---

## 5. Threat Modeling & Failure Mode Recovery

### 5.1 Scenario 1: Power Loss / Kernel Panic During Save
- **Threat:** User loses laptop battery or power cord is pulled while writing a 12 MB notebook vector drawing.
- **Handling:** The payload was being written to `.tmp.7142_1728518400_89211`. The original `page.ink` has not been touched. Upon reboot, FolioNote loads the untouched pre-save `page.ink`. The orphan `.tmp` file is cleaned up during the next startup cache sweep. Zero data corruption.

### 5.2 Scenario 2: Antivirus or Cloud Storage (OneDrive / Dropbox) Lock Contention
- **Threat:** When FolioNote writes a file inside a OneDrive folder, OneDrive's file-system filter driver momentarily locks the file with `FILE_SHARE_READ` to compute an SHA-256 hash. `MoveFileExW` fails with `ERROR_ACCESS_DENIED`.
- **Handling:** `FileWriter` detects the transient lock and enters an exponential backoff loop, retrying up to 5 times (total backoff window ~310 ms). The lock releases and the rename succeeds transparently without surfacing an error modal to the user.

### 5.3 Scenario 3: Destination File Marked `FILE_ATTRIBUTE_READONLY`
- **Threat:** Cloud-synced files or files cloned from Git frequently retain the NTFS read-only attribute bit. Win32 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` fails to overwrite read-only files.
- **Handling:** `PathUtils::EnsureTargetWritable()` queries file attributes and strips `FILE_ATTRIBUTE_READONLY` immediately prior to the rename call.

### 5.4 Scenario 4: Deep Directory Nesting Exceeding 260 Characters
- **Threat:** Deeply nested notebooks and UUID section hierarchies exceed the Win32 `MAX_PATH` limit of 260 characters.
- **Handling:** `PathUtils::Utf8ToNativePath` detects long paths ($\ge 240$ chars) and transforms the path into an NT kernel canonical extended-length path (`\\?\C:\...`), supporting paths up to 32,767 characters.

---

## 6. Subsystem Verification & Test Matrix

The I/O subsystem is validated through automated test suites in `test/`:

1. **`test_file_manager`:** Validates path normalization, directory enumeration, recursive copying, folder moving, and path equivalence across edge cases.
2. **`test_file_saver`:** Validates crash-resilient atomic writes, byte-for-byte fidelity, parent directory creation, and non-destructive overwrite behavior.
3. **`test_storage`:** Validates SQLite database storage and integration with `PageRepository` and `BinarySerializer`.
