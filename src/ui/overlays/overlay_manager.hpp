#pragma once
/**
 * =========================================================================================
 * @file ui/overlays/overlay_manager.hpp
 * @brief Centralized Coordinator for Floating HUDs, Diagnostic Overlays, and Modals
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * --------------------------------
 * In FolioNote, overlays and modals are decoupled from the main workspace layout.
 * The `OverlayManager` unifies:
 *
 *   1. Floating HUDs:
 *      - World-to-screen tracked Audio Controller Capsule
 *      - Spotlight Action Command Palette (`ActionCommandPalette`)
 *
 *   2. Diagnostic Overlays:
 *      - Real-time Dev Telemetry & Frame Pacing (`DebugOverlay`)
 *      - Inking Tuning Studio (`InkingTuningOverlay`)
 *      - Toolbar Component Demo (`ToolbarDemoOverlay`)
 *
 *   3. Interactive Modal Dialogs:
 *      - PDF Import Placement (`PdfImportModal`)
 *      - Attachment Embed/Link Chooser Modal
 *
 * MATHEMATICAL & COORDINATE WORKING PROCESS:
 * - Floating HUDs track canvas world transforms using:
 *     screenPos = canvas.transform.WorldToScreen(worldX, worldY) + canvasOrigin
 * - Overlays render in the uppermost Z-index pass after primary viewport quads to ensure
 *   zero occlusion or input clipping by background rendering.
 */

#include <memory>
#include <string>
#include <imgui.h>

#include "ui/overlays/command_palette.hpp"
#include "ui/overlays/debug_overlay.hpp"
#include "ui/overlays/tuning_overlay.hpp"
#include "ui/overlays/toolbar_demo_overlay.hpp"
#include "ui/overlays/pdf_import_modal.hpp"
#include "app/theme_manager.hpp"

// Forward declarations
class CanvasEngine;
class DocumentSession;
class WindowStateManager;
class InputStateMachine;

namespace Folio {

/**
 * @class OverlayManager
 * @brief Coordinates all floating HUDs, diagnostic tools, and modal windows.
 */
class OverlayManager {
public:
    OverlayManager() = default;
    ~OverlayManager() = default;

    // Subsystem instances
    DebugOverlay devTelemetry;
    InkingTuningOverlay tuningStudio;
    ToolbarDemoOverlay toolbarDemo;
    PdfImportModal pdfImportModal;
    ActionCommandPalette commandPalette;

    // =========================================================================
    // MODAL ACTIVATION & CONTROL APIS
    // =========================================================================

    /**
     * @brief Opens the Spotlight Command Palette.
     */
    void OpenCommandPalette() { commandPalette.Open(); }

    /**
     * @brief Toggles the Spotlight Command Palette.
     */
    void ToggleCommandPalette() { commandPalette.Toggle(); }

    /**
     * @brief Checks if the command palette is currently visible.
     */
    [[nodiscard]] bool IsCommandPaletteOpen() const noexcept { return commandPalette.IsOpen(); }

    // =========================================================================
    // RENDERING PASSES
    // =========================================================================

    /**
     * @brief Renders floating canvas HUDs (such as the Audio controller capsule).
     *
     * @param canvas Canvas engine providing spatial transformations.
     * @param session Active document session providing selected canvas objects.
     * @param canvasOrigin Logical screen coordinates of the canvas viewport origin.
     */
    void RenderCanvasFloatingHUDs(
        CanvasEngine& canvas,
        DocumentSession& session,
        const ImVec2& canvasOrigin
    );

    /**
     * @brief Renders all diagnostic overlays and interactive modals in top Z-index pass.
     *
     * @param canvas Canvas engine reference.
     * @param stateMachine Input state machine reference.
     * @param windowSM Window state manager reference.
     * @param session Document session reference.
     * @param themeManager Active theme configuration.
     */
    void RenderModalsAndOverlays(
        CanvasEngine& canvas,
        InputStateMachine& stateMachine,
        WindowStateManager& windowSM,
        DocumentSession& session,
        const ThemeManager& themeManager
    );
};

} // namespace Folio
