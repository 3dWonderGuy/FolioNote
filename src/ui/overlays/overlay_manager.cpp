/**
 * =========================================================================================
 * @file ui/overlays/overlay_manager.cpp
 * @brief Implementation of Centralized OverlayManager
 * =========================================================================================
 */

#include "ui/overlays/overlay_manager.hpp"
#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "core/objects/media/audio/audio_container.hpp"
#include "core/objects/media/audio/audio_overlay_ui.hpp"
#include "app/window_state_manager.hpp"
#include "input/input_state_machine.hpp"

namespace Folio {

void OverlayManager::RenderCanvasFloatingHUDs(
    CanvasEngine& canvas,
    DocumentSession& session,
    const ImVec2& canvasOrigin
) {
    // When an AudioObject is selected on the canvas, render a floating modern
    // glassmorphic capsule HUD directly below the badge tracking canvas transform.
    for (const auto& obj : session.GetSelectedObjects()) {
        if (obj && obj->type == ObjectType::Audio) {
            if (auto audioObj = std::dynamic_pointer_cast<Folio::AudioObject>(obj)) {
                Folio::AudioOverlayUI::Render(*audioObj, canvas.transform, canvasOrigin, [&]() {
                    canvas.isDirty = true;
                });
            }
        }
    }
}

void OverlayManager::RenderModalsAndOverlays(
    CanvasEngine& canvas,
    InputStateMachine& stateMachine,
    WindowStateManager& windowSM,
    DocumentSession& session,
    const ThemeManager& themeManager
) {
    // =========================================================================
    // 1. ATTACHMENT FLOW MODAL
    // =========================================================================
    if (canvas.m_attachModalOpen) {
        ImGui::OpenPopup("Attach File##Modal");
    }

    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Attach File##Modal", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
        ImGui::Text("File: %s", canvas.m_pendingAttachName.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("How would you like to attach this file?");
        ImGui::Spacing();

        if (ImGui::Button("Embed in Notebook", ImVec2(-1, 30))) {
            canvas.CommitAttachment(true, canvas.m_pendingAttachSession);
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextColored(themeManager.colorTextMuted, "Copies the file into the notebook sidecar. Portable and safe.");
        ImGui::Spacing();

        if (ImGui::Button("Attach as Link", ImVec2(-1, 30))) {
            canvas.CommitAttachment(false, canvas.m_pendingAttachSession);
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextColored(themeManager.colorTextMuted, "Stores the original absolute path. Breaks if the file is moved.");

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            canvas.m_attachModalOpen = false;
            canvas.m_pendingAttachPath.clear();
            canvas.m_pendingAttachName.clear();
            canvas.m_pendingAttachSession = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    // 2. DIAGNOSTIC TOOLS & DEV STUDIOS
    // =========================================================================
    devTelemetry.Render(canvas, stateMachine, windowSM, session,
                        stateMachine.canvasOriginX, stateMachine.canvasOriginY, themeManager);
    tuningStudio.Render(themeManager);
    toolbarDemo.Render(themeManager);

    // =========================================================================
    // 3. DOCUMENT IMPORT MODAL
    // =========================================================================
    pdfImportModal.Render(session, canvas, themeManager);

    // =========================================================================
    // 4. SPOTLIGHT ACTION COMMAND PALETTE (Ctrl+K / Search)
    // =========================================================================
    commandPalette.Render(themeManager);
}

} // namespace Folio
