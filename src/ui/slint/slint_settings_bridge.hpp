#pragma once
/**
 * =========================================================================================
 * @file ui/slint/slint_settings_bridge.hpp
 * @brief Parallel Slint UI Studio & SDL3 Software-Rendered Bridge for Settings & Window FSM
 * =========================================================================================
 *
 * ARCHITECTURAL OVERVIEW:
 * -----------------------
 * Runs a compiled declarative `.slint` UI (`src/ui/slint/settings_studio.slint`) in
 * parallel alongside FolioNote's existing Dear ImGui + SDL3 workspace.
 *
 * How it works:
 * 1. When `FOLIONOTE_HAS_SLINT` is enabled by CMake, this bridge registers a custom
 *    `slint::platform::Platform` + `slint::platform::WindowAdapter` backed by Slint's
 *    `SoftwareRenderer` (RGB8 pixel buffer).
 * 2. Each frame (when visible), `slint::platform::update_timers_and_animations()` runs,
 *    Slint renders dirty regions into `m_pixelBuffer`, uploads them to an OpenGL texture,
 *    and presents the interactive Slint Studio window inside the main workspace (plus
 *    forwards mouse/pointer/wheel/key events from ImGui/SDL3 to `slint::Window`).
 * 3. All Slint callbacks (`apply-theme-preset`, `toggle-gpu-acceleration`,
 *    `request-toggle-fullscreen`, `select-device-tool`, `save-settings-to-disk`, etc.)
 *    are wired directly to `SettingsManager::Instance()`, `WindowStateManager`,
 *    `ThemeManager`, `CanvasEngine`, and `GpuInkManager::Instance()`.
 * 4. On Android or environments without `third_party/slint`, `FOLIONOTE_HAS_SLINT` is 0
 *    and a lightweight native fallback panel is provided so builds never break.
 */

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include "imgui.h"
#include "app/settings_manager.hpp"
#include "app/theme_manager.hpp"
#include "app/window_state_manager.hpp"
#include "core/canvas_engine/canvas_engine.hpp"
#include "core/gpu/gpu_ink_manager.hpp"
#include "ui/components/ribbon_bar.hpp"
#include "input/input_state_machine.hpp"

#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#if defined(FOLIONOTE_HAS_SLINT) && FOLIONOTE_HAS_SLINT
#include <slint.h>
#include <slint-platform.h>
#include "settings_studio.h"
#endif

namespace Folio {

#if defined(FOLIONOTE_HAS_SLINT) && FOLIONOTE_HAS_SLINT

/**
 * @class EmbeddedSlintWindowAdapter
 * @brief Custom `slint::platform::WindowAdapter` rendering into an offscreen RGB8 buffer
 *        that is uploaded to an OpenGL texture and interactive inside the SDL3 loop.
 */
class EmbeddedSlintWindowAdapter : public slint::platform::WindowAdapter {
public:
    EmbeddedSlintWindowAdapter()
        : m_renderer(slint::platform::SoftwareRenderer::RepaintBufferType::ReusedBuffer) {}

    slint::platform::AbstractRenderer& renderer() override {
        return m_renderer;
    }

    slint::PhysicalSize size() override {
        return m_size;
    }

    void set_size(slint::PhysicalSize newSize) override {
        if (newSize.width > 0 && newSize.height > 0) {
            m_size = newSize;
            m_needsRedraw = true;
        }
    }

    void set_visible(bool visible) override {
        m_visible = visible;
        if (visible) {
            m_needsRedraw = true;
            window().dispatch_scale_factor_change_event(1.0f);
            window().dispatch_resize_event(slint::LogicalSize({
                static_cast<float>(m_size.width),
                static_cast<float>(m_size.height)
            }));
        }
    }

    void request_redraw() override {
        m_needsRedraw = true;
    }

    void ResizeTarget(uint32_t w, uint32_t h) {
        w = std::clamp(w, 420u, 1920u);
        h = std::clamp(h, 320u, 1440u);
        if (m_size.width != w || m_size.height != h) {
            m_size = slint::PhysicalSize({ w, h });
            m_needsRedraw = true;
            window().dispatch_resize_event(slint::LogicalSize({
                static_cast<float>(w),
                static_cast<float>(h)
            }));
        }
    }

    bool RenderIfNeeded(std::vector<slint::Rgb8Pixel>& outRgbBuffer,
                        std::vector<uint8_t>& outRgbaBuffer) {
        const size_t totalPixels = static_cast<size_t>(m_size.width) * static_cast<size_t>(m_size.height);
        if (totalPixels == 0) return false;

        if (outRgbBuffer.size() != totalPixels) {
            outRgbBuffer.resize(totalPixels);
            outRgbaBuffer.resize(totalPixels * 4u, 255u);
            m_needsRedraw = true;
        }

        if (!m_needsRedraw && !window().has_active_animations()) {
            return false;
        }

        m_needsRedraw = false;
        m_renderer.render(std::span<slint::Rgb8Pixel>(outRgbBuffer.data(), outRgbBuffer.size()), m_size.width);

        // Convert RGB8 -> RGBA8 for glTexSubImage2D
        const slint::Rgb8Pixel* src = outRgbBuffer.data();
        uint8_t* dst = outRgbaBuffer.data();
        for (size_t i = 0; i < totalPixels; ++i) {
            dst[i * 4 + 0] = src[i].r;
            dst[i * 4 + 1] = src[i].g;
            dst[i * 4 + 2] = src[i].b;
            dst[i * 4 + 3] = 255u;
        }
        return true;
    }

    bool m_visible = false;
    bool m_needsRedraw = true;
    slint::PhysicalSize m_size = slint::PhysicalSize({ 820u, 580u });
    slint::platform::SoftwareRenderer m_renderer;
};

/**
 * @class EmbeddedSlintPlatform
 * @brief Registers our non-blocking SDL3-compatible `WindowAdapter` with Slint.
 */
class EmbeddedSlintPlatform : public slint::platform::Platform {
public:
    explicit EmbeddedSlintPlatform(EmbeddedSlintWindowAdapter** outAdapterPtr)
        : m_outAdapterPtr(outAdapterPtr) {}

    std::unique_ptr<slint::platform::WindowAdapter> create_window_adapter() override {
        auto adapter = std::make_unique<EmbeddedSlintWindowAdapter>();
        if (m_outAdapterPtr) {
            *m_outAdapterPtr = adapter.get();
        }
        return adapter;
    }

private:
    EmbeddedSlintWindowAdapter** m_outAdapterPtr = nullptr;
};

#endif // FOLIONOTE_HAS_SLINT

/**
 * @class SlintSettingsStudio
 * @brief High-level controller managing the parallel Slint Settings & Window Manager Studio.
 */
class SlintSettingsStudio {
public:
    bool isVisible = false;

    SlintSettingsStudio() = default;
    ~SlintSettingsStudio() {
        Shutdown();
    }

    void Toggle() {
        isVisible = !isVisible;
#if defined(FOLIONOTE_HAS_SLINT) && FOLIONOTE_HAS_SLINT
        if (isVisible) {
            EnsureInitialized();
            if (m_ui) {
                (*m_ui)->show();
                if (m_adapter) m_adapter->m_needsRedraw = true;
            }
        } else if (m_ui) {
            (*m_ui)->hide();
        }
#endif
    }

    void Shutdown() {
        if (m_glTexture != 0) {
            glDeleteTextures(1, &m_glTexture);
            m_glTexture = 0;
        }
#if defined(FOLIONOTE_HAS_SLINT) && FOLIONOTE_HAS_SLINT
        m_ui.reset();
        m_adapter = nullptr;
#endif
    }

    /**
     * @brief Renders the parallel Slint Studio window and synchronizes live state with FolioNote's engine.
     */
    void Render(
        SDL_Window* sdlWindow,
        WindowStateManager& windowSM,
        ThemeManager& themeManager,
        CanvasEngine& canvas,
        RibbonBar& ribbon,
        InputStateMachine& inputSM
    ) {
        if (!isVisible) return;

#if defined(FOLIONOTE_HAS_SLINT) && FOLIONOTE_HAS_SLINT
        EnsureInitialized();
        if (!m_ui || !m_adapter) return;

        BindCallbacksIfNeeded(sdlWindow, windowSM, themeManager, canvas, ribbon, inputSM);
        SyncStateToSlint(windowSM, themeManager, canvas, ribbon);

        ImGui::SetNextWindowSize(ImVec2(840.0f, 615.0f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

        if (ImGui::Begin("Slint Settings & Window Manager Studio [F4]###SlintStudioHost", &isVisible,
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            uint32_t targetW = static_cast<uint32_t>(std::max(420.0f, avail.x));
            uint32_t targetH = static_cast<uint32_t>(std::max(320.0f, avail.y));
            m_adapter->ResizeTarget(targetW, targetH);

            // Advance Slint timers & animations non-blockingly
            slint::platform::update_timers_and_animations();

            // Render Slint scene into RGBA8 buffer and upload to OpenGL texture when dirty
            if (m_adapter->RenderIfNeeded(m_rgbBuffer, m_rgbaBuffer) || m_glTexture == 0) {
                UploadTexture(m_adapter->m_size.width, m_adapter->m_size.height);
            }

            ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
            if (m_glTexture != 0) {
                ImGui::Image((ImTextureID)(intptr_t)m_glTexture,
                             ImVec2(static_cast<float>(m_adapter->m_size.width),
                                    static_cast<float>(m_adapter->m_size.height)));
            }

            // Forward pointer / wheel / keyboard interactions from the host region into Slint
            ForwardInputToSlint(imageOrigin);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
#else
        // Clean Fallback when built without third_party/slint (e.g. Android APK build)
        OverlayThemeScope scope(themeManager);
        ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings & Window Manager Studio [F4]", &isVisible)) {
            ImGui::TextColored(themeManager.colorPrimary, "Settings & Window State Manager");
            ImGui::TextDisabled("(Built without prebuilt Desktop Slint SDK — running native fallback)");
            ImGui::Separator();
            ImGui::Text("Window State: %s (%d x %d)", windowSM.GetStateName(), windowSM.windowWidth, windowSM.windowHeight);
            ImGui::Text("GPU Backend: %s (%s)",
                        GpuInkManager::Instance().GetDriverName(),
                        GpuInkManager::Instance().IsEnabled() ? "Enabled" : "Disabled");
            bool gpuEn = GpuInkManager::Instance().IsEnabled();
            if (ImGui::Checkbox("Enable GPU Stroke Acceleration", &gpuEn)) {
                GpuInkManager::Instance().SetEnabled(gpuEn);
                canvas.isDirty = true;
                canvas.needsFullRebake = true;
            }
            if (ImGui::Button("Save Settings to Disk")) {
                SettingsManager::Instance().Save();
            }
        }
        ImGui::End();
#endif
    }

private:
#if defined(FOLIONOTE_HAS_SLINT) && FOLIONOTE_HAS_SLINT
    inline static bool s_platformRegistered = false;
    EmbeddedSlintWindowAdapter* m_adapter = nullptr;
    std::optional<slint::ComponentHandle<SettingsStudioWindow>> m_ui;
    bool m_callbacksBound = false;
    std::string m_lastStatusMessage = "Synced with FolioNote C++ Engine";

    std::vector<slint::Rgb8Pixel> m_rgbBuffer;
    std::vector<uint8_t> m_rgbaBuffer;

    void EnsureInitialized() {
        if (!s_platformRegistered) {
            slint::platform::set_platform(std::make_unique<EmbeddedSlintPlatform>(&m_adapter));
            s_platformRegistered = true;
        }
        if (!m_ui.has_value()) {
            m_ui.emplace(SettingsStudioWindow::create());
            (*m_ui)->show();
        }
    }

    void BindCallbacksIfNeeded(
        SDL_Window* sdlWindow,
        WindowStateManager& windowSM,
        ThemeManager& themeManager,
        CanvasEngine& canvas,
        RibbonBar& ribbon,
        InputStateMachine& inputSM
    ) {
        if (m_callbacksBound || !m_ui.has_value()) return;
        m_callbacksBound = true;

        auto& win = **m_ui;

        win.on_apply_theme_preset([this, sdlWindow, &themeManager, &canvas](int presetIdx) {
            ThemePreset preset = ThemePreset::FolioColor;
            if (presetIdx == 0) preset = ThemePreset::FolioDark;
            else if (presetIdx == 1) preset = ThemePreset::FolioLight;
            else preset = ThemePreset::FolioColor;

            themeManager.ApplyTheme(preset);
            themeManager.UpdateOSWindowFrame(sdlWindow);
            SettingsManager::Instance().isDarkMode = (preset == ThemePreset::FolioDark);
            canvas.isDirty = true;
            m_lastStatusMessage = "Applied workspace theme preset";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_invert_canvas([this, &ribbon, &canvas](bool inv) {
            ribbon.isCanvasInverted = inv;
            SettingsManager::Instance().isCanvasInverted = inv;
            canvas.SetInkColorInverted(inv);
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
            m_lastStatusMessage = inv ? "Canvas paper & ink inverted (Dark Paper)" : "Canvas paper restored to light";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_verbose_logging([this](bool enabled) {
            SettingsManager::Instance().verboseLogging = enabled;
            Folio::enableVerboseLogging = enabled;
            m_lastStatusMessage = enabled ? "Verbose [INFO] logging enabled" : "Verbose logging silenced";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_select_ribbon_mode([this, &ribbon](slint::SharedString modeStr) {
            std::string m(modeStr.data(), modeStr.size());
            SettingsManager::Instance().ribbonDisplayMode = m;
            if (m == "Collapsed") ribbon.SetDisplayMode(RibbonDisplayMode::Collapsed);
            else if (m == "MiniToolbar") ribbon.SetDisplayMode(RibbonDisplayMode::MiniToolbar);
            else if (m == "FullyHidden") ribbon.SetDisplayMode(RibbonDisplayMode::FullyHidden);
            else ribbon.SetDisplayMode(RibbonDisplayMode::FullRibbon);
            m_lastStatusMessage = "Ribbon layout mode set to " + m;
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_select_device_tool([this, &inputSM, &ribbon](slint::SharedString devStr, slint::SharedString toolStr) {
            std::string dev(devStr.data(), devStr.size());
            std::string tool(toolStr.data(), toolStr.size());

            auto toState = [](const std::string& s) -> InteractionState {
                if (s == "Inking")    return InteractionState::Inking;
                if (s == "Eraser")    return InteractionState::Eraser;
                if (s == "Selecting") return InteractionState::Selecting;
                if (s == "Panning")   return InteractionState::Panning;
                return InteractionState::Selecting;
            };

            if (dev == "Stylus") {
                SettingsManager::Instance().defaultStylusTool = tool;
                inputSM.SetToolForDevice(DeviceType::Stylus, toState(tool));
            } else if (dev == "Touch") {
                SettingsManager::Instance().defaultTouchTool = tool;
                SettingsManager::Instance().drawWithTouch = (tool == "Inking");
                ribbon.drawWithTouch = (tool == "Inking");
                inputSM.SetToolForDevice(DeviceType::Touch, toState(tool));
            } else if (dev == "Mouse") {
                SettingsManager::Instance().defaultMouseTool = tool;
                inputSM.SetToolForDevice(DeviceType::Mouse, toState(tool));
            }
            inputSM.currentAction = inputSM.GetActiveDeviceTool();
            m_lastStatusMessage = "Updated default " + dev + " tool to " + tool;
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_request_toggle_fullscreen([this, sdlWindow]() {
            if (!sdlWindow) return;
            bool fs = (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(sdlWindow, !fs);
            m_lastStatusMessage = !fs ? "Entered Fullscreen Mode" : "Exited Fullscreen Mode";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_request_maximize_window([this, sdlWindow]() {
            if (!sdlWindow) return;
            SDL_MaximizeWindow(sdlWindow);
            m_lastStatusMessage = "Window Maximized";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_request_restore_window([this, sdlWindow]() {
            if (!sdlWindow) return;
            SDL_RestoreWindow(sdlWindow);
            m_lastStatusMessage = "Window Restored";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_request_trigger_freeze_test([this, &windowSM]() {
            windowSM.currentState = WindowState::TransitionFreezing;
            windowSM.freezeFrameCounter = windowSM.DEFAULT_FREEZE_FRAMES;
            m_lastStatusMessage = "Triggered 30-frame WindowStateManager transition freeze guard";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_gpu_acceleration([this, &canvas](bool enabled) {
            GpuInkManager::Instance().SetEnabled(enabled);
            canvas.isDirty = true;
            canvas.needsFullRebake = true;
            m_lastStatusMessage = enabled ? "GPU stroke acceleration enabled" : "Switched to CPU Blend2D fallback";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_draw_with_touch([this, &ribbon, &inputSM](bool enabled) {
            ribbon.drawWithTouch = enabled;
            SettingsManager::Instance().drawWithTouch = enabled;
            inputSM.SetToolForDevice(DeviceType::Touch, enabled ? InteractionState::Inking : InteractionState::Panning);
            m_lastStatusMessage = enabled ? "Draw with Touch enabled" : "Touch restored to Panning";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_ruler([this, &ribbon, &canvas](bool enabled) {
            ribbon.rulerEnabled = enabled;
            SettingsManager::Instance().rulerEnabled = enabled;
            canvas.ruler.SetEnabled(enabled);
            canvas.isDirty = true;
            m_lastStatusMessage = enabled ? "Digital Ruler overlay enabled" : "Digital Ruler hidden";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_auto_shapes([this, &ribbon](bool enabled) {
            ribbon.autoShapesEnabled = enabled;
            SettingsManager::Instance().autoShapesEnabled = enabled;
            m_lastStatusMessage = enabled ? "Automatic Shape Recognition enabled" : "Automatic Shape Recognition disabled";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_wobble_smoothing([this, &canvas](bool enabled) {
            SettingsManager::Instance().inkingConfig.google_wobble_enable = enabled;
            canvas.GetInkEngine().SetConfig(SettingsManager::Instance().inkingConfig);
            m_lastStatusMessage = enabled ? "Wobble smoothing filter enabled" : "Wobble smoothing filter disabled";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_kalman_prediction([this, &canvas](bool enabled) {
            SettingsManager::Instance().inkingConfig.google_enable_prediction = enabled;
            canvas.GetInkEngine().SetConfig(SettingsManager::Instance().inkingConfig);
            m_lastStatusMessage = enabled ? "Kalman tip prediction enabled" : "Kalman tip prediction disabled";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_change_max_loaded_pages([this](int cap) {
            SettingsManager::Instance().maxLoadedPages = static_cast<uint32_t>(std::max(1, cap));
            m_lastStatusMessage = "Working-set LRU cap updated to " + std::to_string(cap) + " pages";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_toggle_backup_tier([this](slint::SharedString tierStr, bool enabled) {
            std::string tier(tierStr.data(), tierStr.size());
            auto& sm = SettingsManager::Instance();
            if (tier == "daily") sm.autoBackupDailyEnabled = enabled;
            else if (tier == "weekly") sm.autoBackupWeeklyEnabled = enabled;
            else if (tier == "monthly") sm.autoBackupMonthlyEnabled = enabled;
            m_lastStatusMessage = "Updated backup tier (" + tier + ")";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_save_settings_to_disk([this]() {
            bool ok = SettingsManager::Instance().Save();
            m_lastStatusMessage = ok
                ? "Saved settings to " + SettingsManager::Instance().GetConfigPath()
                : "Failed to write config/settings.json";
            if (m_adapter) m_adapter->m_needsRedraw = true;
        });

        win.on_close_studio([this]() {
            isVisible = false;
        });
    }

    void SyncStateToSlint(
        const WindowStateManager& windowSM,
        const ThemeManager& themeManager,
        const CanvasEngine& canvas,
        const RibbonBar& ribbon
    ) {
        if (!m_ui.has_value()) return;
        auto& win = **m_ui;
        const auto& sm = SettingsManager::Instance();
        const auto& gpu = GpuInkManager::Instance();

        int presetIdx = 2;
        if (themeManager.currentPreset == ThemePreset::FolioDark) presetIdx = 0;
        else if (themeManager.currentPreset == ThemePreset::FolioLight) presetIdx = 1;

        win.set_theme_preset_index(presetIdx);
        win.set_dark_mode(themeManager.IsDarkMode());
        win.set_invert_canvas(ribbon.isCanvasInverted);
        win.set_verbose_logging(sm.verboseLogging);
        win.set_ribbon_display_mode(slint::SharedString(sm.ribbonDisplayMode));

        win.set_default_stylus_tool(slint::SharedString(sm.defaultStylusTool));
        win.set_default_touch_tool(slint::SharedString(sm.defaultTouchTool));
        win.set_default_mouse_tool(slint::SharedString(sm.defaultMouseTool));

        // WindowStateManager telemetry
        std::string curState = windowSM.GetStateName();
        if (curState != std::string(win.get_window_state_name().data(), win.get_window_state_name().size()) ||
            windowSM.windowWidth != win.get_window_width() ||
            windowSM.windowHeight != win.get_window_height() ||
            windowSM.freezeFrameCounter != win.get_freeze_frame_counter()) {
            if (m_adapter) m_adapter->m_needsRedraw = true;
        }

        win.set_window_state_name(slint::SharedString(curState));
        win.set_previous_state_name(slint::SharedString(windowSM.GetPreviousStateName()));
        win.set_window_width(windowSM.windowWidth);
        win.set_window_height(windowSM.windowHeight);
        win.set_is_fullscreen(windowSM.isFullscreen);
        win.set_is_maximized(windowSM.isMaximized);
        win.set_is_focused(windowSM.isFocused);
        win.set_freeze_frame_counter(windowSM.freezeFrameCounter);
        win.set_canvas_frozen(windowSM.ShouldFreezeCanvasRender());

        // GpuInkManager telemetry
        win.set_gpu_available(gpu.IsAvailable());
        win.set_gpu_enabled(gpu.IsEnabled());
        win.set_gpu_driver_name(slint::SharedString(gpu.GetDriverName()));
        win.set_gpu_last_vertices(static_cast<int>(gpu.GetLastVertexCount()));
        win.set_gpu_last_triangles(static_cast<int>(gpu.GetLastTriangleCount()));
        win.set_gpu_last_draw_calls(static_cast<int>(gpu.GetLastDrawCallCount()));
        size_t totalGpuBytes = gpu.GetVertexCapacityBytes() + gpu.GetIndexCapacityBytes() + gpu.GetTextureMemoryBytes();
        win.set_gpu_vram_kb(static_cast<int>(totalGpuBytes / 1024u));

        win.set_draw_with_touch(ribbon.drawWithTouch);
        win.set_ruler_enabled(canvas.ruler.IsEnabled());
        win.set_auto_shapes_enabled(ribbon.autoShapesEnabled);
        win.set_wobble_smoothing_enabled(sm.inkingConfig.google_wobble_enable);
        win.set_kalman_prediction_enabled(sm.inkingConfig.google_enable_prediction);

        win.set_max_loaded_pages(static_cast<int>(sm.maxLoadedPages));
        win.set_auto_backup_daily(sm.autoBackupDailyEnabled);
        win.set_auto_backup_weekly(sm.autoBackupWeeklyEnabled);
        win.set_auto_backup_monthly(sm.autoBackupMonthlyEnabled);
        win.set_status_message(slint::SharedString(m_lastStatusMessage));
    }

    void ForwardInputToSlint(const ImVec2& imageOrigin) {
        if (!m_adapter) return;
        auto& slintWin = m_adapter->window();
        ImGuiIO& io = ImGui::GetIO();

        const bool hovered = ImGui::IsItemHovered();
        const float relX = io.MousePos.x - imageOrigin.x;
        const float relY = io.MousePos.y - imageOrigin.y;
        slint::LogicalPosition pos({ relX, relY });

        if (hovered) {
            if (relX != m_lastMouseX || relY != m_lastMouseY) {
                m_lastMouseX = relX;
                m_lastMouseY = relY;
                slintWin.dispatch_pointer_move_event(pos);
                m_adapter->m_needsRedraw = true;
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                slintWin.dispatch_pointer_press_event(pos, slint::PointerEventButton::Left);
                m_adapter->m_needsRedraw = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                slintWin.dispatch_pointer_release_event(pos, slint::PointerEventButton::Left);
                m_adapter->m_needsRedraw = true;
            }

            if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) {
                slintWin.dispatch_pointer_scroll_event(pos, io.MouseWheelH * 28.0f, io.MouseWheel * 28.0f);
                m_adapter->m_needsRedraw = true;
            }
        } else if (m_wasHovered) {
            slintWin.dispatch_pointer_exit_event();
            m_adapter->m_needsRedraw = true;
        }
        m_wasHovered = hovered;
    }

    void UploadTexture(uint32_t w, uint32_t h) {
        if (w == 0 || h == 0 || m_rgbaBuffer.empty()) return;

        if (m_glTexture == 0) {
            glGenTextures(1, &m_glTexture);
        }

        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glBindTexture(GL_TEXTURE_2D, m_glTexture);

        if (m_texW != w || m_texH != h) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, m_rgbaBuffer.data());
            m_texW = w;
            m_texH = h;
        } else {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                            GL_RGBA, GL_UNSIGNED_BYTE, m_rgbaBuffer.data());
        }

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    }

    float m_lastMouseX = -1.0f;
    float m_lastMouseY = -1.0f;
    bool m_wasHovered = false;
#endif

    GLuint m_glTexture = 0;
    uint32_t m_texW = 0;
    uint32_t m_texH = 0;
};

} // namespace Folio
