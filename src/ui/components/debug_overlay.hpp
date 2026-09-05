#pragma once
#include "imgui.h"
#include "core/engine/canvas_engine.hpp"
#include "input/input_state_machine.hpp"
#include "app/window_state_manager.hpp"
#include "utils/file_logger.hpp"
#include <deque>
#include <string>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

enum class LogCategory {
    System,
    Window,
    Input,
    Engine
};

// LogEntry is no longer needed since we use FolioLogEntry

class DebugOverlay {
public:


    bool isVisible = true;

    // Filter controls
    int selectedCategoryFilter = 0; // 0 = All, 1 = System, 2 = Window, 3 = Input, 4 = Engine
    char searchFilterText[64] = "";
    bool autoScroll = true;

    float frameTimeHistory[120] = { 0.0f };
    int frameTimeOffset = 0;

    // Hardware Digitizer / Stylus Telemetry Counters
    uint32_t inputEventsThisSecond = 0;
    uint32_t currentInputSamplingHz = 0;
    double lastSecondTimestamp = 0.0;
    double lastInputPacketTime = 0.0;
    double currentInputIntervalMs = 0.0;

    // Previous states for edge-triggered logging
    WindowState lastWindowState = WindowState::Stable;
    bool lastWindowStateFullscreen = false; 
    bool lastWindowStateMaximized = false; 
    InteractionState lastStylusAction = InteractionState::Inking;
    StylusState lastStylusState = StylusState::OutOfRange;
    DeviceType lastDeviceType = DeviceType::Mouse;

    void RecordInputPacket(double timestampSec) {
        inputEventsThisSecond++;
        if (lastInputPacketTime > 0.0) {
            currentInputIntervalMs = (timestampSec - lastInputPacketTime) * 1000.0;
        }
        lastInputPacketTime = timestampSec;

        if (timestampSec - lastSecondTimestamp >= 1.0) {
            currentInputSamplingHz = inputEventsThisSecond;
            inputEventsThisSecond = 0;
            lastSecondTimestamp = timestampSec;
        }
    }

    void LogEvent(const std::string& msg, LogCategory cat = LogCategory::System) {
        switch (cat) {
            case LogCategory::Window: LOG_INFO(Window, msg); break;
            case LogCategory::Input:  LOG_INFO(InputManager, msg); break;
            case LogCategory::Engine: LOG_INFO(CanvasEngine, msg); break;
            default:                  LOG_INFO(General, msg); break;
        }
    }

    void UpdateStateTracking(const WindowStateManager& windowState, const InputStateMachine& inputState) {
        if (windowState.currentState != lastWindowState) {
            std::string msg = std::string("Window State: ") + windowState.GetStateName();
            LogEvent(msg, LogCategory::Window);
            lastWindowState = windowState.currentState;
        }

        if (windowState.isFullscreen != lastWindowStateFullscreen) {
            std::string msg = windowState.isFullscreen ? "Mode: Entered TRUE Fullscreen" : "Mode: Exited TRUE Fullscreen";
            LogEvent(msg, LogCategory::Window);
            lastWindowStateFullscreen = windowState.isFullscreen;
        }

        if (windowState.isMaximized != lastWindowStateMaximized) {
            std::string msg = windowState.isMaximized ? "Mode: Window Maximized" : "Mode: Window Un-Maximized";
            LogEvent(msg, LogCategory::Window);
            lastWindowStateMaximized = windowState.isMaximized;
        }

        if (inputState.currentAction != lastStylusAction) {
            const char* names[] = { "Idle", "Inking", "Eraser", "Selecting", "Panning", "Transforming" };
            int idx = static_cast<int>(inputState.currentAction);
            if (idx >= 0 && idx < 6) {
                std::string msg = std::string("Stylus Tool State: ") + names[idx];
                LogEvent(msg, LogCategory::Input);
            }
            lastStylusAction = inputState.currentAction;
        }

        if (inputState.currentStylusState != lastStylusState) {
            const char* sNames[] = { "OutOfRange", "Hovering", "Engaged (Down)" };
            int idx = static_cast<int>(inputState.currentStylusState);
            if (idx >= 0 && idx < 3) {
                std::string msg = std::string("Stylus Contact: ") + sNames[idx];
                LogEvent(msg, LogCategory::Input);
            }
            lastStylusState = inputState.currentStylusState;
        }

        if (inputState.ActiveDevice != lastDeviceType) {
            const char* devNames[] = { "Unknown", "Stylus", "Touch", "Mouse" };
            int idx = static_cast<int>(inputState.ActiveDevice);
            if (idx >= 0 && idx < 4) {
                std::string msg = std::string("Active Device: ") + devNames[idx];
                LogEvent(msg, LogCategory::Input);
            }
            lastDeviceType = inputState.ActiveDevice;
        }
    }

    void Render(CanvasEngine& canvas, const InputStateMachine& inputState, const WindowStateManager& windowState, float canvasScreenX, float canvasScreenY) {
        if (!isVisible) return;

        ImGui::SetNextWindowSize(ImVec2(560, 540), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(20, 160), ImGuiCond_FirstUseEver);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.95f));
        ImGui::Begin("Developer Diagnostics [F3]", &isVisible);

        if (ImGui::BeginTabBar("DevDiagnosticsTabs", ImGuiTabBarFlags_None)) {
            
            // TAB 1: PERFORMANCE & BUS BANDWIDTH
            if (ImGui::BeginTabItem("Performance")) {
                ImGuiIO& io = ImGui::GetIO();
                float currentFps = io.Framerate > 0.0f ? io.Framerate : 120.0f;
                float currentLatencyMs = 1000.0f / currentFps;
                frameTimeHistory[frameTimeOffset] = currentLatencyMs;
                frameTimeOffset = (frameTimeOffset + 1) % 120;

                ImGui::Text("FPS: %.1f | Frame Latency: %.2f ms", currentFps, currentLatencyMs);
                ImGui::PlotLines("##FrameGraph", frameTimeHistory, 120, frameTimeOffset, "Frame Latency (ms)", 0.0f, 33.0f, ImVec2(0, 50));
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "PCIe / GPU MEMORY BANDWIDTH (20%% LOAD EXPLANATION)");
                double frameBytesMB = (canvas.viewportW * canvas.viewportH * 4.0) / (1024.0 * 1024.0);
                double bandwidthMBps = frameBytesMB * currentFps;
                ImGui::Text("1:1 Render Target  : %dx%d (%.2f MB PRGB32 Buffer)", canvas.viewportW, canvas.viewportH, frameBytesMB);
                ImGui::Text("PCIe Bus Transfer  : %.1f MB/sec (Full Texture Upload)", bandwidthMBps);

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "WINDOW LIFECYCLE STATE MACHINE");
                ImGui::Text("Active State       : %s", windowState.GetStateName());
                ImGui::Text("Transition Freeze  : %s (Remaining: %d frames)", 
                            windowState.ShouldFreezeCanvasRender() ? "ACTIVE (VRAM Protected)" : "IDLE", 
                            windowState.freezeFrameCounter);
                ImGui::Text("Window Frame       : %dx%d (Fullscreen: %s | Maximized: %s)", 
                            windowState.windowWidth, windowState.windowHeight,
                            windowState.isFullscreen ? "YES" : "NO",
                            windowState.isMaximized ? "YES" : "NO");

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "MEMORY ALLOCATION");
#if defined(_WIN32)
                PROCESS_MEMORY_COUNTERS memCounters;
                if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounters, sizeof(memCounters))) {
                    ImGui::Text("Private Working Set: %.2f MB", static_cast<double>(memCounters.WorkingSetSize) / (1024.0 * 1024.0));
                }
#endif
                ImGui::EndTabItem();
            }

            // TAB 2: INPUT STATE MACHINE & HARDWARE SAMPLING TELEMETRY
            if (ImGui::BeginTabItem("Telemetry")) {
                const char* stylusStateNames[] = { "OutOfRange", "Hovering", "Engaged (Down)" };
                const char* toolNames[] = { "Idle", "Inking", "Eraser", "Selecting", "Panning", "Transforming" };
                const char* deviceNames[] = { "Unknown", "Active Stylus Pen", "Touch Capacitive", "Standard Mouse" };
                
                int sIdx = std::clamp(static_cast<int>(inputState.currentStylusState), 0, 2);
                int tIdx = std::clamp(static_cast<int>(inputState.currentAction), 0, 5);
                int dIdx = std::clamp(static_cast<int>(inputState.ActiveDevice), 0, 3);

                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "HARDWARE SAMPLING FREQUENCY");
                ImGui::Text("Digitizer Polling Rate : %u Hz (Packets/sec)", currentInputSamplingHz);
                ImGui::Text("Hardware Packet Delta  : %.2f ms", currentInputIntervalMs);

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "INPUT STATE MACHINE");
                ImGui::Text("Active Device      : %s", deviceNames[dIdx]);
                ImGui::Text("Stylus Contact     : %s", stylusStateNames[sIdx]);
                ImGui::Text("Stylus Action Tool : %s", toolNames[tIdx]);

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "DIGITIZER SENSORS");
                ImGui::Text("Pen Screen Pos     : (%.1f, %.1f) px", inputState.pen.x, inputState.pen.y);
                ImGui::Text("Active Pen Pressure: %.4f (ADC: %.0f / 4096)", inputState.pen.pressure, inputState.pen.pressure * 4096.0f);
                ImGui::ProgressBar(inputState.pen.pressure, ImVec2(-FLT_MIN, 0));

                ImGui::Text("Tilt Angles        : X: %.1f deg | Y: %.1f deg", inputState.pen.tiltX, inputState.pen.tiltY);
                ImGui::Text("Barrel Button 1    : %s (Eraser Shortcut)", inputState.pen.barrel1 ? "PRESSED" : "Released");
                ImGui::Text("Barrel Button 2    : %s (Lasso Shortcut)", inputState.pen.barrel2 ? "PRESSED" : "Released");
                ImGui::Text("Eraser Tail Tip    : %s", inputState.pen.eraserTip ? "INVERTED (Active)" : "Normal");

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "MULTI-TOUCH & MOUSE");
                size_t activeFingerCount = std::count_if(inputState.activeFingers.begin(), inputState.activeFingers.end(), 
                                                         [](const auto& slot) { return slot.fingerID != -1; });
                ImGui::Text("Active Fingers     : %zu", activeFingerCount);
                ImGui::Text("Mouse Buttons      : L:%d M:%d R:%d", inputState.mouse.leftButton, inputState.mouse.middleButton, inputState.mouse.rightButton);
                ImGui::Text("Keyboard Modifiers : Ctrl:%d Shift:%d Alt:%d Space:%d", 
                            inputState.keyboard.ctrl, inputState.keyboard.shift, inputState.keyboard.alt, inputState.keyboard.space);

                ImGui::EndTabItem();
            }

            // TAB 3: SPATIAL & RASTER ENGINE
            if (ImGui::BeginTabItem("Spatial & Engine")) {
                ImVec2 rawMouse = ImGui::GetMousePos();
                double canvasLocalX = rawMouse.x - canvasScreenX;
                double canvasLocalY = rawMouse.y - canvasScreenY;
                Point2D worldPt = canvas.transform.ScreenToWorld(canvasLocalX, canvasLocalY);

                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "COORDINATE MAPPING (1:1 PIXEL SPACE)");
                ImGui::Text("Window Absolute Pos: (%.1f, %.1f) px", rawMouse.x, rawMouse.y);
                ImGui::Text("Canvas Viewport Pos: (%.1f, %.1f) px", canvasLocalX, canvasLocalY);
                ImGui::Text("Canvas World Space : (%.1f, %.1f) units", worldPt.x, worldPt.y);
                ImGui::Text("View Transform     : Pan(%.1f, %.1f mm) | Zoom: %.2fx", canvas.transform.panXMm, canvas.transform.panYMm, canvas.transform.zoom);

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "OBJECT GRAPH & RASTER STATE");
                ImGui::Text("Total Objects (History) : N/A");
                ImGui::Text("Active In-Flight Points : %zu", canvas.liveLayer.activeStrokePoints.size());
                ImGui::Text("Static Layer Full Rebake: %s", canvas.needsFullRebake ? "PENDING (O(N) trigger)" : "CLEAN (O(1) composite)");
                ImGui::Text("GPU Dirty Flag          : %s", canvas.isDirty ? "DIRTY (Pending upload)" : "IDLE");

                ImGui::EndTabItem();
            }

            // TAB 4: FILTERABLE EVENT LOGS
            if (ImGui::BeginTabItem("Logs")) {
                ImGui::Text("Filter Source:");
                ImGui::SameLine();
                const char* categories[] = { "All", "General", "FileLoader", "CanvasEngine", "RTree", "DBManager", "PageRepository", "Input", "Window" };
                ImGui::PushItemWidth(130);
                ImGui::Combo("##CategoryFilter", &selectedCategoryFilter, categories, IM_ARRAYSIZE(categories));
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::PushItemWidth(140);
                ImGui::InputTextWithHint("##SearchLog", "Search text...", searchFilterText, sizeof(searchFilterText));
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::Checkbox("Auto-Scroll", &autoScroll);

                ImGui::Separator();

                ImGui::BeginChild("##LogScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
                
                std::string searchLower = searchFilterText;
                std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

                auto history = ::Folio::FileLogger::Instance().GetHistory();
                for (const auto& entry : history) {
                    if (selectedCategoryFilter > 0) {
                        const char* filterSource = categories[selectedCategoryFilter];
                        if (entry.source != filterSource) {
                            continue;
                        }
                    }

                    if (!searchLower.empty()) {
                        std::string msgLower = entry.message;
                        std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);
                        if (msgLower.find(searchLower) == std::string::npos) continue;
                    }

                    ImVec4 levelColor;
                    if (entry.level == "ERROR") {
                        levelColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red
                    } else if (entry.level == "WARN") {
                        levelColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Yellow
                    } else {
                        levelColor = ImVec4(0.4f, 0.8f, 1.0f, 1.0f); // Cyan
                    }

                    ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(levelColor, "[%s]", entry.level.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[%s]", entry.source.c_str());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(entry.message.c_str());
                }

                if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
        ImGui::PopStyleColor();
    }
};