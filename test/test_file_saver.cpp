/**
 * =========================================================================================
 * @file test_file_saver.cpp
 * @brief Standalone Diagnostic Test Suite for Folio::FileSaver Facade
 * =========================================================================================
 *
 * ARCHITECTURAL CONTEXT:
 * ----------------------
 * FileSaver acts as a compatibility and convenience facade forwarding atomic saving
 * and directory preparation operations to Folio::FileManager.
 *
 * WHAT THIS SUITE TESTS:
 * ----------------------
 * 1. Test_FileSaver_WriteString:
 *    - Verifies FileSaver::WriteString persists text atomically through FileManager.
 *    - Reads back text to guarantee full integrity.
 *
 * 2. Test_FileSaver_WriteBuffer:
 *    - Verifies FileSaver::WriteBuffer writes raw binary payload buffers atomically.
 *    - Verifies file size and contents byte-for-byte.
 *
 * 3. Test_FileSaver_CreateParentDirectories:
 *    - Validates creation of deeply nested parent directories given a candidate file path.
 *    - Confirms parent directories exist while the file itself is not created prematurely.
 *    - Validates that paths without directory components safely return true.
 * =========================================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <chrono>

#include "io/file_writer.hpp"
#include "io/file_manager.hpp"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n  [FAILED] " << msg << "\n" \
                      << "  File: " << __FILE__ << " | Line: " << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST_CASE(fn) \
    do { \
        std::cout << "[RUNNING] " << #fn << "... " << std::flush; \
        auto t0 = std::chrono::high_resolution_clock::now(); \
        if (fn()) { \
            auto t1 = std::chrono::high_resolution_clock::now(); \
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count(); \
            std::cout << "PASSED (" << ms << " ms)\n"; \
        } else { \
            std::cout << ">> TEST SUITE ABORTED ON FAILURE <<\n"; \
            return 1; \
        } \
    } while (0)

using namespace Folio;

static std::string GetTestSandboxDir() {
    return FileManager::JoinPath(FileManager::GetTempDirectory(), "FolioNote_FileSaver_TestSandbox");
}

static void CleanupSandbox() {
    std::string sandbox = GetTestSandboxDir();
    if (FileManager::Exists(sandbox)) {
        FileManager::RemoveDirectoryRecursive(sandbox);
    }
}

// =========================================================================================
// TEST CASE 1: FileSaver::WriteString Atomic Facade
// =========================================================================================
bool Test_FileSaver_WriteString() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();
    FileManager::CreateDirectories(sandbox);

    std::string targetPath = FileManager::JoinPath(sandbox, "notes_facade.json");
    std::string expectedText = "{\"facade\": true, \"engine\": \"FolioNote\", \"status\": \"verified\"}";

    TEST_ASSERT(FileSaver::WriteString(targetPath, expectedText), "FileSaver::WriteString returned false");
    TEST_ASSERT(FileManager::Exists(targetPath), "File does not exist after FileSaver::WriteString");

    std::string readBack;
    TEST_ASSERT(FileManager::ReadText(targetPath, readBack), "Failed to read back text written via FileSaver");
    TEST_ASSERT(readBack == expectedText, "Readback text mismatch");

    CleanupSandbox();
    return true;
}

// =========================================================================================
// TEST CASE 2: FileSaver::WriteBuffer Binary Facade
// =========================================================================================
bool Test_FileSaver_WriteBuffer() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();
    FileManager::CreateDirectories(sandbox);

    std::string targetPath = FileManager::JoinPath(sandbox, "binary_facade.ink");
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};

    TEST_ASSERT(FileSaver::WriteBuffer(targetPath, payload), "FileSaver::WriteBuffer returned false");
    TEST_ASSERT(FileManager::Exists(targetPath), "File does not exist after FileSaver::WriteBuffer");
    TEST_ASSERT(FileManager::GetFileSize(targetPath) == payload.size(), "File size mismatch on disk");

    std::vector<uint8_t> readBack;
    TEST_ASSERT(FileManager::ReadBinary(targetPath, readBack), "Failed to read back binary data");
    TEST_ASSERT(readBack == payload, "Binary data corrupted during FileSaver::WriteBuffer round-trip");

    CleanupSandbox();
    return true;
}

// =========================================================================================
// TEST CASE 3: FileSaver::CreateParentDirectories
// =========================================================================================
bool Test_FileSaver_CreateParentDirectories() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();

    // 1. Nested hierarchy: sandbox/subA/subB/target.txt
    std::string nestedPath = FileManager::JoinPath(sandbox, "subA", "subB");
    std::string targetFilePath = FileManager::JoinPath(nestedPath, "target.txt");

    TEST_ASSERT(FileSaver::CreateParentDirectories(targetFilePath), "CreateParentDirectories failed for nested path");
    TEST_ASSERT(FileManager::IsDirectory(nestedPath), "Parent directory was not created on disk");
    TEST_ASSERT(!FileManager::Exists(targetFilePath), "Target file should not exist yet");

    // 2. Path with no parent component (pure filename) must safely return true
    TEST_ASSERT(FileSaver::CreateParentDirectories("local_document.json"), 
                "CreateParentDirectories should return true for root-level filenames");

    CleanupSandbox();
    return true;
}

// =========================================================================================
// Main Entry Point
// =========================================================================================
int main() {
    std::cout << "\n======================================================\n";
    std::cout << "      FolioNote FileSaver Diagnostic Suite            \n";
    std::cout << "======================================================\n";

    auto tStart = std::chrono::high_resolution_clock::now();

    RUN_TEST_CASE(Test_FileSaver_WriteString);
    RUN_TEST_CASE(Test_FileSaver_WriteBuffer);
    RUN_TEST_CASE(Test_FileSaver_CreateParentDirectories);

    auto tEnd = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    std::cout << "------------------------------------------------------\n";
    std::cout << ">>> ALL FILESAVER INVARIANTS VERIFIED (100% HEALTHY) <<<\n";
    std::cout << "    Total Test Suite Duration: " << totalMs << " ms\n";
    std::cout << "======================================================\n\n";

    return 0;
}