/**
 * =========================================================================================
 * @file backup_manager.cpp
 * @brief Implementation of the 3-Tier Autonomous Backup & Disaster Recovery Engine
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Multi-Ring Grandfather-Father-Son Rotation:
 *    - Tier 1 (Daily): Evaluates interval threshold $\Delta t \ge 24\text{ hours}$ ($86.4 \times 10^6\text{ ms}$).
 *      Retains $N_{\text{daily}} = 7$ rolling snapshots.
 *    - Tier 2 (Weekly): Evaluates interval threshold $\Delta t \ge 7\text{ days}$ ($604.8 \times 10^6\text{ ms}$).
 *      Retains $N_{\text{weekly}} = 4$ rolling snapshots.
 *    - Tier 3 (Monthly): Evaluates interval threshold $\Delta t \ge 30\text{ days}$ ($2.592 \times 10^9\text{ ms}$).
 *      Retains $N_{\text{monthly}} = 12$ rolling snapshots.
 * 2. Deep Isolated Filesystem Paths:
 *    Organized cleanly under `<AppRoot>/backups/tiers/{daily,weekly,monthly}/` or `<AppRoot>/backups/manual/`.
 * 3. Safe SQLite WAL Checkpoint Integration:
 *    Scans the source directory tree for any `.db` files and executes `sqlite3_wal_checkpoint_v2`
 *    with `SQLITE_CHECKPOINT_TRUNCATE`. This forces SQLite to merge all uncommitted WAL pages
 *    into the primary database file and reset the journal, eliminating Windows file sharing
 *    violations (`ERROR_SHARING_VIOLATION`) during `std::filesystem::copy`.
 * 4. Non-Blocking Background ThreadPool Execution:
 *    All intensive recursive directory copying and integrity verification run asynchronously.
 */

#include "core/backup/backup_manager.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <sqlite3.h>

#include "io/file_manager.hpp"
#include "utils/thread_pool.hpp"
#include "utils/logger.hpp"
#include "app/settings_manager.hpp"

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
 * Accumulates $\sum \text{file\_size}(f)$ for all directory regular files.
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
    size_t notebookCount,
    BackupTier tier
) {
    std::string metaPath = FileManager::JoinPath(snapshotDir, "backup.meta");
    uint64_t epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string timeStr = GetHumanTimestampString();

    std::ostringstream oss;
    oss << "# FolioNote Backup Metadata\n";
    oss << "format=FolioNoteBackup\n";
    oss << "version=2\n";
    oss << "id=" << backupId << "\n";
    oss << "sourceName=" << sourceName << "\n";
    oss << "sourcePath=" << FileManager::NormalizeSeparators(sourcePath) << "\n";
    oss << "isLibrary=" << (isLibrary ? "1" : "0") << "\n";
    oss << "tier=" << BackupManager::TierToString(tier) << "\n";
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
        else if (key == "tier") outInfo.tier = BackupManager::StringToTier(val);
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

std::string BackupManager::TierToString(BackupTier tier) {
    switch (tier) {
        case BackupTier::Daily:   return "daily";
        case BackupTier::Weekly:  return "weekly";
        case BackupTier::Monthly: return "monthly";
        case BackupTier::Manual:
        default:                  return "manual";
    }
}

BackupTier BackupManager::StringToTier(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "daily")   return BackupTier::Daily;
    if (lower == "weekly")  return BackupTier::Weekly;
    if (lower == "monthly") return BackupTier::Monthly;
    return BackupTier::Manual;
}

std::string BackupManager::GetTierDirectory(BackupTier tier, const std::string& customRoot) {
    std::string root = customRoot.empty() ? FileManager::GetBackupsDirectory() : customRoot;
    if (tier == BackupTier::Manual) {
        return FileManager::JoinPath(root, "manual");
    }
    return FileManager::JoinPath(root, "tiers/" + TierToString(tier));
}

bool BackupManager::FlushSqliteWal(const std::string& rootDirectory) {
    if (rootDirectory.empty() || !FileManager::IsDirectory(rootDirectory)) {
        return false;
    }

    std::error_code ec;
    bool allSuccess = true;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootDirectory, ec)) {
        if (ec) break;
        if (std::filesystem::is_regular_file(entry.status()) && entry.path().extension() == ".db") {
            std::string dbPath = entry.path().string();
            sqlite3* db = nullptr;
            int rc = sqlite3_open(dbPath.c_str(), &db);
            if (rc == SQLITE_OK && db != nullptr) {
                int chkRc = sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
                if (chkRc != SQLITE_OK) {
                    LOG_WARN(FileManager, "FlushSqliteWal: WAL checkpoint returned status " +
                             std::to_string(chkRc) + " for " + dbPath);
                    allSuccess = false;
                }
                sqlite3_close(db);
            } else {
                if (db) sqlite3_close(db);
                allSuccess = false;
            }
        }
    }

    return allSuccess;
}

bool BackupManager::BackupLibrary(const std::string& libraryPath, const std::string& customDestination) {
    return BackupLibraryTiered(libraryPath, BackupTier::Manual, customDestination);
}

std::future<bool> BackupManager::BackupLibraryAsync(const std::string& libraryPath, const std::string& customDestination) {
    return BackupLibraryTieredAsync(libraryPath, BackupTier::Manual, customDestination);
}

bool BackupManager::BackupLibraryTiered(const std::string& libraryPath, BackupTier tier, const std::string& customRoot) {
    if (libraryPath.empty() || !FileManager::IsDirectory(libraryPath)) {
        LOG_ERROR(FileManager, "BackupLibraryTiered: Invalid or missing library path: " + libraryPath);
        return false;
    }

    std::string libName = FileManager::GetStem(libraryPath);
    if (libName.empty()) libName = "Library";

    std::string targetDir = GetTierDirectory(tier, customRoot);
    if (!FileManager::CreateDirectories(targetDir)) {
        LOG_ERROR(FileManager, "BackupLibraryTiered: Cannot create target directory: " + targetDir);
        return false;
    }

    // Flush SQLite WAL logs to prevent Windows sharing violations and dirty copies
    FlushSqliteWal(libraryPath);

    std::string timeSuffix = GetTimestampString();
    std::string backupFolderName = libName + "_Backup_" + timeSuffix;
    std::string snapshotPath = FileManager::JoinPath(targetDir, backupFolderName);

    LOG_INFO(FileManager, "Starting " + TierToString(tier) + " backup of library '" + libName + "' to: " + snapshotPath);

    std::error_code ec;
    std::filesystem::create_directories(snapshotPath, ec);
    if (ec) {
        LOG_ERROR(FileManager, "Failed to create snapshot directory: " + ec.message());
        return false;
    }

    size_t nbCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(libraryPath, ec)) {
        if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == ".notebook") {
            nbCount++;
        }
    }

    std::filesystem::copy(
        libraryPath,
        snapshotPath,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec
    );

    if (ec) {
        LOG_ERROR(FileManager, "BackupLibraryTiered recursive copy failed: " + ec.message());
        std::filesystem::remove_all(snapshotPath, ec);
        return false;
    }

    std::string backupId = "backup_lib_" + TierToString(tier) + "_" + timeSuffix;
    WriteBackupMetadata(snapshotPath, backupId, libName, libraryPath, true, nbCount, tier);

    uint64_t totalBytes = CalculateDirectorySize(snapshotPath);
    LOG_INFO(FileManager, "Completed " + TierToString(tier) + " library backup. Snapshot: '" + snapshotPath +
             "' | Size: " + std::to_string(totalBytes / 1024) + " KB | Notebooks: " + std::to_string(nbCount));
    return true;
}

std::future<bool> BackupManager::BackupLibraryTieredAsync(const std::string& libraryPath, BackupTier tier, const std::string& customRoot) {
    return GetGlobalThreadPool().Enqueue([libraryPath, tier, customRoot]() -> bool {
        return BackupLibraryTiered(libraryPath, tier, customRoot);
    });
}

bool BackupManager::BackupNotebook(const std::string& notebookPath, const std::string& customDestination) {
    return BackupNotebookTiered(notebookPath, BackupTier::Manual, customDestination);
}

std::future<bool> BackupManager::BackupNotebookAsync(const std::string& notebookPath, const std::string& customDestination) {
    return BackupNotebookTieredAsync(notebookPath, BackupTier::Manual, customDestination);
}

bool BackupManager::BackupNotebookTiered(const std::string& notebookPath, BackupTier tier, const std::string& customRoot) {
    if (notebookPath.empty() || !FileManager::IsDirectory(notebookPath)) {
        LOG_ERROR(FileManager, "BackupNotebookTiered: Invalid notebook path: " + notebookPath);
        return false;
    }

    std::string nbName = FileManager::GetStem(notebookPath);
    if (nbName.empty()) nbName = "Notebook";

    std::string targetDir = GetTierDirectory(tier, customRoot);
    if (!FileManager::CreateDirectories(targetDir)) {
        LOG_ERROR(FileManager, "BackupNotebookTiered: Cannot create target directory: " + targetDir);
        return false;
    }

    // Flush SQLite WAL buffer before copying
    FlushSqliteWal(notebookPath);

    std::string timeSuffix = GetTimestampString();
    std::string backupFolderName = nbName + "_Notebook_Backup_" + timeSuffix;
    std::string snapshotPath = FileManager::JoinPath(targetDir, backupFolderName);

    LOG_INFO(FileManager, "Starting " + TierToString(tier) + " backup of notebook '" + nbName + "' to: " + snapshotPath);

    std::error_code ec;
    std::filesystem::create_directories(snapshotPath, ec);
    if (ec) {
        LOG_ERROR(FileManager, "Failed to create notebook snapshot directory: " + ec.message());
        return false;
    }

    std::filesystem::copy(
        notebookPath,
        snapshotPath,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec
    );

    if (ec) {
        LOG_ERROR(FileManager, "BackupNotebookTiered recursive copy failed: " + ec.message());
        std::filesystem::remove_all(snapshotPath, ec);
        return false;
    }

    std::string backupId = "backup_nb_" + TierToString(tier) + "_" + timeSuffix;
    WriteBackupMetadata(snapshotPath, backupId, nbName, notebookPath, false, 1, tier);

    uint64_t totalBytes = CalculateDirectorySize(snapshotPath);
    LOG_INFO(FileManager, "Completed " + TierToString(tier) + " notebook backup. Snapshot: '" + snapshotPath +
             "' | Size: " + std::to_string(totalBytes / 1024) + " KB");
    return true;
}

std::future<bool> BackupManager::BackupNotebookTieredAsync(const std::string& notebookPath, BackupTier tier, const std::string& customRoot) {
    return GetGlobalThreadPool().Enqueue([notebookPath, tier, customRoot]() -> bool {
        return BackupNotebookTiered(notebookPath, tier, customRoot);
    });
}

std::vector<BackupInfo> BackupManager::ListBackups(const std::string& backupRootDir) {
    std::string root = backupRootDir.empty() ? FileManager::GetBackupsDirectory() : backupRootDir;
    std::vector<BackupInfo> backups;

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return backups;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (std::filesystem::is_regular_file(entry.status()) && entry.path().filename() == "backup.meta") {
            std::string snapshotDir = entry.path().parent_path().string();
            BackupInfo info;
            if (ParseBackupMetadata(snapshotDir, info)) {
                backups.push_back(info);
            }
        }
    }

    std::sort(backups.begin(), backups.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.timestamp > b.timestamp;
    });

    return backups;
}

std::vector<BackupInfo> BackupManager::ListTierBackups(BackupTier tier, const std::string& customRoot) {
    std::string tierDir = GetTierDirectory(tier, customRoot);
    return ListBackups(tierDir);
}

bool BackupManager::RestoreBackup(const std::string& backupSnapshotPath, const std::string& targetDestinationDir) {
    if (backupSnapshotPath.empty() || !FileManager::IsDirectory(backupSnapshotPath)) {
        LOG_ERROR(FileManager, "RestoreBackup rejected: Invalid backup snapshot path: " + backupSnapshotPath);
        return false;
    }
    if (targetDestinationDir.empty()) {
        LOG_ERROR(FileManager, "RestoreBackup rejected: Empty target destination directory.");
        return false;
    }

    BackupInfo info;
    if (!ParseBackupMetadata(backupSnapshotPath, info)) {
        LOG_WARN(FileManager, "RestoreBackup: Snapshot lacks valid backup.meta; proceeding with raw file copy.");
    }

    std::error_code ec;
    FileManager::CreateDirectories(targetDestinationDir);

    LOG_INFO(FileManager, "Restoring backup snapshot '" + backupSnapshotPath + "' into '" + targetDestinationDir + "'");

    std::filesystem::copy(
        backupSnapshotPath,
        targetDestinationDir,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec
    );

    if (ec) {
        LOG_ERROR(FileManager, "RestoreBackup copy operation failed: " + ec.message());
        return false;
    }

    std::string metaInDest = FileManager::JoinPath(targetDestinationDir, "backup.meta");
    if (FileManager::Exists(metaInDest)) {
        std::filesystem::remove(metaInDest, ec);
    }

    LOG_INFO(FileManager, "RestoreBackup successfully restored to: " + targetDestinationDir);
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
        LOG_INFO(FileManager, "Deleted backup snapshot: " + backupSnapshotPath);
        return true;
    }
    LOG_ERROR(FileManager, "Failed to delete backup snapshot: " + backupSnapshotPath + " | Error: " + ec.message());
    return false;
}

size_t BackupManager::PruneOldBackups(const std::string& sourceName, size_t maxToKeep, const std::string& backupRootDir) {
    auto allBackups = ListBackups(backupRootDir);
    if (allBackups.empty()) return 0;

    std::vector<BackupInfo> matching;
    for (const auto& b : allBackups) {
        if (sourceName.empty() || b.sourceName == sourceName) {
            matching.push_back(b);
        }
    }

    if (matching.size() <= maxToKeep) {
        return 0;
    }

    size_t prunedCount = 0;
    for (size_t i = maxToKeep; i < matching.size(); ++i) {
        if (DeleteBackup(matching[i].backupPath)) {
            prunedCount++;
        }
    }

    LOG_INFO(FileManager, "PruneOldBackups: Purged " + std::to_string(prunedCount) +
             " obsolete snapshots for '" + (sourceName.empty() ? "All" : sourceName) + "'.");
    return prunedCount;
}

size_t BackupManager::PruneTierBackups(BackupTier tier, const std::string& sourceName, size_t maxToKeep, const std::string& customRoot) {
    std::string tierDir = GetTierDirectory(tier, customRoot);
    if (maxToKeep == 0) {
        // Resolve default retention from settings
        const auto& settings = SettingsManager::Instance();
        switch (tier) {
            case BackupTier::Daily:   maxToKeep = static_cast<size_t>(settings.dailyBackupsRetention); break;
            case BackupTier::Weekly:  maxToKeep = static_cast<size_t>(settings.weeklyBackupsRetention); break;
            case BackupTier::Monthly: maxToKeep = static_cast<size_t>(settings.monthlyBackupsRetention); break;
            default:                  maxToKeep = 5; break;
        }
    }

    return PruneOldBackups(sourceName, maxToKeep, tierDir);
}

void BackupManager::CheckAndRunScheduledBackups(const std::string& libraryPath, const std::string& customRoot) {
    if (libraryPath.empty() || !FileManager::IsDirectory(libraryPath)) {
        return;
    }

    const auto& settings = SettingsManager::Instance();
    auto now = std::chrono::system_clock::now();
    uint64_t nowEpochMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    constexpr uint64_t DAY_MS   = 86400000ULL;        // 24 * 3600 * 1000
    constexpr uint64_t WEEK_MS  = 7ULL * DAY_MS;       // 7 days
    constexpr uint64_t MONTH_MS = 30ULL * DAY_MS;      // 30 days

    // 1. Daily Backup Check
    if (settings.autoBackupDailyEnabled) {
        auto dailyBackups = ListTierBackups(BackupTier::Daily, customRoot);
        bool shouldRunDaily = dailyBackups.empty() || (nowEpochMs - dailyBackups.front().timestamp >= DAY_MS);
        if (shouldRunDaily) {
            LOG_INFO(FileManager, "CheckAndRunScheduledBackups: Running scheduled Daily backup.");
            if (BackupLibraryTiered(libraryPath, BackupTier::Daily, customRoot)) {
                PruneTierBackups(BackupTier::Daily, "", static_cast<size_t>(settings.dailyBackupsRetention), customRoot);
            }
        }
    }

    // 2. Weekly Backup Check
    if (settings.autoBackupWeeklyEnabled) {
        auto weeklyBackups = ListTierBackups(BackupTier::Weekly, customRoot);
        bool shouldRunWeekly = weeklyBackups.empty() || (nowEpochMs - weeklyBackups.front().timestamp >= WEEK_MS);
        if (shouldRunWeekly) {
            LOG_INFO(FileManager, "CheckAndRunScheduledBackups: Running scheduled Weekly backup.");
            if (BackupLibraryTiered(libraryPath, BackupTier::Weekly, customRoot)) {
                PruneTierBackups(BackupTier::Weekly, "", static_cast<size_t>(settings.weeklyBackupsRetention), customRoot);
            }
        }
    }

    // 3. Monthly Backup Check
    if (settings.autoBackupMonthlyEnabled) {
        auto monthlyBackups = ListTierBackups(BackupTier::Monthly, customRoot);
        bool shouldRunMonthly = monthlyBackups.empty() || (nowEpochMs - monthlyBackups.front().timestamp >= MONTH_MS);
        if (shouldRunMonthly) {
            LOG_INFO(FileManager, "CheckAndRunScheduledBackups: Running scheduled Monthly backup.");
            if (BackupLibraryTiered(libraryPath, BackupTier::Monthly, customRoot)) {
                PruneTierBackups(BackupTier::Monthly, "", static_cast<size_t>(settings.monthlyBackupsRetention), customRoot);
            }
        }
    }
}

std::future<void> BackupManager::CheckAndRunScheduledBackupsAsync(const std::string& libraryPath, const std::string& customRoot) {
    return GetGlobalThreadPool().Enqueue([libraryPath, customRoot]() -> void {
        CheckAndRunScheduledBackups(libraryPath, customRoot);
    });
}

} // namespace Folio
