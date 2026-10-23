/**
 * =========================================================================================
 * @file tests/benchmarks/gpu_ink_benchmark.cpp
 * @brief Dedicated GPU Performance Benchmark and Stress-Test Suite for FolioNote
 * =========================================================================================
 *
 * Validates GpuInkManager hardware acceleration under varying synthetic stroke densities:
 *   - Light: ~50 strokes (~10k vertices)
 *   - Typical Note Page: ~500 strokes (~100k vertices)
 *   - Extreme Stress Test: ~5,000 strokes (~1.5M vertices)
 *
 * Execution Modes:
 *   - Headless Micro-benchmark: Runs 100 iterations per tier and prints Min/Max/P50/P99 latency.
 *   - Interactive Live Mode (--interactive): Opens a real-time window with ImGui telemetry overlay.
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>
#include <string>

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"

#include <blend2d/blend2d.h>
#include "core/gpu/gpu_ink_manager.hpp"
#include "input/pen_palette.hpp"
#include "folio_sketch/ink_mesh.hpp"
#include "folio_sketch/ink_stroke_data.hpp"

namespace {

constexpr int VIEWPORT_W = 3840;
constexpr int VIEWPORT_H = 2160;

enum class DensityTier {
    Light,
    Typical,
    Extreme
};

struct TierConfig {
    const char* name;
    int strokeCount;
    int minPtsPerStroke;
    int maxPtsPerStroke;
    const char* expectedVertices;
};

const TierConfig TIERS[] = {
    { "Light",                50,   25,  40,  "~10k vertices"   },
    { "Typical Note Page",   500,   30,  50,  "~100k vertices"  },
    { "Extreme Stress Test", 5000,  30,  60,  "~1.5M vertices"  }
};

struct SyntheticStroke {
    std::vector<StrokePoint> points;
    CapType capType = CapType::Round;
    uint32_t packedColor = 0xFF181A20;
    BlendMode blendMode = BlendMode::Normal;
    float opacity = 1.0f;
    Folio::InkMesh mesh;
};

struct IterationMetrics {
    double beginBatchUs = 0.0;
    double submitMeshesUs = 0.0;
    double flushToSurfaceUs = 0.0;
    double totalFrameUs = 0.0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t drawCalls = 0;
    size_t stagingBytes = 0;
};

// Generates synthetic trajectories spanning 4K canvas (cursive loops, sawtooth, random arcs)
std::vector<SyntheticStroke> GenerateWorkload(const TierConfig& tier, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> posX(100.0, VIEWPORT_W - 100.0);
    std::uniform_real_distribution<double> posY(100.0, VIEWPORT_H - 100.0);
    std::uniform_int_distribution<int> strokePtsDist(tier.minPtsPerStroke, tier.maxPtsPerStroke);
    std::uniform_int_distribution<int> typeDist(0, 2);
    std::uniform_real_distribution<float> radiusDist(1.0f, 6.0f);
    std::uniform_real_distribution<float> pressureDist(0.2f, 1.0f);
    std::uniform_int_distribution<uint32_t> colorDist(0x20, 0xEE);

    std::vector<SyntheticStroke> strokes;
    strokes.reserve(tier.strokeCount);

    for (int s = 0; s < tier.strokeCount; ++s) {
        SyntheticStroke stroke;
        const int numPts = strokePtsDist(rng);
        const int patternType = typeDist(rng);

        const double startX = posX(rng);
        const double startY = posY(rng);

        stroke.points.reserve(numPts);

        uint32_t r = colorDist(rng);
        uint32_t g = colorDist(rng);
        uint32_t b = colorDist(rng);
        BLRgba32 blColor(r, g, b, 255);
        stroke.packedColor = Folio::PackGpuColor(blColor);

        if (s % 7 == 0) {
            stroke.blendMode = BlendMode::Multiply;
            stroke.opacity = 0.75f;
        } else {
            stroke.blendMode = BlendMode::Normal;
            stroke.opacity = 1.0f;
        }

        if (patternType == 0) {
            // Cursive loop trajectory
            double curX = startX;
            double curY = startY;
            double loopRadius = 20.0 + (s % 30);
            for (int p = 0; p < numPts; ++p) {
                double angle = p * 0.25;
                double px = curX + std::cos(angle) * loopRadius + p * 3.0;
                double py = curY + std::sin(angle * 2.0) * loopRadius;
                stroke.points.emplace_back(px, py, radiusDist(rng), pressureDist(rng));
            }
        } else if (patternType == 1) {
            // Tight sawtooth trajectory (high-frequency turns)
            double curX = startX;
            double curY = startY;
            for (int p = 0; p < numPts; ++p) {
                double px = curX + p * 6.0;
                double py = curY + ((p % 2 == 0) ? -25.0 : 25.0);
                stroke.points.emplace_back(px, py, radiusDist(rng), pressureDist(rng));
            }
        } else {
            // Smooth natural arc with variable radius
            double curX = startX;
            double curY = startY;
            double dx = (rng() % 2 == 0 ? 1.0 : -1.0) * (3.0 + (s % 5));
            double dy = 2.0 + (s % 4);
            for (int p = 0; p < numPts; ++p) {
                double px = curX + p * dx + std::sin(p * 0.1) * 15.0;
                double py = curY + p * dy + std::cos(p * 0.1) * 15.0;
                stroke.points.emplace_back(px, py, radiusDist(rng), pressureDist(rng));
            }
        }

        // Direct mesh construction (zero BLPath overhead)
        Folio::InkMeshBuilder::BuildSolidMesh(
            stroke.points,
            stroke.capType,
            stroke.packedColor,
            stroke.mesh,
            1.0f // 1:1 scale for viewport coordinate mapping
        );

        strokes.push_back(std::move(stroke));
    }

    return strokes;
}

IterationMetrics ExecuteFrame(Folio::GpuInkManager& gpu,
                              BLImage& surface,
                              const std::vector<SyntheticStroke>& strokes,
                              int clipX = 0, int clipY = 0,
                              int clipW = VIEWPORT_W, int clipH = VIEWPORT_H) {
    IterationMetrics metrics;

    auto t0 = std::chrono::high_resolution_clock::now();
    gpu.BeginBatch();
    auto t1 = std::chrono::high_resolution_clock::now();

    BLMatrix2D identity = BLMatrix2D::make_identity();
    uint32_t strokeId = 1;

    for (const auto& stroke : strokes) {
        if (!stroke.mesh.empty()) {
            gpu.SubmitMesh(stroke.mesh, identity, 1.0f, stroke.opacity, stroke.blendMode, strokeId++);
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    gpu.FlushToSurface(surface, clipX, clipY, clipW, clipH);
    auto t3 = std::chrono::high_resolution_clock::now();

    metrics.beginBatchUs     = std::chrono::duration<double, std::micro>(t1 - t0).count();
    metrics.submitMeshesUs   = std::chrono::duration<double, std::micro>(t2 - t1).count();
    metrics.flushToSurfaceUs = std::chrono::duration<double, std::micro>(t3 - t2).count();
    metrics.totalFrameUs     = std::chrono::duration<double, std::micro>(t3 - t0).count();

    metrics.vertexCount   = gpu.GetLastVertexCount();
    metrics.triangleCount = gpu.GetLastTriangleCount();
    metrics.drawCalls     = gpu.GetLastDrawCallCount();
    metrics.stagingBytes  = metrics.vertexCount * sizeof(Folio::InkVertex) + metrics.triangleCount * 3 * sizeof(uint32_t);

    return metrics;
}

void PrintSummaryTable(const std::string& tierName,
                       const std::vector<IterationMetrics>& records,
                       size_t vramBytes) {
    if (records.empty()) return;

    std::vector<double> totals;
    std::vector<double> flushes;
    totals.reserve(records.size());
    flushes.reserve(records.size());

    double sumTotal = 0.0;
    double sumFlush = 0.0;
    double sumSubmit = 0.0;

    for (const auto& r : records) {
        totals.push_back(r.totalFrameUs);
        flushes.push_back(r.flushToSurfaceUs);
        sumTotal += r.totalFrameUs;
        sumFlush += r.flushToSurfaceUs;
        sumSubmit += r.submitMeshesUs;
    }

    std::sort(totals.begin(), totals.end());
    std::sort(flushes.begin(), flushes.end());

    size_t count = totals.size();
    double minUs = totals.front();
    double maxUs = totals.back();
    double p50Us = totals[count * 50 / 100];
    double p95Us = totals[count * 95 / 100];
    double p99Us = totals[count * 99 / 100];
    double avgUs = sumTotal / count;

    double verticesM = static_cast<double>(records[0].vertexCount) / 1e6;
    double throughputMVertPerSec = (avgUs > 0.0) ? (verticesM / (avgUs * 1e-6)) : 0.0;
    double stagingMB = static_cast<double>(records[0].stagingBytes) / (1024.0 * 1024.0);
    double vramMB = static_cast<double>(vramBytes) / (1024.0 * 1024.0);

    std::cout << "\n================================================================================\n";
    std::cout << " BENCHMARK RESULTS: " << tierName << "\n";
    std::cout << "================================================================================\n";
    std::cout << std::left << std::setw(28) << "Staged Vertices:"       << records[0].vertexCount << "\n";
    std::cout << std::left << std::setw(28) << "Triangles:"             << records[0].triangleCount << "\n";
    std::cout << std::left << std::setw(28) << "Draw Calls:"            << records[0].drawCalls << "\n";
    std::cout << std::left << std::setw(28) << "PCIe Geometry Staging:" << std::fixed << std::setprecision(2) << stagingMB << " MB/frame\n";
    std::cout << std::left << std::setw(28) << "GPU Buffers VRAM:"     << std::fixed << std::setprecision(2) << vramMB << " MB\n";
    std::cout << std::left << std::setw(28) << "Throughput:"            << std::fixed << std::setprecision(2) << throughputMVertPerSec << " MVert/sec\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << " Latency (Total Pipeline: Batch + Submit + GPU Flush & Fence Wait):\n";
    std::cout << "   Min:   " << std::right << std::setw(8) << std::fixed << std::setprecision(1) << minUs << " us  (" << minUs / 1000.0 << " ms)\n";
    std::cout << "   P50:   " << std::right << std::setw(8) << p50Us << " us  (" << p50Us / 1000.0 << " ms)\n";
    std::cout << "   Avg:   " << std::right << std::setw(8) << avgUs << " us  (" << avgUs / 1000.0 << " ms)\n";
    std::cout << "   P95:   " << std::right << std::setw(8) << p95Us << " us  (" << p95Us / 1000.0 << " ms)\n";
    std::cout << "   P99:   " << std::right << std::setw(8) << p99Us << " us  (" << p99Us / 1000.0 << " ms)\n";
    std::cout << "   Max:   " << std::right << std::setw(8) << maxUs << " us  (" << maxUs / 1000.0 << " ms)\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << " Stage Breakdown (Average):\n";
    std::cout << "   BeginBatch:       " << std::setw(8) << (sumTotal - sumSubmit - sumFlush) / count << " us\n";
    std::cout << "   SubmitMeshes:     " << std::setw(8) << sumSubmit / count << " us\n";
    std::cout << "   FlushToSurface:   " << std::setw(8) << sumFlush / count << " us\n";
    std::cout << "================================================================================\n";
}

int RunHeadlessBenchmark(Folio::GpuInkManager& gpu) {
    std::cout << "Initializing 4K backing surface (3840x2160 PRGB32)...\n";
    BLImage surface(VIEWPORT_W, VIEWPORT_H, BL_FORMAT_PRGB32);
    if (surface.is_empty()) {
        std::cerr << "Failed to allocate 4K Blend2D surface.\n";
        return 1;
    }

    constexpr int ITERATIONS = 100;
    constexpr int WARMUP_FRAMES = 10;

    for (const auto& tier : TIERS) {
        std::cout << "\n[Tier: " << tier.name << "] Generating " << tier.strokeCount
                  << " strokes (" << tier.expectedVertices << ")..." << std::flush;
        auto workload = GenerateWorkload(tier);
        std::cout << " Done.\n";

        std::cout << "  -> Warming up GPU pipeline (" << WARMUP_FRAMES << " passes)..." << std::flush;
        for (int w = 0; w < WARMUP_FRAMES; ++w) {
            ExecuteFrame(gpu, surface, workload);
        }
        std::cout << " Done.\n";

        std::cout << "  -> Executing " << ITERATIONS << " benchmark iterations..." << std::flush;
        std::vector<IterationMetrics> records;
        records.reserve(ITERATIONS);

        for (int i = 0; i < ITERATIONS; ++i) {
            records.push_back(ExecuteFrame(gpu, surface, workload));
        }
        std::cout << " Complete.\n";

        size_t totalVram = gpu.GetVertexCapacityBytes() +
                           gpu.GetIndexCapacityBytes() +
                           gpu.GetImageTransferCapacityBytes() * 2 +
                           gpu.GetTextureMemoryBytes() +
                           gpu.GetDepthStencilMemoryBytes();

        PrintSummaryTable(tier.name, records, totalVram);
    }

    return 0;
}

int RunInteractiveMode(Folio::GpuInkManager& gpu) {
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return 1;
        }
    }

    SDL_Window* window = SDL_CreateWindow(
        "FolioNote GPU Ink Performance Benchmark (4K Stress Viewport)",
        1600, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(0); // Uncapped framerate for benchmarking

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    BLImage surface(VIEWPORT_W, VIEWPORT_H, BL_FORMAT_PRGB32);

    int currentTierIdx = 1; // Default to Typical Note Page
    auto workload = GenerateWorkload(TIERS[currentTierIdx]);

    GLuint canvasTexture = 0;
    glGenTextures(1, &canvasTexture);
    glBindTexture(GL_TEXTURE_2D, canvasTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    bool running = true;
    std::vector<float> frameTimesHistory(180, 0.0f);
    int historyOffset = 0;

    IterationMetrics lastMetrics;
    bool animateStrokes = true;
    float animPhase = 0.0f;

    std::cout << "[Interactive Mode] Running interactive telemetry window. Press ESC or close window to exit.\n";

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        // Animate stroke wave positions to test dynamic submission
        if (animateStrokes) {
            animPhase += 0.03f;
            for (size_t i = 0; i < workload.size(); ++i) {
                if (!workload[i].points.empty()) {
                    double offset = std::sin(animPhase + i * 0.1) * 1.5;
                    workload[i].points[0].x += offset;
                }
            }
        }

        lastMetrics = ExecuteFrame(gpu, surface, workload);

        frameTimesHistory[historyOffset] = static_cast<float>(lastMetrics.totalFrameUs / 1000.0);
        historyOffset = (historyOffset + 1) % frameTimesHistory.size();

        // Upload composited Blend2D frame to OpenGL preview texture
        BLImageData imgData = {};
        surface.get_data(&imgData);
        glBindTexture(GL_TEXTURE_2D, canvasTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIEWPORT_W, VIEWPORT_H, 0, GL_BGRA, GL_UNSIGNED_BYTE, imgData.pixel_data);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Render Telemetry HUD Window
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
        ImGui::Begin("FolioNote GPU Ink Telemetry HUD", nullptr);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "SDL_GPU Hardware Rasterizer Performance");
        ImGui::Text("Backend Driver: %s", gpu.GetDriverName());
        ImGui::Separator();

        ImGui::Text("Workload Density Tier:");
        for (int t = 0; t < 3; ++t) {
            if (ImGui::RadioButton(TIERS[t].name, currentTierIdx == t)) {
                currentTierIdx = t;
                workload = GenerateWorkload(TIERS[currentTierIdx]);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();

        ImGui::Checkbox("Animate Dynamic Strokes", &animateStrokes);
        ImGui::Separator();

        ImGui::Text("Frame Metrics:");
        ImGui::Text("  Total Dispatch Time:  %.2f ms (%.0f FPS)", lastMetrics.totalFrameUs / 1000.0, 1e6 / std::max(1.0, lastMetrics.totalFrameUs));
        ImGui::Text("  BeginBatch Time:      %.2f us", lastMetrics.beginBatchUs);
        ImGui::Text("  SubmitMesh Time:      %.2f us", lastMetrics.submitMeshesUs);
        ImGui::Text("  FlushToSurface Time:  %.2f ms", lastMetrics.flushToSurfaceUs / 1000.0);
        ImGui::Separator();

        ImGui::Text("Geometry Throughput:");
        ImGui::Text("  Staged Vertices:      %u", lastMetrics.vertexCount);
        ImGui::Text("  Triangles Rasterized: %u", lastMetrics.triangleCount);
        ImGui::Text("  Batched Draw Calls:   %u", lastMetrics.drawCalls);
        double throughput = (lastMetrics.totalFrameUs > 0) ? (lastMetrics.vertexCount / (lastMetrics.totalFrameUs * 1e-6) / 1e6) : 0.0;
        ImGui::Text("  Rate:                 %.2f MVert/sec", throughput);
        ImGui::Separator();

        ImGui::Text("Memory Footprint:");
        ImGui::Text("  Geometry Staging:     %.2f MB", static_cast<double>(lastMetrics.stagingBytes) / (1024.0 * 1024.0));
        ImGui::Text("  Vertex Buffer VRAM:   %.2f MB", static_cast<double>(gpu.GetVertexCapacityBytes()) / (1024.0 * 1024.0));
        ImGui::Text("  Index Buffer VRAM:    %.2f MB", static_cast<double>(gpu.GetIndexCapacityBytes()) / (1024.0 * 1024.0));
        ImGui::Text("  Transfer Buffers:     %.2f MB", static_cast<double>(gpu.GetImageTransferCapacityBytes() * 2) / (1024.0 * 1024.0));
        ImGui::Text("  Canvas Render Target: %.2f MB", static_cast<double>(gpu.GetTextureMemoryBytes()) / (1024.0 * 1024.0));
        ImGui::Separator();

        float maxHist = *std::max_element(frameTimesHistory.begin(), frameTimesHistory.end());
        ImGui::PlotLines("Frame Time (ms)", frameTimesHistory.data(), static_cast<int>(frameTimesHistory.size()), historyOffset, nullptr, 0.0f, std::max(16.0f, maxHist * 1.2f), ImVec2(0, 80));

        ImGui::End();

        // Render viewport background
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        glViewport(0, 0, winW, winH);
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    glDeleteTextures(1, &canvasTexture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "================================================================================\n";
    std::cout << " FolioNote GPU Ink Manager Performance Benchmark & Stress Suite\n";
    std::cout << "================================================================================\n";

    bool interactive = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--interactive") {
            interactive = true;
        }
    }

    auto& gpu = Folio::GpuInkManager::Instance();
    if (!gpu.Init()) {
        std::cerr << "[Error] Failed to initialize SDL_GPU device on this system.\n";
        return 1;
    }

    std::cout << "Active SDL_GPU Driver: " << gpu.GetDriverName() << "\n";

    int result = 0;
    if (interactive) {
        result = RunInteractiveMode(gpu);
    } else {
        result = RunHeadlessBenchmark(gpu);
    }

    gpu.Shutdown();
    std::cout << "Benchmark run complete.\n";
    return result;
}
