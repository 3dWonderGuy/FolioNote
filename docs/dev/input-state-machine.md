# Input State Machine & Hardware Arbitration

The `InputStateMachine` (`src/input/input_state_machine.hpp/.cpp`) sits between low-level SDL3 hardware telemetry and high-level canvas operations. It is responsible for hardware contention resolution, palm rejection, Dear ImGui UI capture arbitration, and gesture decoding.

---

## 🛡️ Device Priority Arbitration (Palm Rejection)

When drawing on a tablet or touch-enabled laptop, the user's palm frequently rests on the screen surface, generating concurrent touch and stylus packets. The engine resolves contention every frame via a strict priority hierarchy:

```mermaid
flowchart TD
    Raw[Incoming Raw Event Stream] --> CheckPen{Is Stylus active<br/>or engaged < 80ms?}
    
    CheckPen -->|Yes| ActivePen["ActiveDevice = Stylus<br/>(Touch & Mouse 100% Suppressed)"]
    CheckPen -->|No| CheckTouch{Are touch fingers down<br/>or active < 80ms?}
    
    CheckTouch -->|Yes| ActiveTouch["ActiveDevice = Touch<br/>(Gesture Recognizer & Multi-Touch)"]
    CheckTouch -->|No| ActiveMouse["ActiveDevice = Mouse<br/>(Desktop Fallback Panning / Inking)"]
```

* **Active Lock Timeout ($80\text{ ms}$):** Once a device claims ownership, a short hysteresis cooldown prevents high-frequency touch noise from preempting an in-flight pen stroke between digitizer poll intervals.

---

## 🖊️ Stylus State & Tool Intent Mapping

When `ActiveDevice == Stylus`, the state machine maps physical sensor signals into semantic interaction states:

### 1. Proximity Tracking
* **`OutOfRange`:** Digitizer hover coil is idle.
* **`Hovering`:** Stylus tip is in proximity ($< 10\text{--}15\text{ mm}$) above the display surface ($P = 0.0$).
* **`Engaged`:** Stylus tip physically touches the screen ($P > 0.0$).

### 2. Hardware Button Intent Routing

```mermaid
flowchart TD
    Sensor[Stylus Telemetry Ingest] --> ButtonCheck{Hardware Trigger}
    
    ButtonCheck -->|pen.barrel1 OR pen.eraserTip| StateEraser["InteractionState::Eraser<br/>Direct R-Tree Spatial Erase"]
    ButtonCheck -->|pen.barrel2| StateLasso["InteractionState::Selecting<br/>Dynamic Lasso Hull"]
    ButtonCheck -->|Default Tip Contact| StateInking["InteractionState::Inking<br/>Real-Time Stroke Ingestion"]
```

---

## 🔒 UI Capture & Focus Arbitration (ImGui Interaction Locks)

To prevent accidental stroke artifacts when clicking on the Ribbon Bar, Navigation Sidebar, or Modal Dialogs, the engine enforces interaction capture locks:

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant StateMachine as InputStateMachine.cpp
    participant ImGui as Dear ImGui Context
    participant Canvas as CanvasEngine.cpp

    User->>StateMachine: Pointer Down (justDown)
    StateMachine->>ImGui: Check imguiWantsInput
    
    alt Clicked Inside UI Element
        ImGui-->>StateMachine: true (Hovering UI)
        StateMachine->>StateMachine: uiCaptured = true
        StateMachine-->>Canvas: Suppress PointerDown
        Note over StateMachine,Canvas: All drag/move events routed to UI exclusively
    else Clicked on Infinite Canvas
        ImGui-->>StateMachine: false (Canvas Hit)
        StateMachine->>StateMachine: uiCaptured = false
        StateMachine->>Canvas: CanvasEngine::OnPointerDown(...)
    end

    User->>StateMachine: Pointer Released (justUp)
    StateMachine->>StateMachine: uiCaptured = false (Lock Released)
```

---

## 📐 Coordinate Normalization

Raw digitizer pixels are mapped to canvas-local coordinates by offsetting interface panel margins before passing coordinates to the projection matrix:

$$\text{Local}_x = \text{Screen}_x - \text{CanvasOrigin}_x$$

$$\text{Local}_y = \text{Screen}_y - \text{CanvasOrigin}_y$$

*(Where $\text{CanvasOrigin}$ accounts for active dynamic sidebar width and top ribbon toolbar height).*

---

## ⚡ Device-Specific Dispatchers

```mermaid
flowchart TD
    Dispatch[InputStateMachine::Dispatch] --> Type{ActiveDevice}

    Type -->|Stylus| DS[DispatchStylus]
    Type -->|Mouse| DM[DispatchMouse]
    Type -->|Touch| DT[DispatchTouch]

    subgraph StylusFlow ["Stylus Actions"]
        DS -->|Inking| S_Ink["justDown -> CanvasEngine::OnPointerDown<br/>isMoving -> CanvasEngine::OnPointerMove<br/>justUp -> CanvasEngine::OnPointerUp (Commit)"]
        DS -->|Selecting| S_Sel["CanvasEngine::OnLassoDown / Move / Up"]
        DS -->|Eraser| S_Erase["Spatial R-Tree Intersection & Hit Erase"]
    end

    subgraph MouseFlow ["Mouse Actions"]
        DM -->|Left Click Drag| M_Ink["Simulate 1.0 Pressure Inking"]
        DM -->|Middle Click / Spacebar| M_Pan["CanvasEngine::Pan(deltaX, deltaY)"]
        DM -->|Scroll Wheel| M_Zoom["CanvasEngine::ZoomAt(localX, localY, factor)"]
    end

    subgraph TouchFlow ["Touch Actions (Up to 10 Slots)"]
        DT --> T_Gestures["TouchGestureRecognizer"]
        T_Gestures -->|SingleFingerScroll| T_Pan["CanvasEngine::Pan(deltaX, deltaY)"]
        T_Gestures -->|TwoFingerPinchPan| T_Pinch["Pan(dx, dy) + ZoomAt(focus, zoomFactor)"]
        T_Gestures -->|Tap / PressAndHold| T_Menu["Selection / Caret / Radial Menu"]
    end
```

---

## 📊 Summary of Event Routings

| Input Device | Interaction Trigger | Canvas Operation | Underlying Target Call |
|---|---|---|---|
| **Stylus** | Tip Contact ($P > 0$) | Stroke Ingestion | `CanvasEngine::OnPointerDown/Move/Up` |
| **Stylus** | Barrel 1 / Eraser Tip | Real-time Erasure | `DocumentSession::EraseAt(WorldPoint)` |
| **Stylus** | Barrel 2 | Freehand Lasso | `CanvasEngine::OnLassoDown/Move/Up` |
| **Mouse** | Left Drag | Fallback Inking | `CanvasEngine::OnPointerDown/Move/Up` |
| **Mouse** | Middle Drag / `Space` | Viewport Pan | `CanvasEngine::Pan(dx, dy)` |
| **Mouse** | Wheel Up / Down | Viewport Zoom | `CanvasEngine::ZoomAt(focus, 1.15 / 0.85)` |
| **Touch** | Two-Finger Pinch | Pan & Zoom | `CanvasEngine::Pan(...)` + `CanvasEngine::ZoomAt(...)` |
| **Touch** | 1-Finger Tap | Caret / Selection | `DocumentSession::SelectAt(WorldPoint)` |
