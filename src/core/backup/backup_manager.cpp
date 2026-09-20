/**
 * =========================================================================================
 * @file backup_manager.cpp
 * @brief Implementation of the Autonomous Backup & Disaster Recovery Engine
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Deep Filesystem Snapshots: Performs high-integrity recursive directory duplication
 *    capturing all nested subdirectories, SQLite structure databases, and .ink vector files.
 * 2. Self-Documenting Metadata: Writes `backup.meta` inside every snapshot directory with
 *    source lineage, timestamping, and entity telemetry.
 * 3. Non-Blocking ThreadPool Integration: All I/O heavy operations are offloaded to
 *    background worker threads, ensuring zero frame drops on the main UI/inking thread.
 * 4. Automated Retention Pruning: Calculates snapshot ages and enforces a hard cap on
 *    retained backups to keep disk footprint bounded.
 */

#include "core/backup/backup_manager.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <filesystem>

#include "utils/file_manager.hpp"
#include "utils/thread_pool.hpp"
#include "utils/logger.hpp"
#include "core/document/library/library.hpp"

namespace Folio {

namespace {

/**
 * @brief Generates a clean, filesystem-safe timestamp string.
 * Format: "YYYYMMDD_HHMMSS" (e.g., "20260920_013000").
 */
std::string GetTimestampString() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmVal{};
#if defined(_WIN32)
    localtime_s(&tmVal, &tt);
#else
    localtime_r(&tt, &tmVal);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmVal, "%Y%m%d_%H%M%S");
    return oss.str();
}

/**
 * @brief Generates a user-friendly human-readable timestamp.
 * Format: "YYYY-MM-DD HH:MM:SS" (e.g., "2026-09-20 01:30:00").
 */
std::string GetHumanTimestampString() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmVal{};
#if defined(_WIN32)
    localtime_s(&tmVal, &tt);
#else
    localtime_r(&tt, &tmVal);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmVal, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/**
 * @brief Calculates the recursive byte size of all files inside a directory.
 *
 * MATHEMATICAL PROCESS:
 * Accumulates $\sum \text{file\_size}(f)$ for all directory entries.
 * Skips inaccessible or symlinked files to prevent recursion loops.
 *
 * @param dirPath Path to evaluate.
 * @return Total accumulated size in bytes.
 */
uint64_t CalculateDirectorySize(const std::string& dirPath) {
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath, ec)) {
        if (ec) break;
        if (std::filesystem::is_regular_file(entry.status())) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

/**
 * @brief Writes self-documenting backup.meta file inside a backup snapshot directory.
 */
bool WriteBackupMetadata(
    const std::string& snapshotDir,
    const std::string& backupId,
    const std::string& sourceName,
    const std::string& sourcePath,
    bool isLibrary,
    size_t notebookCount
) {
    std::string metaPath = FileManager::JoinPath(snapshotDir, "backup.meta");
    uint64_t epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string timeStr = GetHumanTimestampString();

    std::ostringstream oss;
    oss << "# FolioNote Backup Metadata\n";
    oss << "format=FolioNoteBackup\n";
    oss << "version=1\n";
    oss << "id=" << backupId << "\n";
    oss << "sourceName=" << sourceName << "\n";
    oss << "sourcePath=" << FileManager::NormalizeSeparators(sourcePath) << "\n";
    oss << "isLibrary=" << (isLibrary ? "1" : "0") << "\n";
    oss << "timestamp=" << epochMs << "\n";
    oss << "timestampStr=" << timeStr << "\n";
    oss << "notebookCount=" << notebookCount << "\n";

    return FileManager::WriteTextAtomic(metaPath, oss.str());
}

/**
 * @brief Parses a backup.meta file into a BackupInfo structure.
 */
bool ParseBackupMetadata(const std::string& snapshotDir, BackupInfo& outInfo) {
    std::string metaPath = FileManager::JoinPath(snapshotDir, "backup.meta");
    std::string content;
    if (!FileManager::ReadText(metaPath, content)) {
        return false;
    }

    outInfo.backupPath = FileManager::NormalizeSeparators(snapshotDir);
    outInfo.totalBytes = CalculateDirectorySize(snapshotDir);

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "id") outInfo.backupId = val;
        else if (key == "sourceName") outInfo.sourceName = val;
        else if (key == "sourcePath") outInfo.sourcePath = val;
        else if (key == "isLibrary") outInfo.isLibrary = (val == "1" || val == "true");
        else if (key == "timestamp") {
            try { outInfo.timestamp = std::stoull(val); } catch (...) {}
        }
        else if (key == "timestampStr") outInfo.timestampStr = val;
        else if (key == "notebookCount") {
            try { outInfo.notebookCount = std::stoul(val); } catch (...) {}
        }
    }

    if (outInfo.backupId.empty()) {
        outInfo.backupId = FileManager::GetFileName(snapshotDir);
    }
    return true;
}

} // anonymous namespace

// =========================================================================================
// BackupManager Implementation
// =========================================================================================

bool BackupManager::BackupLibrary(const std::string& libraryPath, const std::string& customDestination) {
    if (libraryPath.empty() || !FileManager::IsDirectory(libraryPath)) {
        LOG_ERROR(BackupManager, "BackupLibrary failed: Invalid or missing library path: " + libraryPath);
        return false;
    }

    std::string libName = FileManager::GetStem(libraryPath);
    if (libName.empty()) libName = "Library";

    std::string backupsRoot = customDestination.empty() ? FileManager::GetBackupsDirectory() : customDestination;
    if (!FileManager::CreateDirectories(backupsRoot)) {
        LOG_ERROR(BackupManager, "BackupLibrary failed: Cannot create backups root directory: " + backupsRoot);
        return false;
    }

    std::string timeSuffix = GetTimestampString();
    std::string backupFolderName = libName + "_Backup_" + timeSuffix;
    std::string snapshotPath = FileManager::JoinPath(backupsRoot, backupFolderName);

    LOG_INFO(BackupManager, "Starting backup of library '" + libName + "' to: " + snapshotPath);

    std::error_code ec;
    std::filesystem::create_directories(snapshotPath, ec);
    if (ec) {
        LOG_ERROR(BackupManager, "Failed to create snapshot directory: " + ec.message());
        return false;
    }

    // Count notebooks inside source library
    size_t nbCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(libraryPath, ec)) {
        if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == ".notebook") {
            nbCount++;
        }
    }

    // Recursively copy entire library contents to snapshot
    std::filesystem::copy(
        libraryPath,
        snapshotPath,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec
    );

    if (ec) {
        LOG_ERROR(BackupManager, "BackupLibrary recursive copy failed: " + ec.message());
        std::filesystem::remove_all(snapshotPath, ec);
        return false;
    }

    // Write self-documenting backup metadata marker
    std::string backupId = "backup_lib_" + timeSuffix;
    WriteBackupMetadata(snapshotPath, backupId, libName, libraryPath, true, nbCount);

    uint64_t totalBytes = CalculateDirectorySize(snapshotPath);
    LOG_INFO(BackupManager, "Successfully completed library backup. Snapshot: '" + snapshotPath +
             "' | Size: " + std::to_string(totalBytes / 1024) + " KB | Notebooks: " + std::to_string(nbCount));
    return true;
}

std::future<bool> BackupManager::BackupLibraryAsync(const std::string& libraryPath, const std::string& customDestination) {
    return GetGlobalThreadPool().Enqueue([libraryPath, customDestination]() -> bool {
        return BackupLibrary(libraryPath, customDestination);
    });
}

bool BackupManager::BackupNotebook(const std::string& notebookPath, const std::string& customDestination) {
    if (notebookPath.empty() || !FileManager::IsDirectory(notebookPath)) {
        LOG_ERROR(BackupManager, "BackupNotebook failed: Invalid or missing notebook path: " + notebookPath);
        return false;
    }

    std::string nbName = FileManager::GetStem(notebookPath);
    if (nbName.empty()) nbName = "Notebook";

    std::string backupsRoot = customDestination.empty() ? FileManager::GetBackupsDirectory() : customDestination;
    if (!FileManager::CreateDirectories(backupsRoot)) {
        LOG_ERROR(BackupManager, "BackupNotebook failed: Cannot create backups root directory: " + backupsRoot);
        return false;
    }

    std::string timeSuffix = GetTimestampString();
    std::string backupFolderName = nbName + "_Notebook_Backup_" + timeSuffix;
    std::string snapshotPath = FileManager::JoinPath(backupsRoot, backupFolderName);

    LOG_INFO(BackupManager, "Starting backup of notebook '" + nbName + "' to: " + snapshotPath);

    std::error_code ec;
    std::filesystem::create_directories(snapshotPath, ec);
    if (ec) {
        LOG_ERROR(BackupManager, "Failed to create notebook snapshot directory: " + ec.message());
        return false;
    }

    // Recursively copy notebook directory to snapshot
    std::filesystem::copy(
        notebookPath,
        snapshotPath,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec
    );

    if (ec) {
        LOG_ERROR(BackupManager, "BackupNotebook recursive copy failed: " + ec.message());
        std::filesystem::remove_all(snapshotPath, ec);
        return false;
    }

    // Write backup metadata marker
    std::string backupId = "backup_nb_" + timeSuffix;
    WriteBackupMetadata(snapshotPath, backupId, nbName, notebookPath, false, 1);

    uint64_t totalBytes = CalculateDirectorySize(snapshotPath);
    LOG_INFO(BackupManager, "Successfully completed notebook backup. Snapshot: '" + snapshotPath +
             "' | Size: " + std::to_string(totalBytes / 1024) + " KB");
    return true;
}

std::future<bool> BackupManager::BackupNotebookAsync(const std::string& notebookPath, const std::string& customDestination) {
    return GetGlobalThreadPool().Enqueue([notebookPath, customDestination]() -> bool {
        return BackupNotebook(notebookPath, customDestination);
    });
}

std::vector<BackupInfo> BackupManager::ListBackups(const std::string& backupRootDir) {
    std::string root = backupRootDir.empty() ? FileManager::GetBackupsDirectory() : backupRootDir;
    std::vector<BackupInfo> backups;

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return backups;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (std::filesystem::is_directory(entry.status())) {
            BackupInfo info;
            if (ParseBackupMetadata(entry.path().string(), info)) {
                backups.push_back(info);
            }
        }
    }

    // Sort by timestamp descending (newest first)
    std::sort(backups.begin(), backups.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.timestamp > b.timestamp;
    });

    return backups;
}

bool BackupManager::RestoreBackup(const std::string& backupSnapshotPath, const std::string& targetDestinationDir) {
    if (backupSnapshotPath.empty() || !FileManager::IsDirectory(backupSnapshotPath)) {
        LOG_ERROR(BackupManager, "RestoreBackup rejected: Invalid backup snapshot path: " + backupSnapshotPath);
        return false;
    }
    if (targetDestinationDir.empty()) {
        LOG_ERROR(BackupManager, "RestoreBackup rejected: Empty target destination directory.");
        return false;
    }

    BackupInfo info;
    if (!ParseBackupMetadata(backupSnapshotPath, info)) {
        LOG_WARN(BackupManager, "RestoreBackup: Snapshot lacks valid backup.meta; proceeding with raw file copy.");
    }

    std::error_code ec;
    FileManager::CreateDirectories(targetDestinationDir);

    LOG_INFO(BackupManager, "Restoring backup snapshot '" + backupSnapshotPath + "' into '" + targetDestinationDir + "'");

    // Copy snapshot directory contents into destination
    std::filesystem::copy(
        backupSnapshotPath,
        targetDestinationDir,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec
    );

    if (ec) {
        LOG_ERROR(BackupManager, "RestoreBackup copy operation failed: " + ec.message());
        return false;
    }

    // Remove the auxiliary backup.meta from the restored destination so it looks like a clean library/notebook
    std::string metaInDest = FileManager::JoinPath(targetDestinationDir, "backup.meta");
    if (FileManager::Exists(metaInDest)) {
        FileManager::DeleteFile(metaInDest);
    }

    LOG_INFO(BackupManager, "RestoreBackup successfully restored to: " + targetDestinationDir);
    return true;
}

std::future<bool> BackupManager::RestoreBackupAsync(const std::string& backupSnapshotPath, const std::string& targetDestinationDir) {
    return GetGlobalThreadPool().Enqueue([backupSnapshotPath, targetDestinationDir]() -> bool {
        return RestoreBackup(backupSnapshotPath, targetDestinationDir);
    });
}

bool BackupManager::DeleteBackup(const std::string& backupSnapshotPath) {
    if (backupSnapshotPath.empty() || !FileManager::IsDirectory(backupSnapshotPath)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::remove_all(backupSnapshotPath, ec);
    if (!ec) {
        LOG_INFO(BackupManager, "Deleted backup snapshot: " + backupSnapshotPath);
        return true;
    }
    LOG_ERROR(BackupManager, "Failed to delete backup snapshot: " + backupSnapshotPath + " | Error: " + ec.message());
    return false;
}

size_t BackupManager::PruneOldBackups(const std::string& sourceName, size_t maxToKeep, const std::string& backupRootDir) {
    auto allBackups = ListBackups(backupRootDir);
    if (allBackups.empty()) return 0;

    // Filter by sourceName if provided
    std::vector<BackupInfo> matching;
    for (const auto& b : allBackups) {
        if (sourceName.empty() || b.sourceName == sourceName) {
            matching.push_back(b);
        }
    }

    if (matching.size() <= maxToKeep) {
        return 0;
    }

    // matching is already sorted newest first; items from index maxToKeep onward should be purged
    size_t prunedCount = 0;
    for (size_t i = maxToKeep; i < matching.size(); ++i) {
        if (DeleteBackup(matching[i].backupPath)) {
            prunedCount++;
        }
    }

    LOG_INFO(BackupManager, "PruneOldBackups: Purged " + std::to_string(prunedCount) +
             " obsolete backup snapshots for '" + (sourceName.empty() ? "All" : sourceName) + "'.");
    return prunedCount;
}

} // namespace Folio
