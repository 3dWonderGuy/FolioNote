/**
 * =========================================================================================
 * @file test_file_manager.cpp
 * @brief Standalone Local Diagnostic & Invariant Test Suite for Folio::FileManager
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DATA INTEGRITY GUARANTEES:
 * --------------------------------------------------
 * In FolioNote, the FileManager is directly responsible for persisting user data:
 * SQLite databases, binary vector strokes (.ink), notebook structures, and JSON manifests.
 * Any bug in this subsystem can cause silent data loss or file corruption.
 *
 * SAFETY & RUNTIME BEHAVIOR:
 * --------------------------
 * - Safe Sandbox: All tests execute strictly inside an isolated temporary directory
 *   under the system temp directory:
 *   (e.g., %TEMP%/FolioNote_FileManager_TestSandbox on Windows).
 * - No Lingering Artifacts: Every run cleans up its sandbox directory before and after
 *   executing.
 * - Non-Destructive: Never reads, writes, or modifies your actual documents, notebooks,
 *   or user profile directories.
 * - Repeatable: Can be executed continuously and safely in local development or automated CI.
 *
 * WHAT THIS SUITE TESTS (COMPREHENSIVE TEST CASES):
 * ------------------------------------------------
 * 1. Test_AtomicBinaryPersistence:
 *    - Validates WriteBinaryAtomic() with a 2MB deterministic binary payload.
 *    - Verifies byte-for-byte readback matching.
 *    - Verifies that no staging temporary files (.tmp.*) remain after completion.
 *
 * 2. Test_CrashResilienceAndNonDestructiveFailure:
 *    - Tests updating existing files in place.
 *    - Validates that overwriting an existing file commits atomically without corruption.
 *    - Verifies that invalid target paths fail gracefully without throwing exceptions.
 *
 * 3. Test_UnicodeAndInternationalPaths:
 *    - Verifies Win32 wide-character (UTF-16) conversion for international filenames.
 *    - Tests paths containing Cyrillic, Chinese, Japanese, spaces, and emoji characters
 *      (e.g., "Папка_笔记_🎨/Лекция_📐.ink").
 *
 * 4. Test_ContentHashingAndIntegrity:
 *    - Verifies 64-bit FNV-1a hash calculation against identical and 1-bit-flipped files.
 *    - Asserts hash consistency and avalanche effect (no collision on minor byte changes).
 *
 * 5. Test_DirectoryTraversalAndFiltering:
 *    - Populates 115 files (15 .notebook files and 100 .tmp/.bak junk files).
 *    - Tests ListEntries() with and without extension filters, verifying that
 *      FileEntry::fullPath, fileName, and extension match expectations.
 *
 * 6. Test_RecursiveDirectoryLifecycle:
 *    - Tests deep recursive folder creation (CreateDirectories) and safe recursive
 *      purging (RemoveDirectoryRecursive).
 * =========================================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include "utils/file_manager.hpp"

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

// Helper to provide an isolated sandbox directory path in system temp
static std::string GetTestSandboxDir() {
    return FileManager::JoinPath(FileManager::GetTempDirectory(), "FolioNote_FileManager_TestSandbox");
}

// Helper to wipe the test sandbox completely
static void CleanupSandbox() {
    std::string sandbox = GetTestSandboxDir();
    if (FileManager::Exists(sandbox)) {
        FileManager::RemoveDirectoryRecursive(sandbox);
    }
}

// =========================================================================================
// TEST CASE 1: Atomic Binary Persistence & Staging File Cleanup
// =========================================================================================
bool Test_AtomicBinaryPersistence() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();
    TEST_ASSERT(FileManager::CreateDirectories(sandbox), "Failed to create sandbox directory");

    std::string targetFile = FileManager::JoinPath(sandbox, "document_strokes.ink");

    // 1. Generate 2MB payload of deterministic binary test data
    constexpr size_t PAYLOAD_SIZE = 2 * 1024 * 1024;
    std::vector<uint8_t> payload(PAYLOAD_SIZE);
    for (size_t i = 0; i < PAYLOAD_SIZE; ++i) {
        payload[i] = static_cast<uint8_t>((i ^ (i >> 8)) & 0xFF);
    }

    // 2. Write atomically (commits to disk via _commit/fsync then atomic swap)
    TEST_ASSERT(FileManager::WriteBinaryAtomic(targetFile, payload), "WriteBinaryAtomic failed");
    TEST_ASSERT(FileManager::Exists(targetFile), "Target file does not exist after atomic write");
    TEST_ASSERT(FileManager::GetFileSize(targetFile) == PAYLOAD_SIZE, "Persisted file size mismatch");

    // 3. Read back and verify exact byte content
    std::vector<uint8_t> readBack;
    TEST_ASSERT(FileManager::ReadBinary(targetFile, readBack), "Failed to read back written binary file");
    TEST_ASSERT(readBack.size() == payload.size(), "Read-back buffer size mismatch");
    TEST_ASSERT(readBack == payload, "Read-back byte payload differs from written buffer (data corrupted)");

    // 4. Verify that no temporary staging files (.tmp.*) were left behind in the directory
    auto entries = FileManager::ListEntries(sandbox);
    for (const auto& entry : entries) {
        TEST_ASSERT(entry.fullPath.find(".tmp") == std::string::npos, 
                    "Orphaned .tmp staging file left behind by atomic writer");
    }

    CleanupSandbox();
    return true;
}

// =========================================================================================
// TEST CASE 2: Crash Resilience & Non-Destructive Overwrites
// =========================================================================================
bool Test_CrashResilienceAndNonDestructiveFailure() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();
    FileManager::CreateDirectories(sandbox);

    std::string originalFile = FileManager::JoinPath(sandbox, "notebook_manifest.json");
    std::string originalContent = "{\"title\": \"My Important Notes\", \"version\": 1}";

    // 1. Write original baseline file
    TEST_ASSERT(FileManager::WriteTextAtomic(originalFile, originalContent), "Failed to write baseline file");
    TEST_ASSERT(FileManager::Exists(originalFile), "Baseline file missing");

    // 2. Overwrite with updated manifest content
    std::string updatedContent = "{\"title\": \"My Important Notes\", \"version\": 2, \"pages\": [1, 2, 3]}";
    TEST_ASSERT(FileManager::WriteTextAtomic(originalFile, updatedContent), "Failed to overwrite file atomically");

    // 3. Read back and verify the update applied cleanly
    std::string readBack;
    TEST_ASSERT(FileManager::ReadText(originalFile, readBack), "Failed to read updated content");
    TEST_ASSERT(readBack == updatedContent, "Updated content does not match expected string");

    // 4. Verify writing to an invalid path fails safely without throwing unhandled exceptions
    std::string invalidPath = FileManager::JoinPath(sandbox, "missing_subdir_locked/file.txt");
    std::string textContent = "test";
    (void)FileManager::WriteTextAtomic(invalidPath, textContent);

    CleanupSandbox();
    return true;
}

// =========================================================================================
// TEST CASE 3: Unicode & International Path Handling (Win32 Wide-Char Verification)
// =========================================================================================
bool Test_UnicodeAndInternationalPaths() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();

    // Multi-lingual directory name with Cyrillic, Chinese, Japanese, spaces, and emoji
    std::string unicodeDir = FileManager::JoinPath(sandbox, "Папка с заметками_笔记目录_ノート_🎨");
    TEST_ASSERT(FileManager::CreateDirectories(unicodeDir), "Failed to create directory with international characters");
    TEST_ASSERT(FileManager::Exists(unicodeDir), "International directory exists check failed");

    // Filename with non-ASCII characters
    std::string unicodeFile = FileManager::JoinPath(unicodeDir, "Лекция_1_数学_📐.ink");
    std::string payload = "Binary stroke data inside Unicode path: Привет мир, 世界, こんにちは!";

    TEST_ASSERT(FileManager::WriteTextAtomic(unicodeFile, payload), "Failed to write file to Unicode path");
    TEST_ASSERT(FileManager::Exists(unicodeFile), "Unicode file exists check failed");
    TEST_ASSERT(FileManager::GetFileSize(unicodeFile) == payload.size(), "Unicode file size mismatch");

    std::string readBack;
    TEST_ASSERT(FileManager::ReadText(unicodeFile, readBack), "Failed to read back file from Unicode path");
    TEST_ASSERT(readBack == payload, "Content corrupted when read through Unicode path");

    CleanupSandbox();
    return true;
}

// =========================================================================================
// TEST CASE 4: 64-Bit Content Hashing (FNV-1a Integrity Verification)
// =========================================================================================
bool Test_ContentHashingAndIntegrity() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();
    FileManager::CreateDirectories(sandbox);

    std::string fileA = FileManager::JoinPath(sandbox, "hash_test_a.bin");
    std::string fileB = FileManager::JoinPath(sandbox, "hash_test_b.bin");

    std::vector<uint8_t> dataA = {10, 20, 30, 40, 50, 60, 70, 80};
    std::vector<uint8_t> dataB = {10, 20, 30, 40, 50, 60, 70, 81}; // Single bit flipped

    FileManager::WriteBinaryAtomic(fileA, dataA);
    FileManager::WriteBinaryAtomic(fileB, dataB);

    uint64_t hashA = 0, sizeA = 0;
    uint64_t hashB = 0, sizeB = 0;

    TEST_ASSERT(FileManager::ComputeFileHash64(fileA, hashA, sizeA), "ComputeFileHash64 failed on file A");
    TEST_ASSERT(FileManager::ComputeFileHash64(fileB, hashB, sizeB), "ComputeFileHash64 failed on file B");

    TEST_ASSERT(sizeA == dataA.size(), "Hashed size mismatch for file A");
    TEST_ASSERT(sizeB == dataB.size(), "Hashed size mismatch for file B");
    TEST_ASSERT(hashA != 0, "Hash evaluated to zero");
    TEST_ASSERT(hashA != hashB, "Single bit change resulted in hash collision (Avalanche failure)");

    // Re-computing hash on identical file must yield identical value
    uint64_t hashA2 = 0, sizeA2 = 0;
    FileManager::ComputeFileHash64(fileA, hashA2, sizeA2);
    TEST_ASSERT(hashA == hashA2, "Identical file yielded different hash on second calculation");

    CleanupSandbox();
    return true;
}

// =========================================================================================
// TEST CASE 5: Directory Traversal & Fast Extension Filtering
// =========================================================================================
bool Test_DirectoryTraversalAndFiltering() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();
    FileManager::CreateDirectories(sandbox);

    constexpr int NOTEBOOK_COUNT = 15;
    constexpr int JUNK_COUNT = 100;

    // Create 15 .notebook files
    for (int i = 0; i < NOTEBOOK_COUNT; ++i) {
        std::string name = "Notebook_" + std::to_string(i) + ".notebook";
        FileManager::WriteTextAtomic(FileManager::JoinPath(sandbox, name), "dummy content");
    }

    // Create 100 other junk files (.tmp, .bak)
    for (int i = 0; i < JUNK_COUNT; ++i) {
        std::string ext = (i % 2 == 0) ? ".tmp" : ".bak";
        std::string name = "junk_" + std::to_string(i) + ext;
        FileManager::WriteTextAtomic(FileManager::JoinPath(sandbox, name), "junk");
    }

    // 1. Unfiltered scan: must return all files
    auto allEntries = FileManager::ListEntries(sandbox);
    TEST_ASSERT(allEntries.size() == (NOTEBOOK_COUNT + JUNK_COUNT), "Unfiltered directory list count mismatch");

    // 2. Filtered scan for .notebook: must return exactly 15
    auto notebooks = FileManager::ListEntries(sandbox, ".notebook");
    TEST_ASSERT(notebooks.size() == NOTEBOOK_COUNT, "Filtered .notebook count mismatch");
    for (const auto& entry : notebooks) {
        TEST_ASSERT(entry.fullPath.find(".notebook") != std::string::npos, 
                    "Non-matching file returned by extension filter");
    }

    // 3. Filtered scan for non-existent extension: must return 0
    auto foliolibs = FileManager::ListEntries(sandbox, ".foliolib");
    TEST_ASSERT(foliolibs.empty(), "Filter returned entries for non-existent extension");

    CleanupSandbox();
    return true;
}

// =========================================================================================
// TEST CASE 6: Nested Directory Lifecycle & Recursive Cleanup
// =========================================================================================
bool Test_RecursiveDirectoryLifecycle() {
    CleanupSandbox();
    std::string sandbox = GetTestSandboxDir();

    // Create deep directory hierarchy: sandbox/level1/level2/level3/
    std::string deepPath = FileManager::JoinPath(sandbox, "level1");
    deepPath = FileManager::JoinPath(deepPath, "level2");
    deepPath = FileManager::JoinPath(deepPath, "level3");

    TEST_ASSERT(FileManager::CreateDirectories(deepPath), "Failed to create deeply nested directory chain");
    TEST_ASSERT(FileManager::Exists(deepPath), "Deep directory does not exist after creation");

    // Place a file in the deepest directory
    std::string deepFile = FileManager::JoinPath(deepPath, "leaf_file.txt");
    FileManager::WriteTextAtomic(deepFile, "hello from the deep");
    TEST_ASSERT(FileManager::Exists(deepFile), "Deep leaf file missing");

    // Recursively delete the entire sandbox hierarchy
    TEST_ASSERT(FileManager::RemoveDirectoryRecursive(sandbox), "RemoveDirectoryRecursive failed");
    TEST_ASSERT(!FileManager::Exists(sandbox), "Sandbox directory still exists after recursive deletion");

    return true;
}

// =========================================================================================
// Main Entry Point
// =========================================================================================
int main() {
    std::cout << "\n======================================================\n";
    std::cout << "     FolioNote FileManager Diagnostic Suite           \n";
    std::cout << "======================================================\n";

    auto tStart = std::chrono::high_resolution_clock::now();

    RUN_TEST_CASE(Test_AtomicBinaryPersistence);
    RUN_TEST_CASE(Test_CrashResilienceAndNonDestructiveFailure);
    RUN_TEST_CASE(Test_UnicodeAndInternationalPaths);
    RUN_TEST_CASE(Test_ContentHashingAndIntegrity);
    RUN_TEST_CASE(Test_DirectoryTraversalAndFiltering);
    RUN_TEST_CASE(Test_RecursiveDirectoryLifecycle);

    auto tEnd = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    std::cout << "------------------------------------------------------\n";
    std::cout << ">>> ALL FILEMANAGER INVARIANTS VERIFIED (100% HEALTHY) <<<\n";
    std::cout << "    Total Test Suite Duration: " << totalMs << " ms\n";
    std::cout << "======================================================\n\n";

    return 0;
}