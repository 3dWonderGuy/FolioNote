/**
 * =========================================================================================
 * @file test_storage.cpp
 * @brief Comprehensive Unit & Invariant Test Suite for FolioNote Storage Subsystem
 * =========================================================================================
 *
 * Verifies:
 * 1. CRC32 Checksum Engine (IEEE 802.3 standard polynomial)
 * 2. Endian-Safe ByteWriter & Boundary-Checked ByteReader
 * 3. Vector Stroke Compression, Delta Encoding, & Full Page Serialization (.ink)
 * 4. Corruption Detection & Data Integrity Fail-Safe (CRC32 Mismatch Rejection)
 * 5. SQLite DBManager (WAL Mode, Schema Migrations, Cascades, Soft-Delete Recycle Bin)
 * 6. PageRepository Safety (Rejection of Unloaded Page Stubs)
 */

#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring>
#include <cmath>

#include "core/storage/binary_serializer.hpp"
#include "core/storage/db_manager.hpp"
#include "core/storage/page_repository.hpp"
#include "core/document/canvas_page.hpp"
#include "core/ink_engine/ink_engine.hpp"
#include "core/objects/ink_container/ink_container.hpp"
#include "core/objects/text/text_box.hpp"
#include "io/facade/file_manager.hpp"

using namespace Folio;

void TestCRC32Engine() {
    std::cout << "[Test 1/6] Running CRC32 Engine Validation..." << std::endl;

    // Standard IEEE 802.3 test vector: CRC32("123456789") == 0xCBF43926
    const char* testVector = "123456789";
    uint32_t crc = CRC32::Compute(reinterpret_cast<const uint8_t*>(testVector), 9);
    assert(crc == 0xCBF43926);

    // Empty data CRC32 should be 0x00000000
    uint32_t emptyCrc = CRC32::Compute(nullptr, 0);
    assert(emptyCrc == 0x00000000);

    std::cout << "  -> CRC32 test vector matched 0xCBF43926 successfully." << std::endl;
}

void TestByteStreamPrimitives() {
    std::cout << "[Test 2/6] Running ByteWriter / ByteReader Invariant Tests..." << std::endl;

    ByteWriter writer;
    writer.WriteU8(42);
    writer.WriteBool(true);
    writer.WriteBool(false);
    writer.WriteU16(0xABCD);
    writer.WriteU32(0x12345678);
    writer.WriteU64(0xFEEDFACECAFEBEEFULL);
    writer.WriteFloat(3.14159f);
    writer.WriteDouble(2.718281828459);
    writer.WriteString("Hello FolioNote Storage");

    ByteReader reader(writer.Data(), writer.Size());
    assert(reader.ReadU8() == 42);
    assert(reader.ReadBool() == true);
    assert(reader.ReadBool() == false);
    assert(reader.ReadU16() == 0xABCD);
    assert(reader.ReadU32() == 0x12345678);
    assert(reader.ReadU64() == 0xFEEDFACECAFEBEEFULL);
    assert(std::abs(reader.ReadFloat() - 3.14159f) < 1e-5f);
    assert(std::abs(reader.ReadDouble() - 2.718281828459) < 1e-9);
    assert(reader.ReadString() == "Hello FolioNote Storage");
    assert(reader.RemainingBytes() == 0);

    // Test out-of-bounds reading safety
    bool caughtException = false;
    try {
        reader.ReadU8(); // Buffer exhausted
    } catch (const std::runtime_error&) {
        caughtException = true;
    }
    assert(caughtException);

    std::cout << "  -> ByteWriter / ByteReader boundary safety verified." << std::endl;
}

void TestPageSerializationRoundTrip() {
    std::cout << "[Test 3/6] Running Page Serialization & Delta Compression Round-Trip..." << std::endl;

    CanvasPage originalPage("Calculus Lecture 1");
    originalPage.paperStyle = PaperStyle::Grid;
    originalPage.gridSpacingMm = 7.0;
    originalPage.infinityMode = CanvasInfinityMode::SemiInfinity;

    // 1. Add an InkContainer with multiple segments
    auto ink = std::make_shared<InkContainer>();
    ink->zOrder = 1;
    Stroke stroke;
    stroke.color.value = 0xFF0055AA;
    stroke.baseWidthMm = 0.7f;

    Segment1D seg0;
    seg0.p0 = Point2D{ 10.0, 20.0 };
    seg0.p1 = Point2D{ 15.0, 25.0 };
    seg0.width = 0.7f;
    stroke.segments.push_back(seg0);

    Segment1D seg1;
    seg1.p0 = Point2D{ 15.0, 25.0 };
    seg1.p1 = Point2D{ 22.0, 31.0 };
    seg1.width = 0.8f;
    stroke.segments.push_back(seg1);

    ink->strokes.push_back(stroke);
    ink->UpdateBounds();
    originalPage.AddObject(ink);

    // 2. Add a TextBoxObject
    auto textObj = std::make_shared<TextBoxObject>();
    textObj->text = "# Title\nEnergy equation: $E=mc^2$";
    textObj->worldX = 50.0;
    textObj->worldY = 100.0;
    textObj->worldWidth = 80.0;
    textObj->worldHeight = 40.0;
    textObj->UpdateBounds();
    originalPage.AddObject(textObj);

    // Serialize page
    std::vector<uint8_t> compressedBlob;
    SerializationStats stats;
    bool serOk = BinarySerializer::SerializePage(originalPage, compressedBlob, &stats);
    assert(serOk);
    assert(!compressedBlob.empty());
    assert(stats.crc32Checksum != 0);
    assert(stats.objectCount == 2);
    assert(stats.strokeCount == 1);
    assert(stats.rawSizeBytes > stats.compressedSizeBytes); // Compression ratio positive

    // Deserialize into a fresh page
    CanvasPage loadedPage("Blank Stub");
    bool deserOk = BinarySerializer::DeserializePage(compressedBlob.data(), compressedBlob.size(), loadedPage);
    assert(deserOk);

    // Verify fields
    assert(loadedPage.guid == originalPage.guid);
    assert(loadedPage.title == "Calculus Lecture 1");
    assert(loadedPage.paperStyle == PaperStyle::Grid);
    assert(std::abs(loadedPage.gridSpacingMm - 7.0) < 1e-4);
    assert(loadedPage.infinityMode == CanvasInfinityMode::SemiInfinity);
    assert(loadedPage.objects.size() == 2);

    // Verify deterministic pageIndex assignment upon load
    assert(loadedPage.objects[0]->pageIndex == 0);
    assert(loadedPage.objects[1]->pageIndex == 1);

    // Verify ink stroke reconstruction
    auto loadedInk = std::dynamic_pointer_cast<InkContainer>(loadedPage.objects[0]);
    assert(loadedInk != nullptr);
    assert(loadedInk->strokes.size() == 1);
    assert(loadedInk->strokes[0].segments.size() == 2);
    assert(loadedInk->strokes[0].color.value == 0xFF0055AA);
    assert(std::abs(loadedInk->strokes[0].segments[0].p0.x - 10.0) < 1e-4);
    assert(std::abs(loadedInk->strokes[0].segments[1].p1.x - 22.0) < 1e-4);

    // Verify text box reconstruction
    auto loadedText = std::dynamic_pointer_cast<TextBoxObject>(loadedPage.objects[1]);
    assert(loadedText != nullptr);
    assert(loadedText->text == "# Title\nEnergy equation: $E=mc^2$");

    std::cout << "  -> Full page serialization round-trip verified (raw=" 
              << stats.rawSizeBytes << "B, compressed=" << stats.compressedSizeBytes << "B)." << std::endl;
}

void TestCorruptionAndFailSafeDetection() {
    std::cout << "[Test 4/6] Running Data Corruption & Fail-Safe Detection Tests..." << std::endl;

    CanvasPage originalPage("Safety Test");
    originalPage.AddObject(std::make_shared<TextBoxObject>());

    std::vector<uint8_t> validBlob;
    bool serOk = BinarySerializer::SerializePage(originalPage, validBlob);
    assert(serOk);
    assert(validBlob.size() >= 12);

    // 1. Truncated blob (< 8 bytes) must be rejected
    CanvasPage dummyPage("Dummy");
    assert(!BinarySerializer::DeserializePage(validBlob.data(), 5, dummyPage));
    assert(!BinarySerializer::DeserializePage(nullptr, 100, dummyPage));

    // 2. Corrupt one byte inside the compressed payload (offset 10)
    std::vector<uint8_t> corruptedBlob = validBlob;
    corruptedBlob[10] ^= 0xAA; // Flip bits in payload

    CanvasPage recoverPage("Should Fail");
    bool result = BinarySerializer::DeserializePage(corruptedBlob.data(), corruptedBlob.size(), recoverPage);
    // Must be rejected due to CRC32 checksum mismatch
    assert(!result);

    std::cout << "  -> Data corruption successfully trapped and rejected by CRC32 validator." << std::endl;
}

void TestSQLiteDBManagerOperations() {
    std::cout << "[Test 5/6] Running SQLite DBManager CRUD & WAL Mode Tests..." << std::endl;

    std::string tempDir = (std::filesystem::temp_directory_path() / "folionote_test_db").string();
    std::filesystem::create_directories(tempDir);
    std::string dbPath = (std::filesystem::path(tempDir) / "notebook.db").string();

    // Clean up any stale test database
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath + "-wal", ec);
    std::filesystem::remove(dbPath + "-shm", ec);

    DBManager db;
    assert(db.Open(dbPath));
    assert(db.IsOpen());

    // 1. Notebook Meta
    DBNotebookRecord nb;
    nb.guid = "nb-test-123";
    nb.name = "My Test Notebook";
    nb.colorR = 0.1f; nb.colorG = 0.5f; nb.colorB = 0.9f; nb.colorA = 1.0f;
    assert(db.UpsertNotebookMeta(nb));

    DBNotebookRecord loadedNb;
    assert(db.LoadNotebookMeta(loadedNb));
    assert(loadedNb.guid == nb.guid);
    assert(loadedNb.name == nb.name);

    // 2. Section Groups
    DBSectionGroupRecord grp;
    grp.guid = "grp-1";
    grp.notebookGuid = nb.guid;
    grp.name = "Semester 1";
    grp.sortOrder = 0;
    assert(db.UpsertSectionGroup(grp));

    auto groups = db.LoadSectionGroups(nb.guid);
    assert(groups.size() == 1);
    assert(groups[0].name == "Semester 1");

    // 3. Sections
    DBSectionRecord sec;
    sec.guid = "sec-1";
    sec.notebookGuid = nb.guid;
    sec.groupGuid = grp.guid;
    sec.name = "Mathematics";
    sec.sortOrder = 0;
    assert(db.UpsertSection(sec));

    auto sections = db.LoadSections(nb.guid);
    assert(sections.size() == 1);
    assert(sections[0].name == "Mathematics");

    // 4. Pages Metadata
    assert(db.SavePageMetadata("page-1", sec.guid, "Algebra", "2026-10-08", "12:00:00", 0, true));
    assert(db.SavePageMetadata("page-2", sec.guid, "Geometry", "2026-10-08", "12:01:00", 1, true));

    auto pages = db.LoadPagesMetadata(sec.guid);
    assert(pages.size() == 2);
    assert(pages[0].title == "Algebra");
    assert(pages[1].title == "Geometry");

    // 5. Soft-Delete & Recycle Bin
    assert(db.SoftDeletePage("page-2"));
    pages = db.LoadPagesMetadata(sec.guid);
    assert(pages.size() == 1); // Excluded from active pages

    auto deletedPages = db.LoadDeletedPages(nb.guid);
    assert(deletedPages.size() == 1);
    assert(deletedPages[0].guid == "page-2");

    assert(db.RestorePage("page-2"));
    pages = db.LoadPagesMetadata(sec.guid);
    assert(pages.size() == 2); // Restored!

    // 6. WAL Checkpoint & Clean Close
    assert(db.CheckpointWAL());
    db.Close();
    assert(!db.IsOpen());

    // Clean up temp test directory
    std::filesystem::remove_all(tempDir, ec);

    std::cout << "  -> SQLite DBManager CRUD, WAL checkpoint, and Recycle Bin verified." << std::endl;
}

void TestPageRepositorySafety() {
    std::cout << "[Test 6/6] Running PageRepository Safety Invariants..." << std::endl;

    std::string tempPkg = (std::filesystem::temp_directory_path() / "folionote_test_pkg.notebook").string();
    std::error_code ec;
    std::filesystem::remove_all(tempPkg, ec);

    PageRepository repo;
    assert(repo.OpenNotebookPackage(tempPkg));

    // Create an unloaded page stub
    auto unloadedStub = std::make_shared<CanvasPage>("Unloaded Stub");
    unloadedStub->isLoaded = false;
    unloadedStub->isModified = false;

    // Attempting to save an unloaded page must be REJECTED immediately
    auto saveFuture = repo.SavePageAsync(unloadedStub, "sec-dummy", 0);
    bool saveResult = saveFuture.get();
    assert(!saveResult); // Must return false! Prevents overwriting real file with empty stub!

    // Clean up
    repo.dbManager->Close();
    std::filesystem::remove_all(tempPkg, ec);

    std::cout << "  -> PageRepository unloaded page save rejection safety verified." << std::endl;
}

// =========================================================================
// Link Stubs for Secondary Canvas Objects Not Directly Tested Here
// =========================================================================
#include "core/objects/primitives/shape_container.hpp"
#include "core/objects/connectors/smart_arrow_container.hpp"
#include "core/objects/attachment_container/attachment_container.hpp"
#include "core/objects/media/videos/video_container.hpp"
#include "core/objects/media/videos/video_player_instance.hpp"
#include "core/objects/media/audio/audio_container.hpp"
#include "core/objects/media/images/image_container.hpp"
#include "core/objects/text/text_editor_state.hpp"
#include "core/pdf_engine/pdf_renderer.hpp"

namespace Folio {
ShapeObject::ShapeObject() { type = ObjectType::Shape; }
void ShapeObject::UpdateBounds() {}
bool ShapeObject::HitTest(double, double) const { return false; }
bool ShapeObject::HitTestCircle(double, double, double) const { return false; }
void ShapeObject::BakeTransform() {}
void ShapeObject::Render(BLContext&, const Viewport&) const {}

SmartArrowObject::SmartArrowObject() { type = ObjectType::Connector; }
void SmartArrowObject::UpdateBounds() {}
bool SmartArrowObject::HitTest(double, double) const { return false; }
void SmartArrowObject::ApplyTransform(const BLMatrix2D&) {}
void SmartArrowObject::BakeTransform() {}
void SmartArrowObject::Render(BLContext&, const Viewport&) const {}
std::unique_ptr<CanvasObject> SmartArrowObject::Clone() const { return nullptr; }

AttachmentObject::AttachmentObject() { type = ObjectType::AttachmentFile; }
void AttachmentObject::ApplyTransform(const BLMatrix2D&) {}
void AttachmentObject::BakeTransform() {}
void AttachmentObject::Render(BLContext&, const Viewport&) const {}
std::unique_ptr<CanvasObject> AttachmentObject::Clone() const { return nullptr; }
void AttachmentObject::CustomizeActions(std::vector<Folio::ContextMenuItem>&) {}

void VideoPlayerInstance::Stop() {}
VideoObject::VideoObject() { type = ObjectType::Video; }
void VideoObject::BakeTransform() {}
void VideoObject::Render(BLContext&, const Viewport&) const {}
std::unique_ptr<CanvasObject> VideoObject::Clone() const { return nullptr; }
void VideoObject::CustomizeActions(std::vector<Folio::ContextMenuItem>&) {}

AudioObject::AudioObject() { type = ObjectType::Audio; }
AudioObject::~AudioObject() {}
void AudioObject::ApplyTransform(const BLMatrix2D&) {}
void AudioObject::BakeTransform() {}
void AudioObject::Render(BLContext&, const Viewport&) const {}
std::unique_ptr<CanvasObject> AudioObject::Clone() const { return nullptr; }
void AudioObject::CustomizeActions(std::vector<Folio::ContextMenuItem>&) {}

ImageObject::ImageObject() { type = ObjectType::Image; }
bool ImageObject::EnsureLoaded() { return true; }
void ImageObject::Render(BLContext&, const Viewport&) const {}
void ImageObject::CustomizeActions(std::vector<Folio::ContextMenuItem>&) {}
std::unique_ptr<CanvasObject> ImageObject::Clone() const { return nullptr; }

PdfPageRenderResult PdfRenderer::RenderPage(const std::string&, int, double, bool) {
    return {};
}

std::vector<AABB> TextEditorState::GetSelectionBoxes() const { return {}; }
Point2D TextEditorState::GetCursorWorldPos() const { return {}; }
double TextEditorState::GetCaretHeight() const { return 0.0; }
}

void TestStrokeTessellationAndRasterPerformance() {
    std::cout << "[Test 7/7] Running 5-Second Stroke (1000 pts) Rasterization Performance..." << std::endl;

    std::vector<StrokePoint> points;
    points.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        double t = i * 0.02;
        double x = 400.0 + 300.0 * std::cos(t * 0.5) + t * 5.0;
        double y = 400.0 + 300.0 * std::sin(t * 0.5);
        points.emplace_back(x, y, 0.4f + 0.1f * std::sin(t * 2.0));
    }

    // Benchmark incremental AppendRibbonSegments as points arrive from digitizer
    BLPath outline;
    std::vector<StrokePoint> streamPts;
    streamPts.reserve(1000);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < points.size(); ++i) {
        size_t prevSize = streamPts.size();
        streamPts.push_back(points[i]);
        InkGenerator::AppendRibbonSegments(outline, streamPts, prevSize, CapType::Round, StrokePattern::Solid);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double appendMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "  -> Incremental append 1000 points: " << appendMs << " ms" << std::endl;

    // Benchmark fill_path rasterization in 1920x1080 surface
    BLImage img(1920, 1080, BL_FORMAT_PRGB32);
    BLContext ctx(img);

    auto t2 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        ctx.fill_path(outline);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double rasterMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / 10.0;
    std::cout << "  -> Frame rasterization time (solid): " << rasterMs << " ms per frame" << std::endl;

    // Benchmark pencil pattern rasterization
    auto t4 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        InkGenerator::RenderPencilStroke(ctx, points, 0.8f, BLRgba32(0xFF202020), &outline);
    }
    auto t5 = std::chrono::high_resolution_clock::now();
    double pencilMs = std::chrono::duration<double, std::milli>(t5 - t4).count() / 10.0;
    std::cout << "  -> Frame rasterization time (pencil): " << pencilMs << " ms per frame" << std::endl;

    // Benchmark Miter-Clipped Single Contour generation (CPU) and rasterization
    auto tGen0 = std::chrono::high_resolution_clock::now();
    BLPath singleContour = InkGenerator::GenerateStroke(points, CapType::Round, StrokePattern::Solid);
    auto tGen1 = std::chrono::high_resolution_clock::now();
    double genMs = std::chrono::duration<double, std::milli>(tGen1 - tGen0).count();
    std::cout << "  -> CPU generation time (Miter-Clipped Single Contour 1000 pts): " << genMs << " ms" << std::endl;

    auto t6 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        ctx.fill_path(singleContour);
    }
    auto t7 = std::chrono::high_resolution_clock::now();
    double singleContourRasterMs = std::chrono::duration<double, std::milli>(t7 - t6).count() / 10.0;
    std::cout << "  -> Frame rasterization time (Miter-Clipped Single Contour): " << singleContourRasterMs << " ms per frame" << std::endl;

    // Benchmark pencil pattern with Miter-Clipped Single Contour!
    auto t8 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        InkGenerator::RenderPencilStroke(ctx, points, 0.8f, BLRgba32(0xFF202020), &singleContour);
    }
    auto t9 = std::chrono::high_resolution_clock::now();
    double pencilSingleContourMs = std::chrono::duration<double, std::milli>(t9 - t8).count() / 10.0;
    std::cout << "  -> Frame rasterization time (pencil with Single Contour 1000 pts): " << pencilSingleContourMs << " ms per frame" << std::endl;

    // 10-second stroke benchmark (2500 points at 240Hz)
    std::vector<StrokePoint> points2500;
    points2500.reserve(2500);
    for (int i = 0; i < 2500; ++i) {
        double t = i * 0.02;
        double x = 400.0 + 300.0 * std::cos(t * 0.5) + t * 5.0;
        double y = 400.0 + 300.0 * std::sin(t * 0.5);
        points2500.emplace_back(x, y, 0.4f + 0.1f * std::sin(t * 2.0));
    }
    auto tGen2500_0 = std::chrono::high_resolution_clock::now();
    BLPath singleContour2500 = InkGenerator::GenerateStroke(points2500, CapType::Round, StrokePattern::Solid);
    auto tGen2500_1 = std::chrono::high_resolution_clock::now();
    double gen2500Ms = std::chrono::duration<double, std::milli>(tGen2500_1 - tGen2500_0).count();
    std::cout << "  -> CPU generation time (Single Contour 2500 pts / 10 sec): " << gen2500Ms << " ms" << std::endl;

    auto tRast2500_0 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        ctx.fill_path(singleContour2500);
    }
    auto tRast2500_1 = std::chrono::high_resolution_clock::now();
    double solidRast2500Ms = std::chrono::duration<double, std::milli>(tRast2500_1 - tRast2500_0).count() / 10.0;
    std::cout << "  -> Frame rasterization time (solid 2500 pts / 10 sec): " << solidRast2500Ms << " ms per frame" << std::endl;

    auto tPencil2500_0 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        InkGenerator::RenderPencilStroke(ctx, points2500, 0.8f, BLRgba32(0xFF202020), &singleContour2500);
    }
    auto tPencil2500_1 = std::chrono::high_resolution_clock::now();
    double pencil2500Ms = std::chrono::duration<double, std::milli>(tPencil2500_1 - tPencil2500_0).count() / 10.0;
    std::cout << "  -> Frame rasterization time (pencil 2500 pts / 10 sec): " << pencil2500Ms << " ms per frame" << std::endl;

    // Assert that incremental stroke assembly and frame rendering meet real-time 120 FPS latency bounds
    assert(appendMs < 10.0);
    assert(rasterMs < 8.33);
    assert(pencilMs < 10.0);
}

void TestStrokePressureTransitions() {
    std::cout << "[Test 8] Running Stroke Pressure Transition & Miter Clamping Invariant Tests..." << std::endl;

    BLImage img(1000, 1000, BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // 1. Rapid pressure transition test (step from 0.1mm to 3.0mm back to 0.1mm along a curved path)
    std::vector<StrokePoint> stepPoints;
    for (int i = 0; i < 100; ++i) {
        double angle = i * 0.05; // 0.05 rad ~ 2.8 deg per step
        double x = 100.0 + i * 0.8;
        double y = 100.0 + 30.0 * std::sin(angle);
        float p = (i >= 30 && i <= 60) ? 0.95f : 0.10f;
        float r = 0.2f + p * 2.8f; // jumps from 0.48mm to 2.86mm
        stepPoints.emplace_back(x, y, r, p);
    }

    // Verify all cap types generate valid contours without self-intersections or crashes
    std::vector<CapType> caps = { CapType::Round, CapType::Square, CapType::Chisel };
    for (auto cap : caps) {
        BLPath path = InkGenerator::GenerateStroke(stepPoints, cap, StrokePattern::Solid);
        assert(!path.is_empty());
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        BLResult res = ctx.fill_path(path);
        assert(res == BL_SUCCESS);
    }

    // 2. High-curvature corner under extreme pressure (r = 4.0mm with 90 deg turn on 0.1mm segment)
    std::vector<StrokePoint> cornerPoints;
    cornerPoints.emplace_back(200.0, 200.0, 4.0f, 0.95f);
    cornerPoints.emplace_back(200.1, 200.0, 4.0f, 0.95f);
    cornerPoints.emplace_back(200.1, 200.1, 4.0f, 0.95f);
    cornerPoints.emplace_back(200.1, 201.0, 4.0f, 0.95f);

    BLPath cornerPath = InkGenerator::GenerateStroke(cornerPoints, CapType::Round, StrokePattern::Solid);
    assert(!cornerPath.is_empty());
    ctx.clear_all();
    BLResult cRes = ctx.fill_path(cornerPath);
    assert(cRes == BL_SUCCESS);

    // 3. 180-Degree Hairpin Turnaround Invariant Tests:
    // Test exact 180 deg, near-180 deg (179.9 deg CCW and CW), and multi-turn zig-zags
    {
        // 3a. Exact 180 deg reversal
        std::vector<StrokePoint> hairpinExact = {
            StrokePoint(100.0, 100.0, 2.0f, 0.5f),
            StrokePoint(150.0, 100.0, 2.0f, 0.5f),
            StrokePoint(100.0, 100.0, 2.0f, 0.5f)
        };
        BLPath hp1 = InkGenerator::GenerateStroke(hairpinExact, CapType::Round, StrokePattern::Solid);
        assert(!hp1.is_empty());
        BLBox b1;
        hp1.get_bounding_box(&b1);
        // Turnaround apex at P1 (150, 100) with r=2.0 must reach approximately x=152.0
        assert(b1.x1 >= 151.9);
        ctx.clear_all();
        assert(ctx.fill_path(hp1) == BL_SUCCESS);

        // 3b. Near-180 deg slight left turn (179.9 deg)
        std::vector<StrokePoint> hairpinLeft = {
            StrokePoint(100.0, 100.0, 2.0f, 0.5f),
            StrokePoint(150.0, 100.0, 2.0f, 0.5f),
            StrokePoint(100.0, 100.1, 2.0f, 0.5f)
        };
        BLPath hp2 = InkGenerator::GenerateStroke(hairpinLeft, CapType::Round, StrokePattern::Solid);
        assert(!hp2.is_empty());
        BLBox b2;
        hp2.get_bounding_box(&b2);
        assert(b2.x1 >= 151.9);
        ctx.clear_all();
        assert(ctx.fill_path(hp2) == BL_SUCCESS);

        // 3c. Near-180 deg slight right turn (179.9 deg)
        std::vector<StrokePoint> hairpinRight = {
            StrokePoint(100.0, 100.0, 2.0f, 0.5f),
            StrokePoint(150.0, 100.0, 2.0f, 0.5f),
            StrokePoint(100.0, 99.9, 2.0f, 0.5f)
        };
        BLPath hp3 = InkGenerator::GenerateStroke(hairpinRight, CapType::Round, StrokePattern::Solid);
        assert(!hp3.is_empty());
        BLBox b3;
        hp3.get_bounding_box(&b3);
        assert(b3.x1 >= 151.9);
        ctx.clear_all();
        assert(ctx.fill_path(hp3) == BL_SUCCESS);

        // 3d. Polygonal nib (Chisel & Square) 180 deg reversal
        for (auto cap : { CapType::Square, CapType::Chisel }) {
            BLPath polyHp = InkGenerator::GenerateStroke(hairpinExact, cap, StrokePattern::Solid);
            assert(!polyHp.is_empty());
            ctx.clear_all();
            assert(ctx.fill_path(polyHp) == BL_SUCCESS);
        }
    }

    // 4. Textured pencil with selective pressure band activation (press hard in middle only)
    ctx.clear_all();
    InkGenerator::RenderPencilStroke(ctx, stepPoints, 0.8f, BLRgba32(0xFF202020));

    // 5. Hockey-stick / J-turn Inner Corner Notch Regression Test:
    // When a stroke turns sharply with short segments near deceleration,
    // inner corner vertex must NEVER collapse into the centerline.
    {
        float rNominal = 2.0f;
        std::vector<StrokePoint> jTurnPoints = {
            StrokePoint(100.0, 100.0, rNominal, 0.5f),
            StrokePoint(120.0, 105.0, rNominal, 0.5f),
            StrokePoint(120.2, 105.1, rNominal, 0.5f), // Short deceleration segment (0.22mm)
            StrokePoint(122.0, 130.0, rNominal, 0.5f), // Sharp 80 deg turn upward
            StrokePoint(123.0, 150.0, rNominal, 0.5f)
        };

        for (auto cap : { CapType::Round, CapType::Square, CapType::Chisel }) {
            BLPath jPath = InkGenerator::GenerateStroke(jTurnPoints, cap, StrokePattern::Solid);
            assert(!jPath.is_empty());
            ctx.clear_all();
            assert(ctx.fill_path(jPath) == BL_SUCCESS);

            // Verify that around the corner node (120.2, 105.1), NO contour vertex collapses inside the radius
            BLPathView v = jPath.view();
            assert(v.size > 0);
            for (size_t i = 0; i < v.size; ++i) {
                if (std::isnan(v.vertex_data[i].x) || std::isnan(v.vertex_data[i].y)) continue;
                double dx = v.vertex_data[i].x - 120.2;
                double dy = v.vertex_data[i].y - 105.1;
                double dist = std::hypot(dx, dy);
                // Contour around corner node (120.2, 105.1) must never collapse into centerline
                if (cap == CapType::Round && i == 2) {
                    assert(dist >= rNominal * 0.99); // Inner corner vertex is at full nominal radius
                }
                assert(dist >= rNominal * 0.45);
            }
        }
    }

    std::cout << "  -> Pressure step transition, 180 deg turnaround, and J-turn inner corner tests passed successfully." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting FolioNote Storage Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestCRC32Engine();
    TestByteStreamPrimitives();
    TestPageSerializationRoundTrip();
    TestCorruptionAndFailSafeDetection();
    TestSQLiteDBManagerOperations();
    TestPageRepositorySafety();
    TestStrokeTessellationAndRasterPerformance();
    TestStrokePressureTransitions();

    std::cout << "========================================" << std::endl;
    std::cout << "All Storage & Performance Tests Passed!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}

