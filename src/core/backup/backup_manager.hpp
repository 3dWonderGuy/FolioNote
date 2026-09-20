#pragma once
/**
 * =========================================================================================
 * @file backup_manager.hpp
 * @brief Autonomous 3-Tier Multi-Ring Backup & Disaster Recovery Engine
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * BackupManager is the dedicated subsystem responsible for offline snapshots, archival,
 * and disaster recovery of FolioNote data:
 * 1. 3-Tier Grandfather-Father-Son Retention:
 *    - Tier 1 (Daily / Post-Session): 24h interval, rolling N (default: 7 copies).
 *    - Tier 2 (Weekly): 7d interval, rolling N (default: 4 copies).
 *    - Tier 3 (Monthly): 30d interval, rolling N (default: 12 copies).
 *    - Manual: User on-demand snapshots.
 * 2. Deep Filesystem Isolation:
 *    Snapshots are organized deep in `<AppRoot>/backups/tiers/{daily,weekly,monthly}/`.
 * 3. Safe SQLite WAL Checkpointing:
 *    Flushes and truncates SQLite WAL journals (`PRAGMA wal_checkpoint(TRUNCATE)`) prior
 *    to disk copying to eliminate Windows file sharing violations.
 * 4. Non-Blocking ThreadPool Execution:
 *    All file tree duplications are dispatched to background worker threads.
 */

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <cstdint>

namespace Folio {

/**
 * @enum BackupTier
 * @brief Grandfather-Father-Son multi-ring backup tiers.
 */
enum class BackupTier {
    Daily,    ///< 24-hour interval; retains rolling N copies (default: 7)
    Weekly,   ///< 7-day interval; retains rolling N copies (default: 4)
    Monthly,  ///< 30-day interval; retains rolling N copies (default: 12)
    Manual    ///< On-demand user snapshot
};

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
    BackupTier tier = BackupTier::Manual; ///< Retention tier category
};

/**
 * @class BackupManager
 * @brief High-level coordinator for creating, listing, restoring, and pruning backups.
 */
class BackupManager {
public:
    /**
     * @brief Converts a BackupTier enum to its string representation ("daily", "weekly", "monthly", "manual").
     */
    static std::string TierToString(BackupTier tier);

    /**
     * @brief Parses a string into a BackupTier enum.
     */
    static BackupTier StringToTier(const std::string& str);

    /**
     * @brief Resolves the deep isolated filesystem directory for a backup tier: `<BackupsRoot>/tiers/<tier>`.
     */
    static std::string GetTierDirectory(BackupTier tier, const std::string& customRoot = "");

    /**
     * @brief Flushes and truncates any SQLite WAL buffers inside `rootDirectory` before backup copying.
     * @param rootDirectory Path to library or notebook directory to scan for .db files.
     * @return true if all detected SQLite databases were successfully checkpointed.
     */
    static bool FlushSqliteWal(const std::string& rootDirectory);

    /**
     * @brief Creates a synchronous snapshot of a library folder.
     */
    static bool BackupLibrary(const std::string& libraryPath, const std::string& customDestination = "");

    /**
     * @brief Asynchronously creates a full snapshot of a library folder on the background ThreadPool.
     */
    static std::future<bool> BackupLibraryAsync(const std::string& libraryPath, const std::string& customDestination = "");

    /**
     * @brief Creates a synchronous snapshot of a library folder placed in a specific retention tier.
     */
    static bool BackupLibraryTiered(const std::string& libraryPath, BackupTier tier, const std::string& customRoot = "");

    /**
     * @brief Asynchronously creates a tiered library snapshot on the background ThreadPool.
     */
    static std::future<bool> BackupLibraryTieredAsync(const std::string& libraryPath, BackupTier tier, const std::string& customRoot = "");

    /**
     * @brief Creates a synchronous snapshot of an individual notebook directory.
     */
    static bool BackupNotebook(const std::string& notebookPath, const std::string& customDestination = "");

    /**
     * @brief Asynchronously creates a snapshot of a notebook directory on the background ThreadPool.
     */
    static std::future<bool> BackupNotebookAsync(const std::string& notebookPath, const std::string& customDestination = "");

    /**
     * @brief Creates a synchronous snapshot of an individual notebook placed in a specific retention tier.
     */
    static bool BackupNotebookTiered(const std::string& notebookPath, BackupTier tier, const std::string& customRoot = "");

    /**
     * @brief Asynchronously creates a tiered notebook snapshot on the background ThreadPool.
     */
    static std::future<bool> BackupNotebookTieredAsync(const std::string& notebookPath, BackupTier tier, const std::string& customRoot = "");

    /**
     * @brief Discovers and catalogs all available backup snapshots in the specified directory.
     */
    static std::vector<BackupInfo> ListBackups(const std::string& backupRootDir = "");

    /**
     * @brief Discovers and catalogs backups for a specific tier.
     */
    static std::vector<BackupInfo> ListTierBackups(BackupTier tier, const std::string& customRoot = "");

    /**
     * @brief Restores an existing backup snapshot to a target directory.
     */
    static bool RestoreBackup(const std::string& backupSnapshotPath, const std::string& targetDestinationDir);

    /**
     * @brief Asynchronously restores a backup snapshot on the background ThreadPool.
     */
    static std::future<bool> RestoreBackupAsync(const std::string& backupSnapshotPath, const std::string& targetDestinationDir);

    /**
     * @brief Deletes a specific backup snapshot directory from disk.
     */
    static bool DeleteBackup(const std::string& backupSnapshotPath);

    /**
     * @brief Rotates and prunes older backup snapshots to enforce storage retention limits.
     */
    static size_t PruneOldBackups(const std::string& sourceName, size_t maxToKeep = 5, const std::string& backupRootDir = "");

    /**
     * @brief Prunes snapshots in a specific tier according to its retention limit.
     */
    static size_t PruneTierBackups(BackupTier tier, const std::string& sourceName = "", size_t maxToKeep = 0, const std::string& customRoot = "");

    /**
     * @brief Evaluates elapsed timestamps and runs any due scheduled multi-ring backups (Daily, Weekly, Monthly).
     *
     * MATHEMATICAL SCHEDULING LOGIC:
     * - Evaluates $\Delta t_{\text{daily}} \ge 24\text{h}$, $\Delta t_{\text{weekly}} \ge 7\text{d}$, $\Delta t_{\text{monthly}} \ge 30\text{d}$.
     * - Dispatches snapshots and prunes obsolete snapshots per configured tier retention caps.
     *
     * @param libraryPath Root path of the active library to back up.
     * @param customRoot Optional backups storage root (defaults to `<AppRoot>/backups`).
     */
    static void CheckAndRunScheduledBackups(const std::string& libraryPath, const std::string& customRoot = "");

    /**
     * @brief Asynchronously executes the scheduled backup evaluation on the background ThreadPool.
     */
    static std::future<void> CheckAndRunScheduledBackupsAsync(const std::string& libraryPath, const std::string& customRoot = "");
};

} // namespace Folio
