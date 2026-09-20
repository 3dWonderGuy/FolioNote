#pragma once
/**
 * =========================================================================================
 * @file page_version_manager.hpp
 * @brief Google Docs-Style Page Revision History & Disaster Rollback Engine
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * PageVersionManager provides point-in-time version control for individual CanvasPages:
 * 1. Automatic Session Checkpoints: Captures snapshot when interacting with a page during a session.
 * 2. Named Milestone Versions: Users can pin milestones (e.g., "Draft 1", "Pre-Exam Review").
 *    Named versions are permanent and immune to automatic pruning.
 * 3. Rolling Retention Pruning: Unnamed session snapshots are automatically capped to N
 *    (configurable in SettingsManager, e.g. 15, or 0 for unlimited).
 * 4. Non-Destructive Rollback: Reverting to an earlier version automatically creates a safety
 *    snapshot of the active page first so uncommitted work is never lost.
 * 5. Isolated Storage: Revisions reside in `<notebook>/revisions/<pageGuid>/<versionId>.ink`
 *    with accompanying `<versionId>.meta` telemetry, preserving notebook portability.
 */

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declaration of global CanvasPage class
class CanvasPage;

namespace Folio {

/**
 * @struct PageRevisionInfo
 * @brief Telemetry and metadata describing a historical page revision snapshot.
 */
struct PageRevisionInfo {
    std::string versionId;       ///< Unique identifier (e.g., "rev_1726800000000")
    std::string pageGuid;        ///< GUID of the parent page
    std::string versionName;     ///< User-defined name or auto session label (e.g. "Draft 1")
    std::string timestampStr;    ///< Human-readable timestamp ("2026-09-20 03:45:00")
    uint64_t timestamp = 0;      ///< Epoch millisecond timestamp
    uint32_t strokeCount = 0;    ///< Total vector ink strokes in this revision
    uint32_t objectCount = 0;    ///< Total canvas objects in this revision
    uint64_t fileSizeBytes = 0;  ///< Compressed .ink BLOB size on disk in bytes
    bool isNamed = false;        ///< If true, pinned milestone version immune to auto-pruning
};

/**
 * @class PageVersionManager
 * @brief Coordinator for creating, listing, restoring, and pruning page revision history.
 */
class PageVersionManager {
public:
    /**
     * @brief Creates a new revision snapshot for the given page.
     *
     * @param page Reference to the CanvasPage to capture.
     * @param notebookPath Absolute filesystem path to the containing .notebook bundle.
     * @param versionName Optional descriptive label (e.g. "Pre-Exam Review").
     * @param isNamed If true, marks as a permanent user-named milestone that is never auto-pruned.
     * @param outInfo Optional output pointer receiving the generated revision metadata.
     * @return true if successfully serialized and written to disk; false otherwise.
     */
    static bool CreateRevision(const CanvasPage& page, const std::string& notebookPath,
                               const std::string& versionName = "", bool isNamed = false,
                               PageRevisionInfo* outInfo = nullptr);

    /**
     * @brief Discovers and lists all revisions for a page, sorted newest first.
     *
     * @param notebookPath Absolute filesystem path to the containing .notebook bundle.
     * @param pageGuid GUID of the page to query.
     * @return Vector of PageRevisionInfo records sorted by timestamp descending.
     */
    static std::vector<PageRevisionInfo> ListRevisions(const std::string& notebookPath, const std::string& pageGuid);

    /**
     * @brief Restores a page to a previous revision state.
     *
     * @param targetPage In-memory page instance to overwrite with revision contents.
     * @param notebookPath Absolute filesystem path to the containing .notebook bundle.
     * @param versionId Unique identifier of the revision to restore.
     * @param createSafetySnapshot If true, automatically captures a safety snapshot of current page state first.
     * @return true if successfully restored; false otherwise.
     */
    static bool RestoreRevision(CanvasPage& targetPage, const std::string& notebookPath,
                                const std::string& versionId, bool createSafetySnapshot = true);

    /**
     * @brief Renames an existing revision and marks it as a named (pinned) milestone.
     *
     * @param notebookPath Absolute filesystem path to the containing .notebook bundle.
     * @param pageGuid GUID of the page.
     * @param versionId Unique identifier of the revision to update.
     * @param newName New display title for this revision milestone.
     * @return true on success; false otherwise.
     */
    static bool RenameRevision(const std::string& notebookPath, const std::string& pageGuid,
                               const std::string& versionId, const std::string& newName);

    /**
     * @brief Permanently removes a specific revision from disk.
     *
     * @param notebookPath Absolute filesystem path to the containing .notebook bundle.
     * @param pageGuid GUID of the page.
     * @param versionId Unique identifier of the revision to delete.
     * @return true if deleted; false otherwise.
     */
    static bool DeleteRevision(const std::string& notebookPath, const std::string& pageGuid,
                               const std::string& versionId);

    /**
     * @brief Prunes older unnamed revisions according to retention limit (0 = unlimited).
     *
     * Named milestone versions (isNamed == true) are NEVER pruned.
     *
     * @param notebookPath Absolute filesystem path to the containing .notebook bundle.
     * @param pageGuid GUID of the page.
     * @param maxUnnamedToKeep Maximum number of newest unnamed revisions to retain (0 = keep all).
     * @return Number of pruned/deleted revisions.
     */
    static size_t PruneRevisions(const std::string& notebookPath, const std::string& pageGuid,
                                 size_t maxUnnamedToKeep = 15);

    /**
     * @brief Resolves the revisions directory for a given page: `<notebookPath>/revisions/<pageGuid>`.
     */
    static std::string GetRevisionsDirectory(const std::string& notebookPath, const std::string& pageGuid);
};

} // namespace Folio
