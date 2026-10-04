/**
 * =========================================================================================
 * @file core/objects/media/audio/audio_overlay_ui.cpp
 * @brief Implementation of Floating Glassmorphic Audio Controller Capsule HUD
 * =========================================================================================
 *
 * MATHEMATICAL WORKING PROCESS & COORDINATE PROJECTION:
 * -----------------------------------------------------
 * 1. World-to-Screen Spatial Anchor:
 *    The audio chip is located in 2D continuous world space (worldX, worldY) with
 *    fixed physical dimensions (worldWidth = 75mm, worldHeight = 22mm).
 *    To ensure the floating HUD maintains an aesthetically balanced margin beneath
 *    the badge invariant to canvas zoom:
 *      anchorWorldX = audio.worldX + audio.worldWidth * 0.5;
 *      anchorWorldY = audio.worldY + audio.worldHeight + 2.5; // 2.5mm physical margin
 *
 * 2. Canvas Transform Projection:
 *    Screen-space viewport coordinates are evaluated via CanvasTransform::WorldToScreen:
 *      p_screen = transform.WorldToScreen(anchorWorldX, anchorWorldY)
 *      screenX = canvasOrigin.x + p_screen.x
 *      screenY = canvasOrigin.y + p_screen.y
 *
 * 3. Centering Offset:
 *    The capsule window is centered horizontally about screenX:
 *      windowPos.x = screenX - (estimatedCapsuleW * 0.5f)
 *      windowPos.y = screenY
 *
 * 4. Micro-Interaction Polish:
 *    - Glassmorphism: RGBA(15, 18, 28, 0.94) background with 18px rounded pill borders.
 *    - Neon Glow Accent: 1.5px violet border RGBA(139, 92, 246, 0.60).
 *    - Interactive scrubber with direct proportional seeking and audio engine synchronization.
 */

#include "core/objects/media/audio/audio_overlay_ui.hpp"
#include "core/objects/media/audio/audio_container.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace Folio {

namespace {

/**
 * @brief Formats fractional seconds into MM:SS timestamp format.
 *
 * @param sec Total elapsed duration in seconds.
 * @param buf Output char buffer.
 * @param bufSize Size of output buffer in bytes.
 */
void FormatMmSs(double sec, char* buf, size_t bufSize) {
    if (sec < 0.0) sec = 0.0;
    int totalSec = static_cast<int>(std::floor(sec));
    int m = totalSec / 60;
    int s = totalSec % 60;
    std::snprintf(buf, bufSize, "%02d:%02d", m, s);
}

} // anonymous namespace

void AudioOverlayUI::Render(AudioObject& audio, const CanvasTransform& transform,
                            const ImVec2& canvasOrigin, std::function<void()> onDirty) {
    // Poll the underlying playback engine for updated progress
    audio.UpdateProgress();

    // 1. Calculate World-to-Screen Projection Anchor
    // Badge bottom-center anchor in world millimeters
    const double anchorWorldX = audio.worldX + audio.worldWidth * 0.5;
    const double anchorWorldY = audio.worldY + audio.worldHeight + 2.5;

    Point2D screenPt = transform.WorldToScreen(anchorWorldX, anchorWorldY);

    // Approximate capsule width for centered horizontal positioning
    const float capsuleWidth = 340.0f;
    const ImVec2 capsuleScreenPos(
        canvasOrigin.x + static_cast<float>(screenPt.x) - (capsuleWidth * 0.5f),
        canvasOrigin.y + static_cast<float>(screenPt.y)
    );

    // 2. Window Position & Window Flags Setup
    ImGui::SetNextWindowPos(capsuleScreenPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(capsuleWidth, 0.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings;

    // 3. Dark Glassmorphism Capsule Theme
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    // Dark slate background with high opacity for readability over canvas strokes
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.08f, 0.12f, 0.94f));
    // Vibrant neon violet border
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.36f, 0.96f, 0.65f));

    char windowId[64];
    std::snprintf(windowId, sizeof(windowId), "##AudioOverlayHUD_%u", audio.uid);

    if (ImGui::Begin(windowId, nullptr, flags)) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);

        // --- Left: Play / Pause Button ---
        bool isPlaying = audio.isPlaying;
        if (isPlaying) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.36f, 0.96f, 0.90f));        // Electric violet
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.45f, 1.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.25f, 0.85f, 1.00f));
            if (ImGui::Button("⏸", ImVec2(28.0f, 26.0f))) {
                audio.Pause();
                if (onDirty) onDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause Playback");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.32f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.32f, 0.55f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.36f, 0.96f, 1.00f));
            if (ImGui::Button("▶", ImVec2(28.0f, 26.0f))) {
                audio.Play();
                if (onDirty) onDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start Playback");
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // --- Stop Button ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.28f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.30f, 0.90f)); // Red on hover
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.15f, 0.20f, 1.00f));
        if (ImGui::Button("⏹", ImVec2(28.0f, 26.0f))) {
            audio.Stop();
            if (onDirty) onDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop & Rewind");
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // --- Center: Interactive Time Scrub Slider ---
        float curSec = static_cast<float>(audio.currentPositionSeconds);
        float totalSec = static_cast<float>(audio.durationSeconds);
        if (totalSec < 0.1f) totalSec = 0.1f;

        char timeDisplay[64];
        char curBuf[16];
        char durBuf[16];
        FormatMmSs(curSec, curBuf, sizeof(curBuf));
        FormatMmSs(totalSec, durBuf, sizeof(durBuf));
        std::snprintf(timeDisplay, sizeof(timeDisplay), "%s / %s", curBuf, durBuf);

        // Scrubber styling
        ImGui::PushItemWidth(150.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.16f, 0.24f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.22f, 0.32f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.24f, 0.26f, 0.38f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.65f, 0.45f, 0.98f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.85f, 0.70f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 0.95f));

        float scrubVal = curSec;
        if (ImGui::SliderFloat("##Scrub", &scrubVal, 0.0f, totalSec, timeDisplay)) {
            double ratio = static_cast<double>(scrubVal) / static_cast<double>(totalSec);
            audio.Seek(std::clamp(ratio, 0.0, 1.0));
            if (onDirty) onDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scrub Playback Position");

        ImGui::PopStyleColor(6);
        ImGui::PopItemWidth();

        ImGui::SameLine();

        // --- Right: Volume Slider / Mute Toggle ---
        bool isMuted = (audio.volume == 0);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.28f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.32f, 0.45f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.35f, 0.60f, 1.00f));
        if (ImGui::Button(isMuted ? "🔇" : "🔊", ImVec2(28.0f, 26.0f))) {
            if (isMuted) {
                audio.SetVolume(100);
            } else {
                audio.SetVolume(0);
            }
            if (onDirty) onDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(isMuted ? "Unmute" : "Mute");
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // Compact mini-volume slider
        ImGui::PushItemWidth(55.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.16f, 0.24f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.55f, 0.36f, 0.96f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.75f, 0.55f, 1.00f, 1.00f));

        int vol = audio.volume;
        if (ImGui::SliderInt("##Vol", &vol, 0, 100, "%d%%")) {
            audio.SetVolume(vol);
            if (onDirty) onDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume: %d%%", audio.volume);

        ImGui::PopStyleColor(3);
        ImGui::PopItemWidth();

        ImGui::PopStyleVar(); // FrameRounding
    }
    ImGui::End();

    ImGui::PopStyleColor(2); // WindowBg, Border
    ImGui::PopStyleVar(4);   // WindowRounding, WindowBorderSize, WindowPadding, ItemSpacing
}

} // namespace Folio
