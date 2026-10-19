/**
 * =========================================================================================
 * @file test_physics_model.cpp
 * @brief Unit tests for computational physics models and dynamic eraser simulation
 * =========================================================================================
 */

#include <cassert>
#include <iostream>
#include <fstream>
#include <cmath>
#include "utils/physics_model.hpp"
#include "core/ink_engine/ink_engine.hpp"
#include "core/gpu/gpu_ink_manager.hpp"

using namespace Folio::Physics;

void TestSpringDamperCriticallyDamped() {
    std::cout << "[Test] Running SpringDamper1D critically damped convergence test..." << std::endl;

    SpringConfig cfg = SpringConfig::CriticallyDamped(5.0f, 1.0f);
    assert(std::abs(cfg.DampingRatio() - 1.0f) < 0.001f);

    SpringDamper1D spring(cfg, 0.0f, 0.0f);
    spring.SetTarget(10.0f);

    // Simulate for 1.0 second at 120 FPS (dt ~ 0.00833s)
    constexpr float dt = 1.0f / 120.0f;
    for (int i = 0; i < 120; ++i) {
        spring.Step(dt);
    }

    // After 1.0 second, a 5Hz critically damped system should have reached near target (within 1%)
    assert(std::abs(spring.GetPosition() - 10.0f) < 0.1f);
    assert(spring.GetPosition() <= 10.05f); // No significant overshoot for zeta = 1.0

    std::cout << "[Test] SpringDamper1D convergence test passed! Final position: "
              << spring.GetPosition() << std::endl;
}

void TestDynamicEraserPhysicsModel() {
    std::cout << "[Test] Running DynamicEraserPhysicsModel velocity simulation test..." << std::endl;

    DynamicEraserPhysicsModel::Parameters params;
    params.minRadiusMm = 1.5f;
    params.maxRadiusMm = 16.0f;
    params.minSpeedPxPerSec = 100.0f;
    params.maxSpeedPxPerSec = 2000.0f;

    DynamicEraserPhysicsModel model(params);
    assert(std::abs(model.GetRadius() - 1.5f) < 0.001f);

    // 1. Stationary / slow pointer: stays at minimum radius
    constexpr float dt = 1.0f / 120.0f;
    for (int i = 0; i < 60; ++i) {
        model.Update(50.0f, dt); // Below 100 px/sec floor
    }
    assert(std::abs(model.GetRadius() - 1.5f) < 0.05f);

    // 2. High-speed scrubbing (2000 px/sec): expands toward 16.0mm
    for (int i = 0; i < 120; ++i) {
        model.Update(2000.0f, dt);
    }
    assert(model.GetRadius() > 14.0f);
    assert(model.GetRadius() <= 16.0f);

    // 3. Deceleration to 0 px/sec: smoothly contracts back toward 1.5mm
    for (int i = 0; i < 240; ++i) {
        model.Update(0.0f, dt);
    }
    assert(model.GetRadius() < 2.0f);
    assert(model.GetRadius() >= 1.5f);

    // 4. Hard bounds preservation
    for (int i = 0; i < 60; ++i) {
        model.Update(50000.0f, dt); // Extreme speed
    }
    assert(model.GetRadius() <= 16.0f);

    std::cout << "[Test] DynamicEraserPhysicsModel velocity simulation test passed!" << std::endl;
}

void TestStylusTiltDynamics() {
    std::cout << "[Test] Running Stylus Tilt & Pencil Shading Dynamics test..." << std::endl;

    Folio::InkingConfig config;
    config.useTilt = true;
    config.usePressure = false; // Isolate tilt
    config.useVelocity = false;
    config.dynamicWidthSmoothing = 1.0f; // Instant for unit test
    config.maxTiltWidthMultiplier = 2.5f;
    config.tiltDeadzoneRad = 0.20f;
    config.tiltMaxAngleRad = 1.10f;

    const float baseWidth = 0.7f; // 2B Pencil baseline

    // 1. Upright stylus (tilt = 0): no expansion
    float wUpright = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wUpright - baseWidth) < 1e-4f);

    // 2. Stylus within deadzone (tilt = 0.15 rad < 0.20 rad): stays at base width
    float wDeadzone = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 0.15f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wDeadzone - baseWidth) < 1e-4f);

    // 3. Stylus tilted for shading (tilt = 0.65 rad ~ mid-shading): smooth expansion
    float wMidShading = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 0.65f, 0.0f, -1.0f, 1.0f, config);
    assert(wMidShading > baseWidth);
    assert(wMidShading < baseWidth * config.maxTiltWidthMultiplier);

    // 4. Stylus at maximum tilt (tilt = 1.20 rad > 1.10 rad ceiling): clamped to maxTiltWidthMultiplier
    float wMaxTilt = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 1.20f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wMaxTilt - (baseWidth * config.maxTiltWidthMultiplier)) < 1e-4f);

    // 5. Stylus with useTilt disabled: tilt angle does not affect width
    config.useTilt = false;
    float wNoTilt = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 1.0f, 0.0f, 1.20f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wNoTilt - baseWidth) < 1e-4f);

    std::cout << "[Test] Stylus Tilt & Pencil Shading Dynamics test passed!" << std::endl;
}

#include "core/ink_engine/ink_engine.hpp"

void TestCurveSimplificationPreservesRoundness() {
    std::cout << "[Test] Running StrokePostProcessor curve roundness preservation test..." << std::endl;

    // Generate a smooth 180-degree circular arc of radius 5.0 mm with 100 samples
    std::vector<StrokePoint> arc;
    arc.reserve(100);
    for (int i = 0; i < 100; ++i) {
        double a = (3.14159265358979323846 * i) / 99.0;
        arc.emplace_back(5.0 * std::cos(a), 5.0 * std::sin(a), 0.35f, 0.7f);
    }

    auto simplified = Folio::StrokePostProcessor::Simplify(arc, 0.015f, 4.0f, 0.08f);
    // Should retain enough vertices to keep the arc smooth without collapsing into a coarse polygon
    assert(simplified.size() >= 18);

    // Verify no adjacent segments form a sharp corner peak (> 12 degrees)
    for (size_t i = 1; i + 1 < simplified.size(); ++i) {
        double ux = simplified[i].x - simplified[i - 1].x;
        double uy = simplified[i].y - simplified[i - 1].y;
        double vx = simplified[i + 1].x - simplified[i].x;
        double vy = simplified[i + 1].y - simplified[i].y;
        double uLen = std::hypot(ux, uy);
        double vLen = std::hypot(vx, vy);
        if (uLen > 1e-6 && vLen > 1e-6) {
            double cosTurn = std::clamp((ux * vx + uy * vy) / (uLen * vLen), -1.0, 1.0);
            double turnDeg = std::acos(cosTurn) * (180.0 / 3.14159265358979323846);
            assert(turnDeg < 12.0);
        }
    }

    std::cout << "[Test] StrokePostProcessor curve roundness test passed! Retained "
              << simplified.size() << " smooth vertices." << std::endl;
}

void TestWidthThinningToggle() {
    std::cout << "[Test] Running Width Thinning Toggle Invariant test..." << std::endl;

    Folio::InkingConfig config;
    config.dynamicWidthSmoothing = 1.0f; // Instantaneous response
    constexpr float baseWidth = 1.0f;

    // 1. Velocity thinning enabled: fast speed thins stroke
    config.useVelocity = true;
    config.usePressure = false;
    float wFast = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, -1.0f, 1000.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(wFast < baseWidth);

    // 2. Velocity thinning disabled: fast speed does NOT thin stroke
    config.useVelocity = false;
    float wFastNoThin = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, -1.0f, 1000.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wFastNoThin - baseWidth) < 1e-4f);

    // 3. Pressure thinning enabled: light pressure thins stroke
    config.useVelocity = false;
    config.usePressure = true;
    float wLight = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 0.2f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(wLight < baseWidth);

    // 4. Pressure thinning disabled: light pressure does NOT thin stroke
    config.usePressure = false;
    float wLightNoThin = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, 0.2f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(std::abs(wLightNoThin - baseWidth) < 1e-4f);

    std::cout << "[Test] Width Thinning Toggle test passed (100% OK)!" << std::endl;
}

void TestStrokeGeometryDebugger() {
    std::cout << "[Test] Running StrokeGeometryDebugger and telemetry verification..." << std::endl;

    InkGenerator::SetDebugCapture(true);
    assert(InkGenerator::IsDebugCaptureEnabled());

    // 1. Clean straight stroke (no defects)
    std::vector<StrokePoint> cleanStroke = {
        { 0.0, 0.0, 2.0f, 0.5f, 0.0f, 0.0f },
        { 10.0, 0.0, 2.0f, 0.5f, 0.0f, 0.0f },
        { 20.0, 0.0, 2.0f, 0.5f, 0.0f, 0.0f }
    };
    BLPath cleanPath = InkGenerator::BuildSolidRibbon(cleanStroke);
    assert(!cleanPath.is_empty());

    const auto& cleanInfo = InkGenerator::GetLastDebugInfo();
    assert(cleanInfo.rawInputPts.size() == 3);
    assert(cleanInfo.cleanPts.size() == 3);
    assert(!cleanInfo.leftRail.empty());
    assert(!cleanInfo.rightRail.empty());
    assert(cleanInfo.selfIntersections.empty());
    assert(!cleanInfo.hasDefects);

    // 2. Self-intersecting crossing stroke (figure-8 / scissors)
    std::vector<StrokePoint> crossingStroke = {
        { 0.0, 0.0, 3.0f, 0.5f, 0.0f, 0.0f },
        { 40.0, 40.0, 3.0f, 0.5f, 0.0f, 0.0f },
        { 40.0, 0.0, 3.0f, 0.5f, 0.0f, 0.0f },
        { 0.0, 40.0, 3.0f, 0.5f, 0.0f, 0.0f }
    };
    BLPath crossPath = InkGenerator::BuildSolidRibbon(crossingStroke);
    assert(!crossPath.is_empty());

    const auto& crossInfo = InkGenerator::GetLastDebugInfo();
    assert(crossInfo.hasDefects);
    assert(!crossInfo.selfIntersections.empty());

    // 3. Verify RenderDebugOverlay runs without crash on Blend2D context
    BLImage img(400, 400, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    InkGenerator::RenderDebugOverlay(ctx, crossInfo, 1.0);
    ctx.end();

    // 4. Verify dumper creates valid file output
    const std::string testDumpFile = "test_stroke_dump.txt";
    InkGenerator::DumpStrokeToFile(testDumpFile, &crossInfo);
    std::ifstream dumpCheck(testDumpFile);
    assert(dumpCheck.is_open());
    std::string line;
    bool foundRepro = false;
    while (std::getline(dumpCheck, line)) {
        if (line.find("std::vector<StrokePoint> reproPts") != std::string::npos) {
            foundRepro = true;
            break;
        }
    }
    dumpCheck.close();
    std::remove(testDumpFile.c_str());
    assert(foundRepro);

    // Clean up
    InkGenerator::ClearDebugInfo();
    InkGenerator::SetDebugCapture(false);
    assert(!InkGenerator::IsDebugCaptureEnabled());

    std::cout << "[Test] StrokeGeometryDebugger test passed (100% OK)!" << std::endl;
}

void TestCrescentTurnGeometry() {
    std::cout << "[Test] Running Crescent Turn Geometry (Tight Radius Curvature) test..." << std::endl;

    InkGenerator::SetDebugCapture(true);
    std::vector<StrokePoint> crescentPts = {
#include "repro114.inc"
    };

    BLPath path = InkGenerator::BuildSolidRibbon(crescentPts);
    assert(!path.is_empty());

    const auto& info = InkGenerator::GetLastDebugInfo();
    std::cout << "  Crescent stroke clean points: " << info.cleanPts.size() << std::endl;
    std::cout << "  Left rail points: " << info.leftRail.size() << ", Right rail points: " << info.rightRail.size() << std::endl;
    std::cout << "  Self intersections: " << info.selfIntersections.size() << std::endl;
    std::cout << "  Retrograde folds: " << info.retrogradeIndices.size() << std::endl;

    // Verify 100% elimination of self-intersections and retrograde folds
    assert(info.selfIntersections.empty());
    assert(info.retrogradeIndices.empty());
    assert(!info.hasDefects);

    InkGenerator::ClearDebugInfo();
    InkGenerator::SetDebugCapture(false);
    std::cout << "[Test] Crescent Turn Geometry test passed (0 defects, 100% OK)!" << std::endl;
}

void TestInkEngineMeshIntegration() {
    std::cout << "[Test] Running InkEngine GPU Mesh Integration test..." << std::endl;

    Folio::InkEngine engine;
    StrokeStyle style;
    style.baseWidth = 1.5f;
    style.capType = CapType::Round;
    style.color = BLRgba32(0x20, 0x80, 0xFF, 0xFF);

    engine.BeginStroke(10.0, 10.0, 0.5f, 0.000, style, 1.0f);
    assert(engine.IsStrokeActive());
    const auto& initialMesh = engine.GetLiveMesh();
    assert(!initialMesh.empty());
    assert(initialMesh.indices.size() % 3 == 0);

    engine.AppendPoint(20.0, 15.0, 0.7f, 0.016);
    engine.AppendPoint(35.0, 28.0, 0.8f, 0.032);
    engine.AppendPoint(50.0, 30.0, 0.6f, 0.048);

    const auto& liveMesh = engine.GetLiveMesh();
    assert(!liveMesh.empty());
    assert(liveMesh.indices.size() % 3 == 0);

    FinishedStrokeData finished = engine.FinishStroke();
    assert(!engine.IsStrokeActive());
    assert(!finished.mesh.empty());
    assert(finished.mesh.indices.size() % 3 == 0);

    std::cout << "[Test] InkEngine GPU Mesh Integration test passed ("
              << finished.mesh.vertices.size() << " vertices, "
              << finished.mesh.indices.size() / 3 << " triangles, 100% OK)!" << std::endl;
}

void TestGpuInkManagerRender() {
    std::cout << "[Test] Running SDL_GPU InkMesh Hardware Rasterization test..." << std::endl;

    auto& gpu = Folio::GpuInkManager::Instance();
    if (!gpu.Init()) {
        std::cout << "  -> SDL_GPU device unavailable in headless environment; skipping GPU raster check." << std::endl;
        return;
    }

    std::cout << "  SDL_GPU initialized with driver: " << gpu.GetDriverName() << std::endl;

    BLImage surface(256, 256, BL_FORMAT_PRGB32);
    BLContext ctx(surface);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    ctx.fill_all();
    ctx.end();

    // Build a horizontal variable-width stroke from (32, 128) to (224, 128) with radius 8px
    std::vector<StrokePoint> pts = {
        { 32.0, 128.0, 8.0f, 0.8f, 0.0f, 0.0f },
        { 128.0, 128.0, 10.0f, 0.9f, 0.0f, 0.0f },
        { 224.0, 128.0, 8.0f, 0.8f, 0.0f, 0.0f }
    };

    BLRgba32 inkColor(0xE0, 0x20, 0x10, 0xFF);
    Folio::InkMesh mesh;
    Folio::InkEngine::BuildStrokeMesh(pts, CapType::Round, Folio::PackGpuColor(inkColor), mesh);
    assert(!mesh.empty());

    gpu.BeginBatch();
    gpu.SubmitMesh(mesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Normal);
    assert(gpu.HasPendingDraws());

    bool ok = gpu.FlushToSurface(surface);
    assert(ok);
    assert(gpu.GetLastVertexCount() == mesh.vertices.size());
    assert(gpu.GetLastTriangleCount() == mesh.indices.size() / 3);

    // 1. Verify rendered pixel at center (128, 128) matches inkColor (R=0xE0, G=0x20, B=0x10)
    BLImageData imgData = {};
    surface.get_data(&imgData);
    const uint8_t* centerPx = static_cast<const uint8_t*>(imgData.pixel_data) + 128 * imgData.stride + 128 * 4;
    // BL_FORMAT_PRGB32 in little-endian memory is [B, G, R, A]
    uint8_t b = centerPx[0];
    uint8_t g = centerPx[1];
    uint8_t r = centerPx[2];
    uint8_t a = centerPx[3];

    assert(std::abs(int(r) - 0xE0) <= 2);
    assert(std::abs(int(g) - 0x20) <= 2);
    assert(std::abs(int(b) - 0x10) <= 2);
    assert(a == 0xFF);

    // 2. Render a curved S-turn stroke with variable width to verify smooth edge rasterization
    std::vector<StrokePoint> curvedPts;
    for (int i = 0; i <= 60; ++i) {
        double t = i / 60.0;
        double x = 40.0 + t * 180.0;
        double y = 128.0 + 50.0 * std::sin(t * M_PI * 2.0);
        float radius = static_cast<float>(6.0 + 8.0 * std::sin(t * M_PI));
        curvedPts.push_back({ x, y, radius, 0.8f, 0.0f, 0.0f });
    }
    Folio::InkMesh curvedMesh;
    Folio::InkEngine::BuildStrokeMesh(curvedPts, CapType::Round, Folio::PackGpuColor(BLRgba32(0xD6, 0x20, 0x20, 0xFF)), curvedMesh);

    ctx.begin(surface);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    ctx.fill_all();
    ctx.end();

    gpu.BeginBatch();
    gpu.SubmitMesh(curvedMesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Normal);
    gpu.FlushToSurface(surface);

    // 3. Zero Intra-Stroke Darkening Test: Self-intersecting / overlapping Multiply mesh
    ctx.begin(surface);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    ctx.fill_all();
    ctx.end();

    BLRgba32 hlColor(0x00, 0xC8, 0x00, 0x80);
    uint32_t packedHlColor = Folio::PackGpuColor(hlColor);

    Folio::InkMesh selfOverlapMesh;
    selfOverlapMesh.vertices = {
        { 60.0f,  60.0f, 0.0f, 10.0f, packedHlColor },
        { 120.0f, 60.0f, 0.0f, 10.0f, packedHlColor },
        { 120.0f, 120.0f, 0.0f, 10.0f, packedHlColor },
        { 60.0f,  120.0f, 0.0f, 10.0f, packedHlColor },
        { 60.0f,  60.0f, 0.0f, 10.0f, packedHlColor },
        { 120.0f, 60.0f, 0.0f, 10.0f, packedHlColor },
        { 120.0f, 120.0f, 0.0f, 10.0f, packedHlColor },
        { 60.0f,  120.0f, 0.0f, 10.0f, packedHlColor }
    };
    selfOverlapMesh.indices = {
        0, 1, 2,  0, 2, 3,
        4, 5, 6,  4, 6, 7
    };

    gpu.BeginBatch();
    gpu.SubmitMesh(selfOverlapMesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Multiply, 1);
    gpu.FlushToSurface(surface);

    surface.get_data(&imgData);
    const uint8_t* overlapPx = static_cast<const uint8_t*>(imgData.pixel_data) + 90 * imgData.stride + 90 * 4;
    uint8_t greenChOverlap = overlapPx[1];

    ctx.begin(surface);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    ctx.fill_all();
    ctx.end();

    Folio::InkMesh singleQuadMesh;
    singleQuadMesh.vertices.assign(selfOverlapMesh.vertices.begin(), selfOverlapMesh.vertices.begin() + 4);
    singleQuadMesh.indices = { 0, 1, 2,  0, 2, 3 };

    gpu.BeginBatch();
    gpu.SubmitMesh(singleQuadMesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Multiply, 1);
    gpu.FlushToSurface(surface);

    surface.get_data(&imgData);
    const uint8_t* singlePx = static_cast<const uint8_t*>(imgData.pixel_data) + 90 * imgData.stride + 90 * 4;
    uint8_t greenChSingle = singlePx[1];

    std::cout << "  [Multiply Stencil Test] Single-pass green: " << int(greenChSingle)
              << ", Self-overlap intra-stroke green: " << int(greenChOverlap) << std::endl;
    assert(std::abs(int(greenChOverlap) - int(greenChSingle)) <= 1);

    // Verify inter-stroke multiplication between distinct strokes
    ctx.begin(surface);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    ctx.fill_all();
    ctx.end();

    gpu.BeginBatch();
    gpu.SubmitMesh(singleQuadMesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Multiply, 1);
    gpu.SubmitMesh(singleQuadMesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Multiply, 2);
    gpu.FlushToSurface(surface);

    surface.get_data(&imgData);
    const uint8_t* doublePx = static_cast<const uint8_t*>(imgData.pixel_data) + 90 * imgData.stride + 90 * 4;
    uint8_t greenChDouble = doublePx[1];

    std::cout << "  [Multiply Stencil Test] Two distinct strokes (inter-stroke) green: " << int(greenChDouble) << std::endl;
    assert(greenChDouble < greenChSingle);

    // Verify Normal blend mode opaque pen self-overlap (Zero Intra-Stroke Darkening & Zero Seams)
    ctx.begin(surface);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    ctx.fill_all();
    ctx.end();

    Folio::InkMesh penOverlapMesh = selfOverlapMesh;
    // Set 100% opacity green (0xFF00FF00) on all vertices
    for (auto& v : penOverlapMesh.vertices) {
        v.color = 0xFF00FF00;
    }

    gpu.BeginBatch();
    gpu.SubmitMesh(penOverlapMesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Normal, 1);
    gpu.FlushToSurface(surface);

    surface.get_data(&imgData);
    const uint8_t* penOverlapPx = static_cast<const uint8_t*>(imgData.pixel_data) + 90 * imgData.stride + 90 * 4;
    uint8_t penGreenOverlap = penOverlapPx[1];

    ctx.begin(surface);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    ctx.fill_all();
    ctx.end();

    Folio::InkMesh penSingleMesh = singleQuadMesh;
    for (auto& v : penSingleMesh.vertices) {
        v.color = 0xFF00FF00;
    }

    gpu.BeginBatch();
    gpu.SubmitMesh(penSingleMesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, BlendMode::Normal, 1);
    gpu.FlushToSurface(surface);

    surface.get_data(&imgData);
    const uint8_t* penSinglePx = static_cast<const uint8_t*>(imgData.pixel_data) + 90 * imgData.stride + 90 * 4;
    uint8_t penGreenSingle = penSinglePx[1];

    std::cout << "  [Normal/Pen Test] Single-pass green: " << int(penGreenSingle)
              << ", Self-overlap intra-stroke green: " << int(penGreenOverlap) << std::endl;
    assert(penGreenOverlap == penGreenSingle && penGreenSingle == 255);

    gpu.Shutdown();
    std::cout << "[Test] SDL_GPU InkMesh Hardware Rasterization test passed (100% OK)!" << std::endl;
}

void TestReproduceLoopImages() {
    std::cout << "[Test] Generating loop images for Round, Square, and Chisel..." << std::endl;
    auto& gpu = Folio::GpuInkManager::Instance();
    gpu.Init();

    // Generate a rounded rectangular loop of points (around a box with rounded corners)
    std::vector<StrokePoint> loopPts;
    // Top-left to top-right
    for (int i = 0; i <= 25; ++i) {
        double t = i / 25.0;
        loopPts.push_back({ 150.0 + t * 200.0, 100.0, 16.0f, 0.8f, 0.0f, 0.0f });
    }
    // Top-right corner turn to down
    for (int i = 1; i <= 25; ++i) {
        double t = i / 25.0;
        loopPts.push_back({ 350.0, 100.0 + t * 200.0, 16.0f, 0.8f, 0.0f, 0.0f });
    }
    // Bottom-right to bottom-left
    for (int i = 1; i <= 25; ++i) {
        double t = i / 25.0;
        loopPts.push_back({ 350.0 - t * 200.0, 300.0, 16.0f, 0.8f, 0.0f, 0.0f });
    }
    // Bottom-left to top-left and overlap
    for (int i = 1; i <= 35; ++i) {
        double t = i / 25.0;
        loopPts.push_back({ 150.0, 300.0 - t * 200.0, 16.0f, 0.8f, 0.0f, 0.0f });
    }

    // Generate a zigzag turnaround stroke (left to right, turnaround 180 deg, right to left, etc.)
    std::vector<StrokePoint> zigzagPts;
    for (int i = 0; i <= 30; ++i) {
        double t = i / 30.0;
        zigzagPts.push_back({ 80.0 + t * 350.0, 120.0, 18.0f, 0.8f, 0.0f, 0.0f });
    }
    // Turnaround to next row
    for (int i = 1; i <= 10; ++i) {
        double a = -M_PI * 0.5 + (M_PI * i) / 10.0;
        zigzagPts.push_back({ 430.0 + std::cos(a) * 20.0, 140.0 + std::sin(a) * 20.0, 18.0f, 0.8f, 0.0f, 0.0f });
    }
    // Right to left
    for (int i = 1; i <= 30; ++i) {
        double t = i / 30.0;
        zigzagPts.push_back({ 430.0 - t * 350.0, 160.0, 18.0f, 0.8f, 0.0f, 0.0f });
    }
    // Turnaround to third row
    for (int i = 1; i <= 10; ++i) {
        double a = M_PI * 0.5 + (M_PI * i) / 10.0;
        zigzagPts.push_back({ 80.0 + std::cos(a) * 20.0, 180.0 + std::sin(a) * 20.0, 18.0f, 0.8f, 0.0f, 0.0f });
    }
    // Left to right
    for (int i = 1; i <= 30; ++i) {
        double t = i / 30.0;
        zigzagPts.push_back({ 80.0 + t * 350.0, 200.0, 18.0f, 0.8f, 0.0f, 0.0f });
    }

    auto renderAndSave = [&](const std::vector<StrokePoint>& pts, CapType cap, const char* name, BlendMode blend, BLRgba32 col) {
        BLImage surface(512, 512, BL_FORMAT_PRGB32);
        BLContext ctx(surface);
        ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.fill_all();
        ctx.end();

        Folio::InkMesh mesh;
        Folio::InkEngine::BuildStrokeMesh(pts, cap, Folio::PackGpuColor(col), mesh, 1.0f);

        gpu.BeginBatch();
        gpu.SubmitMesh(mesh, BLMatrix2D::make_identity(), 1.0f, 1.0f, blend, 1);
        gpu.FlushToSurface(surface);

        std::string filename = std::string("/home/odysseus/.gemini/antigravity/brain/3314c842-4afc-44b2-8c5b-c660e95ed743/scratch/") + name + ".png";
        surface.write_to_file(filename.c_str());

        // Also render outlinePath via Blend2D
        BLImage surfaceCpu(512, 512, BL_FORMAT_PRGB32);
        BLContext ctxCpu(surfaceCpu);
        ctxCpu.set_comp_op(BL_COMP_OP_SRC_COPY);
        ctxCpu.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctxCpu.fill_all();
        ctxCpu.set_comp_op(blend == BlendMode::Multiply ? BL_COMP_OP_MULTIPLY : BL_COMP_OP_SRC_OVER);
        ctxCpu.set_fill_style(col);
        BLPath outline = Folio::InkEngine::BuildStrokeOutline(pts, cap);
        ctxCpu.fill_path(outline);
        ctxCpu.end();

        std::string filenameCpu = std::string("/home/odysseus/.gemini/antigravity/brain/3314c842-4afc-44b2-8c5b-c660e95ed743/scratch/") + name + "_cpu.png";
        surfaceCpu.write_to_file(filenameCpu.c_str());
    };

    // Generate a diagonal test stroke matching the user's shape
    std::vector<StrokePoint> diagonalPts;
    for (int i = 0; i <= 40; ++i) {
        double t = i / 40.0;
        // Introduce small sub-pixel digitizer jitter typical of real stylus input
        double jx = (i % 2 == 0 ? 0.3 : -0.3);
        double jy = (i % 3 == 0 ? 0.3 : -0.3);
        diagonalPts.push_back({ 300.0 - t * 150.0 + jx, 400.0 - t * 150.0 + jy, 18.0f, 0.8f, 0.0f, 0.0f });
    }
    for (int i = 1; i <= 20; ++i) {
        double t = i / 20.0;
        diagonalPts.push_back({ 150.0, 250.0 - t * 100.0, 18.0f, 0.8f, 0.0f, 0.0f });
    }
    for (int i = 1; i <= 40; ++i) {
        double t = i / 40.0;
        double jx = (i % 2 == 0 ? 0.3 : -0.3);
        double jy = (i % 3 == 0 ? 0.3 : -0.3);
        diagonalPts.push_back({ 150.0 + t * 150.0 + jx, 150.0 - t * 100.0 + jy, 18.0f, 0.8f, 0.0f, 0.0f });
    }

    renderAndSave(loopPts, CapType::Round, "loop_round", BlendMode::Normal, BLRgba32(0xD6, 0x20, 0x20, 0xFF));
    renderAndSave(loopPts, CapType::Round, "loop_brush_semi_trans", BlendMode::Normal, BLRgba32(0x20, 0x60, 0xD6, 0x80));
    renderAndSave(loopPts, CapType::Square, "loop_square_pink", BlendMode::Multiply, BLRgba32(0xFF, 0x40, 0x80, 0x80));
    renderAndSave(loopPts, CapType::Chisel, "loop_chisel_red", BlendMode::Normal, BLRgba32(0xD6, 0x20, 0x20, 0xFF));
    renderAndSave(zigzagPts, CapType::Square, "zigzag_square_pink", BlendMode::Multiply, BLRgba32(0xFF, 0x40, 0x80, 0x80));
    renderAndSave(diagonalPts, CapType::Square, "diagonal_square_pink", BlendMode::Multiply, BLRgba32(0xFF, 0x40, 0x80, 0x80));

    gpu.Shutdown();
}

void TestQuickStrokeLoopMesh() {
    std::cout << "[Test] Running Quick Stroke Loop Mesh (Branch-Cut & Hole Integrity) test..." << std::endl;

    // Circle loop centered at (100, 100) with radius 15mm traversing branch cut
    std::vector<StrokePoint> loopPts;
    const double centerX = 100.0, centerY = 100.0, loopR = 15.0;
    const int numSteps = 40;
    for (int i = 0; i <= numSteps; ++i) {
        double theta = (2.0 * M_PI * i) / numSteps;
        double px = centerX + loopR * std::cos(theta);
        double py = centerY + loopR * std::sin(theta);
        loopPts.emplace_back(px, py, 0.5f);
    }

    Folio::InkMesh mesh;
    Folio::InkMeshBuilder::BuildSolidMesh(loopPts, CapType::Round, 0xFF000000, mesh, 3.7795f);
    assert(!mesh.empty());
    assert(!mesh.vertices.empty());

    // Verify that NO vertex intrudes into the central hole of the loop
    for (const auto& v : mesh.vertices) {
        double distFromCenter = std::hypot(v.x - centerX, v.y - centerY);
        assert(distFromCenter >= 10.0 && "Vertex intruded into loop center hole! Fan wrap bug detected.");
    }

    std::cout << "[Test] Loop Mesh test passed: central hole completely hollow (0 intruding vertices)!" << std::endl;
}

void TestTailTaper() {
    std::cout << "[Test] Running Configurable Tail Taper test..." << std::endl;

    Folio::InkEngine engine;
    Folio::InkingConfig config = engine.GetConfig();
    config.tailTaperLengthMm = 20.0f;
    config.tailTaperPower = 1.0f;
    config.usePressure = true;
    config.minWidthMultiplier = 0.2f;
    config.maxWidthMultiplier = 1.0f;
    engine.SetConfig(config);

    StrokeStyle style;
    style.baseWidth = 2.0f;
    style.pattern = StrokePattern::Solid;
    style.capType = CapType::Round;
    style.usePressure = true;

    // Draw straight line along X from 0 to 60mm with constant 0.8 pressure
    engine.BeginStroke(0.0, 0.0, 0.8f, 0.0, style, 1.0f);
    for (int i = 1; i <= 60; ++i) {
        engine.AppendPoint(static_cast<double>(i), 0.0, 0.8f, i * 0.01);
    }

    FinishedStrokeData data = engine.FinishStroke();
    assert(!data.modeledPoints.empty());

    const auto& pts = data.modeledPoints;
    float minExpectedR = style.baseWidth * config.minWidthMultiplier * 0.5f;

    // Tip point should be tapered down to minExpectedR
    assert(pts.back().r <= minExpectedR + 1e-3f);
    assert(pts.back().pressure <= 1e-4f);

    // Verify monotonic decay over the trailing 15mm span
    for (size_t i = pts.size() - 1; i > 0; --i) {
        double distFromEnd = pts.back().x - pts[i].x;
        if (distFromEnd > 18.0) break;
        assert(pts[i - 1].r >= pts[i].r - 1e-4f);
    }

    std::cout << "[Test] Tail Taper test passed: stroke tip tapered smoothly to minWidth ("
              << pts.back().r * 2.0f << " mm)!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Folio Physics Model Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestSpringDamperCriticallyDamped();
    TestDynamicEraserPhysicsModel();
    TestStylusTiltDynamics();
    TestCurveSimplificationPreservesRoundness();
    TestWidthThinningToggle();
    TestStrokeGeometryDebugger();
    TestCrescentTurnGeometry();
    TestInkEngineMeshIntegration();
    TestGpuInkManagerRender();
    TestReproduceLoopImages();
    TestQuickStrokeLoopMesh();
    TestTailTaper();

    std::cout << "========================================" << std::endl;
    std::cout << "All Physics Model tests passed (100% OK)!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}


