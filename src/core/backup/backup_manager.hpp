#pragma once
/**
 * =========================================================================================
 * @file backup_manager.hpp
 * @brief Autonomous Backup & Disaster Recovery Engine for Libraries and Notebooks
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * BackupManager is the dedicated subsystem responsible for offline snapshots, archival,
 * and disaster recovery of FolioNote data:
 * 1. Full Library Snapshots: Captures all constituent notebooks, section hierarchies,
 *    SQLite structure databases, and binary .ink vector files.
 * 2. Standalone Notebook Snapshots: Backs up individual notebooks independently.
 * 3. Atomic Staging & Integrity: Writes metadata markers (backup.meta) containing SHA/size
 *    telemetry, timestamps, and schema version to verify backup health.
 * 4. Non-Blocking ThreadPool Execution: All intensive file tree duplications are offloaded
 *    to background worker threads, ensuring the 120 FPS inking canvas never hitches.
 * 5. Automated Retention Pruning: Rotates snapshots according to configurable retention
 *    policies (e.g. keep last N snapshots), preventing unbounded disk consumption.
 */

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <cstdint>

namespace Folio {

/**
 * @struct BackupInfo
 * @brief Telemetry and filesystem metadata describing an existing backup snapshot.
 */
struct BackupInfo {
    std::string backupId;            ///< Unique identifier (e.g., "backup_1726800000")
    std::string sourceName;          ///< User-visible display name (e.g. "Default", "Physics")
    std::string sourcePath;          ///< Original filesystem path where the library/notebook was backed up from
    std::string backupPath;          ///< Destination directory containing the snapshot
    std::string timestampStr;        ///< Human-readable timestamp (e.g. "2026-09-20 01:30:00")
    uint64_t timestamp = 0;          ///< Epoch millisecond timestamp
    uint64_t totalBytes = 0;         ///< Total byte size of the backup folder on disk
    bool isLibrary = false;          ///< True if snapshot represents an entire library; false if individual notebook
    size_t notebookCount = 0;        ///< Number of notebooks contained inside the snapshot
};

/**
 * @class BackupManager
 * @brief High-level coordinator for creating, listing, restoring, and pruning backups.
 */
class BackupManager {
public:
    /**
     * @brief Creates a synchronous, full snapshot of a library folder.
     *
     * GENERAL WORKING PROCESS:
     * 1. Validates that `libraryPath` exists and is a valid directory.
     * 2. Resolves destination root: uses `customDestination` if provided; otherwise
     *    defaults to `FileManager::GetBackupsDirectory()`.
     * 3. Constructs a timestamped snapshot directory: `<Dest>/<LibName>_Backup_<YYYYMMDD_HHMMSS>/`.
     * 4. Copies the directory tree recursively with full Unicode path fidelity.
     * 5. Generates and writes `backup.meta` inside the snapshot folder with source telemetry.
     *
     * @param libraryPath Absolute path to the source library directory on disk.
     * @param customDestination Optional target directory (defaults to `<AppRoot>/backups`).
     * @return true if snapshot was successfully completed and validated; false otherwise.
     */
    static bool BackupLibrary(const std::string& libraryPath, const std::string& customDestination = "");

    /**
     * @brief Asynchronously creates a full snapshot of a library folder on the background ThreadPool.
     *
     * Guarantees zero UI/inking thread interruptions during heavy disk I/O.
     *
     * @param libraryPath Absolute path to the source library directory on disk.
     * @param customDestination Optional target directory (defaults to `<AppRoot>/backups`).
     * @return std::future<bool> Future resolving to true on success.
     */
    static std::future<bool> BackupLibraryAsync(const std::string& libraryPath, const std::string& customDestination = "");

    /**
     * @brief Creates a synchronous, full snapshot of an individual notebook directory.
     *
     * Copies `structure.db`, `pages/` (.ink files), and `imports/` to a timestamped backup directory.
     *
     * @param notebookPath Absolute path to the .notebook directory.
     * @param customDestination Optional target directory (defaults to `<AppRoot>/backups`).
     * @return true if backup succeeded; false otherwise.
     */
    static bool BackupNotebook(const std::string& notebookPath, const std::string& customDestination = "");

    /**
     * @brief Asynchronously creates a full snapshot of a notebook directory on the background ThreadPool.
     *
     * @param notebookPath Absolute path to the .notebook directory.
     * @param customDestination Optional target directory (defaults to `<AppRoot>/backups`).
     * @return std::future<bool> Future resolving to true on success.
     */
    static std::future<bool> BackupNotebookAsync(const std::string& notebookPath, const std::string& customDestination = "");

    /**
     * @brief Discovers and catalogs all available backup snapshots in the backups directory.
     *
     * Parses `backup.meta` markers inside each snapshot directory.
     *
     * @param backupRootDir Directory to scan (defaults to `FileManager::GetBackupsDirectory()`).
     * @return Vector of BackupInfo entries sorted by timestamp descending (newest first).
     */
    static std::vector<BackupInfo> ListBackups(const std::string& backupRootDir = "");

    /**
     * @brief Restores an existing backup snapshot to a target directory.
     *
     * WORKING PROCESS:
     * 1. Validates `backupSnapshotPath` and verifies the presence of `backup.meta`.
     * 2. Cleans/prepares the `targetDestinationDir`.
     * 3. Recursively copies the snapshot contents (omitting `backup.meta`) to the destination.
     * 4. Verifies disk integrity.
     *
     * @param backupSnapshotPath Absolute path to the snapshot directory to restore.
     * @param targetDestinationDir Target location where the restored library/notebook will reside.
     * @return true if restore completed successfully; false otherwise.
     */
    static bool RestoreBackup(const std::string& backupSnapshotPath, const std::string& targetDestinationDir);

    /**
     * @brief Asynchronously restores a backup snapshot on the background ThreadPool.
     *
     * @param backupSnapshotPath Absolute path to the snapshot directory.
     * @param targetDestinationDir Target restore path.
     * @return std::future<bool> Future resolving to true on success.
     */
    static std::future<bool> RestoreBackupAsync(const std::string& backupSnapshotPath, const std::string& targetDestinationDir);

    /**
     * @brief Deletes a specific backup snapshot directory from disk.
     * @param backupSnapshotPath Absolute path to the snapshot directory to delete.
     * @return true if deleted; false otherwise.
     */
    static bool DeleteBackup(const std::string& backupSnapshotPath);

    /**
     * @brief Rotates and prunes older backup snapshots to enforce storage retention limits.
     *
     * MATHEMATICAL RETENTION LOGIC:
     * For a given source entity (e.g. "Default Library"), queries all matching snapshots
     * and sorts by `timestamp` descending. If total count $C > \text{maxToKeep}$,
     * deletes the oldest $C - \text{maxToKeep}$ snapshots from disk.
     *
     * @param sourceName User-visible source name to prune (e.g. "Default Library"), or empty for all.
     * @param maxToKeep Maximum number of newest snapshots to retain (default: 5).
     * @param backupRootDir Directory containing snapshots (defaults to `<AppRoot>/backups`).
     * @return Number of pruned/deleted backup directories.
     */
    static size_t PruneOldBackups(const std::string& sourceName, size_t maxToKeep = 5, const std::string& backupRootDir = "");
};

} // namespace Folio
