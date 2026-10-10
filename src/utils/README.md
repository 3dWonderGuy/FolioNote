# FolioNote Utilities Subsystem (`src/utils`)

> **Location:** `src/utils/`  
> **Architecture Pattern:** Thread-Safe Foundation Services, Mathematical Engines & System Helpers  
> **Target Framework:** C++20, Multi-Platform (Windows, Linux, macOS, Android)

---

## 1. Architectural Overview & Design Philosophy

The `src/utils` directory houses foundational, cross-cutting infrastructure classes utilized across all layers of FolioNote (from input digitizer telemetry to document serialization and UI rendering). 

Every utility in this module is built around strict architectural invariants:
1. **Zero UI Thread Blocking:** Expensive operations (I/O, database writes, background indexing) are dispatched to dedicated worker pools.
2. **Lock-Free Concurrency Where Feasible:** High-frequency identifiers (UIDs) and randomness engines (UUIDs) use atomic relaxed memory orders and thread-local state to eliminate thread contention.
3. **Deterministic Numerical Foundations:** Interactive animations, camera gliding, and stroke smoothing rely on formal physical equations (second-order damped harmonic oscillators, RK4 numerical integrators).

---

## 2. Catalog of Utility Classes

| Class / Component | Header | Category | Primary Purpose |
| :--- | :--- | :--- | :--- |
| **`GUIDGenerator`** | [`guid_generator.hpp`](guid_generator.hpp) | Identity | High-entropy RFC 4122 UUID v4 generation for persistent document entities. |
| **`UIDGenerator`** | [`uid_generator.hpp`](uid_generator.hpp) | Identity | Lock-free monotonic 32-bit unique ID generation for runtime canvas objects. |
| **`ThreadPool`** | [`thread_pool.hpp`](thread_pool.hpp) | Concurrency | Work-stealing background thread pool preserving 1 CPU core for the UI thread. |
| **`FolioErrorCode`** | [`error_codes.hpp`](error_codes.hpp) / [`.md`](error_codes.md) | Diagnostics | Strongly-typed, categorized error codes and troubleshooting mappings. |
| **`Logger`** | [`logger.hpp`](logger.hpp) | Diagnostics | Multi-sink logging facade routing to console, rotating file sinks, and Android logcat. |
| **`PhysicsModel`** | [`physics_model.hpp`](physics_model.hpp) | Mathematics | Second-order spring-damper harmonic oscillators, kinetic friction, and RK4 integration. |
| **`PrinterInstaller`** | [`printer_installer.hpp`](printer_installer.hpp) | Windows OS | Spooler registration and scheduled event routing for "Print to FolioNote". |

---

## 3. Deep-Dive: Utility Specifications

### 3.1 `GUIDGenerator` ([`guid_generator.hpp`](guid_generator.hpp))

#### What it is for:
Generates standard 36-character RFC 4122 Version 4 UUID strings in the canonical format:
$$\text{xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx}$$

#### Why it exists:
- FolioNote represents the entire document hierarchy (Libraries, Notebooks, Section Groups, Sections, and Canvas Pages) using persistent UUID string keys.
- By using UUID strings instead of bidirectional C++ pointers (`std::shared_ptr`), the document model eliminates cyclic reference memory leaks and allows direct foreign key mapping into SQLite tables (`structure.db`).

#### General Working Process & Multi-Source Entropy:
- Standard `std::random_device` can degenerate into a deterministic pseudo-random sequence on certain platforms (e.g., MinGW/Windows).
- To prevent duplicate UUIDs across threads or process restarts, `GUIDGenerator` mixes multiple entropy sources:
  1. Two 32-bit `std::random_device` queries are combined into a 64-bit word.
  2. The high-resolution system clock (`std::chrono::high_resolution_clock::now().time_since_epoch()`) is XOR-blended into the seed.
- **Concurrency & Performance:** The Mersenne Twister PRNG (`std::mt19937_64`) and integer distribution are marked `thread_local`. Each worker thread seeds its own engine once without lock contention. Formatting uses an on-stack buffer (`char buf[37]`) via `std::snprintf`, bypassing heap allocations.

---

### 3.2 `UIDGenerator` ([`uid_generator.hpp`](uid_generator.hpp))

#### What it is for:
Provides fast, thread-safe monotonic 32-bit integer identifiers (`uint32_t`) for in-memory canvas objects. Zero (`0`) is strictly reserved to denote `InvalidUID` / `NullObject`.

#### Why it exists:
- Every active object on an infinite canvas (`InkContainer`, `TextBox`, `ImageObject`, `VideoObject`, `ShapeObject`) requires an $O(1)$ lookup handle in `CanvasPage::objectMap` and entry keys in spatial R-Trees.
- Generating full 36-byte UUID strings for every short pen stroke would inflate memory usage and slow down spatial lookups. Monotonic 32-bit integers require only 4 bytes.

#### Collision-Avoidance During Deserialization:
- When a page is loaded from disk, existing stroke objects contain pre-recorded UIDs.
- `EnsureAtLeast(uint32_t minVal)` uses a lock-free `compare_exchange_weak` loop:
  ```cpp
  uint32_t current = nextUID.load(std::memory_order_relaxed);
  while (current < minVal) {
      if (nextUID.compare_exchange_weak(current, minVal, std::memory_order_relaxed))
          break;
      current = nextUID.load(std::memory_order_relaxed);
  }
  ```
  This guarantees that subsequent newly drawn strokes always receive IDs strictly greater than all loaded objects, preventing UID collisions without requiring full-page UID remapping.

---

### 3.3 `ThreadPool` ([`thread_pool.hpp`](thread_pool.hpp))

#### What it is for:
A fixed-size task queue and asynchronous worker thread pool returning `std::future<ReturnType>`.

#### Why it exists:
- Heavy disk I/O, two-phase `.tmp` file saving, SQLite Write-Ahead Log (WAL) commits, FTS5 full-text indexing, and thumbnail rendering would cause noticeable micro-stutters if executed on the main thread.
- Spawning ad-hoc OS threads per save request incurs expensive OS stack allocations (~1 MB) and kernel context switching overhead.

#### CPU Allocation Math & Heuristic:
- If constructed with `threads = 0`, the pool queries hardware cores via `std::thread::hardware_concurrency()` and applies the following formula:
  $$\text{workers} = \begin{cases} \text{cores} - 1 & \text{if } \text{cores} > 2 \\ 2 & \text{otherwise} \end{cases}$$
- **UI Protection Invariant:** Reserving 1 CPU core exclusively for the main rendering loop ensures that Dear ImGui and Blend2D consistently maintain 120 FPS / 60 FPS presentation even during heavy background export or save operations.

---

### 3.4 `FolioErrorCode` ([`error_codes.hpp`](error_codes.hpp))

#### What it is for:
A strongly typed enumeration (`enum class FolioErrorCode : uint32_t`) categorizing every system, storage, document, engine, and UI failure condition.

#### Why it exists:
- Avoids ambiguous boolean return codes (`bool success`), raw integer status codes, or unhandled exceptions escaping across thread boundaries.
- Provides a direct link between runtime failures and the structured troubleshooting guides documented in [`error_codes.md`](error_codes.md).

#### Range Partitioning:
- `1000 - 1999`: System, OS, File I/O & Concurrency
- `2000 - 2999`: Storage & SQLite Database Subsystem
- `3000 - 3999`: Document Hierarchy Subsystem (Workspace, Library, Notebook, Section, Page)
- `4000 - 4999`: Canvas, Inking & Rendering Engines
- `5000 - 5999`: UI Presentation & View Navigation
- `6000 - 6999`: Asset Import & Export Pipelines

---

### 3.5 `Logger` ([`logger.hpp`](logger.hpp))

#### What it is for:
A centralized, thread-safe diagnostic logging system with severity filtering (`Info`, `Warn`, `Error`), component tags (`LogSource`), and multi-sink output routing.

#### Why it exists:
- Cross-platform divergence: Desktop builds require synchronous console output (`std::cout`/`std::cerr`) combined with rotating file logs (`src/io/file_logger.hpp`), while Android builds require logging via `__android_log_print` / `SDL_Log`.
- Prevents interleaved or corrupted multi-line log output when multiple worker threads log errors simultaneously.

#### Macro Ergonomics:
- `LOG_INFO(source, message, ...)`
- `LOG_WARN(source, message, ...)`
- `LOG_ERROR(source, message, ...)`
- `LOG_ERROR_CODE(source, errorCode, context)`: Automatically appends the error code enum name and numeric ID.

---

### 3.6 `PhysicsModel` ([`physics_model.hpp`](physics_model.hpp))

#### What it is for:
High-performance computational physics engines and numerical integrators providing physical dynamics for canvas interactions.

#### Why it exists:
- Digital canvas manipulation feels robotic and unnatural when coordinates snap linearly. Real-world physical tools exhibit inertia, spring resistance, and settling damping.
- Used extensively for:
  1. Kinetic camera panning glide with friction deceleration.
  2. Spring-damper zoom transitions.
  3. Dynamic velocity-coupled eraser radius scaling.

#### Mathematical Foundations:

##### 1. Second-Order Damped Harmonic Oscillator (Spring-Damper System)
The motion of an object attached to a virtual spring and damper is modeled by the differential equation:
$$m \frac{d^2 x}{dt^2} + c \frac{dx}{dt} + k (x - x_{\text{target}}) = 0$$

Dividing by mass $m$:
$$\frac{d^2 x}{dt^2} + 2 \zeta \omega_n \frac{dx}{dt} + \omega_n^2 (x - x_{\text{target}}) = 0$$

Where:
- $\omega_n = \sqrt{k / m}$ is the natural undamped angular frequency (responsiveness in rad/s).
- $\zeta = \frac{c}{2 \sqrt{k m}}$ is the damping ratio:
  - $\zeta = 1.0$: **Critically Damped** (fastest settling time without overshoot).
  - $\zeta < 1.0$: **Underdamped** (natural elastic spring oscillation).
  - $\zeta > 1.0$: **Overdamped** (viscous, smooth non-oscillatory glide).

##### 2. Velocity-Coupled Dynamic Eraser Physics
Couples raw stylus speed $v = \frac{\|\Delta \mathbf{p}\|}{\Delta t}$ to a dynamic virtual eraser radius $r$:
- Target radius maps monotonically to velocity:
  $$r_{\text{target}} = r_{\text{base}} + (r_{\text{max}} - r_{\text{base}}) \cdot \frac{v}{v + v_{\text{half}}}$$
- **Asymmetric Damping:** When acceleration occurs ($\dot{v} > 0$), lower damping is used for fast expansion; during deceleration ($\dot{v} \le 0$), higher damping is used to prevent jittery shrinking.

##### 3. Numerical Integrators
- **Semi-Implicit Euler:**
  $$v_{t+\Delta t} = v_t + a_t \Delta t$$
  $$x_{t+\Delta t} = x_t + v_{t+\Delta t} \Delta t$$
  *(Symplectic integrator preserving phase space energy better than standard Explicit Euler).*
- **Runge-Kutta 4th Order (RK4):** Fourth-order integrator evaluating four derivative slopes ($k_1, k_2, k_3, k_4$) for high-precision simulation trajectories when step sizes vary.

---

### 3.7 `PrinterInstaller` ([`printer_installer.hpp`](printer_installer.hpp))

#### What it is for:
Manages the installation, configuration, and event routing of the virtual Windows printer named **"Print to FolioNote"**.

#### Why it exists:
- Enables users to "print" any document, webpage, or lecture slide directly from third-party desktop software (Chrome, Edge, Microsoft Office, SumatraPDF, CAD viewers) into FolioNote.
- Eliminates manual PDF export $\rightarrow$ file saving $\rightarrow$ manual import workflows.

#### Operating Mechanism:
1. **Driver Foundation:** Uses the native Windows 10/11 "Microsoft Print to PDF" driver (`ntprint.inf`).
2. **Spooler Event Hook (Event 307):** Enables the Windows Print Service Operational log (`wevtutil sl Microsoft-Windows-PrintService/Operational /e:true`). When a print job completes, Event 307 captures document title (`Param2`) and printer name (`Param5`).
3. **Scheduled Task Dispatch:** Registers a Windows Task (`FolioNotePrintDispatch`) that triggers upon Event 307 for "Print to FolioNote", launching FolioNote with `--import` and passing the spooled PDF.
4. **UAC Elevation:** Standard execution runs under medium integrity. Spooler modifications require administrative rights; `InstallPrinterElevated` triggers standard Windows UAC elevation via `ShellExecuteExW` with verb `L"runas"`.
