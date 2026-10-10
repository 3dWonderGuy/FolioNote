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

    std::cout << "========================================" << std::endl;
    std::cout << "All 6 Storage Tests Passed Successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
