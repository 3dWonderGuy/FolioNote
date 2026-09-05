# Input Pipeline & Event Arbitration

The input subsystem processes continuous hardware telemetry, decouples sampling rates from frame display rates, and arbitrates input priorities between active pens, touch gestures, and mouse controls.

---

## 🎯 Priority Arbitration

Input events from SDL3 are routed through an **Input State Machine** adhering to strict priority rules:

1. **Active Stylus (Highest Priority):** When the digitizer senses pen tip contact or hover within proximity, touch events are rejected to enable palm rejection.
2. **Touch Gestures (Medium Priority):** Multi-touch points are routed to the `TouchGestureRecognizer` to compute pinch-to-zoom and two-finger pan deltas.
3. **Mouse / Trackpad (Fallback):** Standard desktop cursor interactions and wheel zooms.

---

              ┌──────────────────────┐
              │    SDL3 Event Ingest │
              └──────────┬───────────┘
                         │
                         ▼
              ┌──────────────────────┐
              │  Input State Machine │
              └──────────┬───────────┘
                         │
        ┌────────────────┼────────────────┐
        │ [Pen Event]    │ [Multi-Touch]  │ [Mouse]
        ▼                ▼                ▼
 ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
 │ Inking Tail │  │ Pan / Zoom  │  │ Tool Click  │
 │  Live Layer │  │ Decoders    │  │ UI Actions  │
 └─────────────┘  └─────────────┘  └─────────────┘

 ---

## ⏱️ Decoupled Sampling Rates

* **Digitizer Rate (Up to 480 Hz):** Captured on the input thread, converted to physical world coordinates ($mm$) via `CanvasTransform`, and immediately appended to the in-flight `LiveLayerPipeline`.
* **Render Display Rate (60 / 120 / 144 Hz):** The canvas composites the live tail on top of the static baked vector buffer, eliminating visual stroke lag.
