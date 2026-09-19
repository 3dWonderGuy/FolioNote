/**
 * =========================================================================================
 * @file library_trash.cpp
 * @brief Recycle bin (.trash/) isolation, restoration, and permanent purge operations
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Handles notebook lifecycle quarantine:
 * 1. Safe Deletion: Moves discarded .notebook packages into their library's hidden `.trash/`
 *    subfolder instead of performing destructive immediate filesystem unlinks.
 * 2. Collision Safety: Automatically disambiguates destination paths inside `.trash/` so
 *    repeated deletions of identically named packages do not overwrite earlier versions.
 * 3. Restoration: Moves packages from `.trash/` back to active library bundles.
 * 4. Permanent Erasure: Purges individual or all trashed packages with strict invariant guards
 *    preventing accidental deletion of non-trashed notebooks.
 */

#include "core/document/library/library.hpp"

#include <string>
#include <vector>

#include "utils/file_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

/**
 * @brief Moves a .notebook package into its library's .trash/ directory for safe recovery.
 *
 * GENERAL WORKING PROCESS & INVARIANTS:
 * 1. Identifies which library bundle owns the target notebook by checking `libraries` or
 *    climbing to the parent path.
 * 2. Creates the hidden `.trash/` subdirectory inside that library root if it does not already exist.
 * 3. Disambiguates destination filename if an identically named notebook was previously trashed.
 * 4. Atomically relocates the package folder via `FileManager::Move`.
 * 5. Refreshes the library catalogs so active views immediately reflect package removal.
 *
 * @param notebookPath Absolute filesystem path to the .notebook directory.
 * @return bool True if successfully moved to trash; false on validation error or I/O failure.
 */
bool LibraryManager::MoveNotebookToTrash(const std::string& notebookPath) {
    if (notebookPath.empty() || !FileManager::IsDirectory(notebookPath)) {
        LOG_ERROR(LibraryManager, "MoveNotebookToTrash rejected: Package not found: " + notebookPath);
        return false;
    }

    // Step 1: Discover owning library
    std::string libRoot;
    for (const auto& lib : libraries) {
        if (lib.ContainsNotebook(notebookPath)) {
            libRoot = lib.rootPath;
            break;
        }
    }
    if (libRoot.empty()) {
        libRoot = FileManager::GetParentPath(notebookPath);
    }
    if (libRoot.empty()) {
        libRoot = defaultLibraryPath;
    }

    // Step 2: Ensure .trash/ directory exists inside the library root
    std::string trashDir = FileManager::JoinPath(libRoot, ".trash");
    if (!FileManager::CreateDirectories(trashDir)) {
        LOG_ERROR(LibraryManager, "MoveNotebookToTrash failed to create trash directory: " + trashDir);
        return false;
    }

    // Step 3: Determine collision-free destination path inside .trash
    std::string nbStem = FileManager::GetStem(notebookPath);
    std::string targetTrashPath = FileManager::DisambiguatePath(trashDir, nbStem, FOLIO_NOTEBOOK_EXTENSION);

    // Step 4: Perform move
    if (!FileManager::Move(notebookPath, targetTrashPath)) {
        LOG_ERROR(LibraryManager, "MoveNotebookToTrash failed to move " + notebookPath + " -> " + targetTrashPath);
        return false;
    }

    LOG_INFO(LibraryManager, "Moved notebook package to library trash: " + targetTrashPath);

    // Step 5: Refresh catalog
    RefreshAll();
    return true;
}

/**
 * @brief Restores a notebook from .trash/ back to an active library directory.
 *
 * GENERAL WORKING PROCESS:
 * 1. Resolves destination library: if targetLibraryPath is unspecified, defaults to the
 *    parent library owning the `.trash/` folder.
 * 2. Generates a collision-safe destination filename in the library root.
 * 3. Relocates package from `.trash/` to the active library via `FileManager::Move`.
 * 4. Refreshes library notebook catalogs.
 *
 * @param trashNotebookPath Absolute path to the trashed .notebook package.
 * @param targetLibraryPath Optional destination library path (defaults to owning library).
 * @return bool True if successfully restored; false on failure.
 */
bool LibraryManager::RestoreNotebookFromTrash(const std::string& trashNotebookPath, const std::string& targetLibraryPath) {
    if (trashNotebookPath.empty() || !FileManager::IsDirectory(trashNotebookPath)) {
        LOG_ERROR(LibraryManager, "RestoreNotebookFromTrash rejected: Trashed notebook not found: " + trashNotebookPath);
        return false;
    }

    std::string destLib = targetLibraryPath;
    if (destLib.empty()) {
        // Parent of .trash is the library directory
        std::string trashDir = FileManager::GetParentPath(trashNotebookPath);
        destLib = FileManager::GetParentPath(trashDir);
    }
    if (destLib.empty() || !FileManager::IsDirectory(destLib)) {
        destLib = defaultLibraryPath;
    }

    std::string nbStem = FileManager::GetStem(trashNotebookPath);
    std::string destPath = FileManager::DisambiguatePath(destLib, nbStem, FOLIO_NOTEBOOK_EXTENSION);

    if (!FileManager::Move(trashNotebookPath, destPath)) {
        LOG_ERROR(LibraryManager, "RestoreNotebookFromTrash failed to move " + trashNotebookPath + " -> " + destPath);
        return false;
    }

    LOG_INFO(LibraryManager, "Restored notebook from trash to active library: " + destPath);
    RefreshAll();
    return true;
}

/**
 * @brief Discovers all trashed .notebook folders inside a library's .trash/ subfolder.
 *
 * @param libraryPath Absolute path to the library bundle.
 * @return std::vector<std::string> List of absolute paths to discovered trashed packages.
 */
std::vector<std::string> LibraryManager::GetTrashNotebooks(const std::string& libraryPath) const {
    std::vector<std::string> result;
    std::string trashDir = FileManager::JoinPath(libraryPath, ".trash");
    if (!FileManager::IsDirectory(trashDir)) {
        return result;
    }

    auto entries = FileManager::ListEntries(trashDir, FOLIO_NOTEBOOK_EXTENSION);
    for (const auto& entry : entries) {
        if (entry.type == FileType::Directory) {
            result.push_back(entry.fullPath);
        }
    }
    return result;
}

/**
 * @brief Permanently purges a trashed .notebook package from disk.
 *
 * SAFETY INVARIANT:
 * Explicitly verifies that the target path contains `.trash` to guarantee
 * that an active, non-trashed notebook package is NEVER accidentally destroyed.
 *
 * @param trashNotebookPath Absolute path to the trashed notebook directory.
 * @return bool True if deleted permanently; false on validation error or I/O failure.
 */
bool LibraryManager::PermanentlyDeleteNotebookFromTrash(const std::string& trashNotebookPath) {
    if (trashNotebookPath.empty() || !FileManager::IsDirectory(trashNotebookPath)) {
        return false;
    }

    // Safety guard: ensure the folder resides inside a .trash directory
    std::string normalized = FileManager::NormalizeSeparators(trashNotebookPath);
    if (normalized.find("/.trash/") == std::string::npos && normalized.find("\\.trash\\") == std::string::npos) {
        LOG_ERROR(LibraryManager, "PermanentlyDeleteNotebookFromTrash rejected: Path is not inside a .trash directory: " + trashNotebookPath);
        return false;
    }

    bool success = FileManager::RemoveDirectoryRecursive(trashNotebookPath);
    if (success) {
        LOG_INFO(LibraryManager, "Permanently purged notebook from trash: " + trashNotebookPath);
    } else {
        LOG_ERROR(LibraryManager, "Failed to permanently purge notebook from trash: " + trashNotebookPath);
    }
    return success;
}

/**
 * @brief Permanently purges all contents of a library's .trash/ directory.
 *
 * @param libraryPath Absolute path to the library bundle.
 * @return size_t Count of trashed notebooks permanently erased.
 */
size_t LibraryManager::EmptyLibraryTrash(const std::string& libraryPath) {
    auto trashed = GetTrashNotebooks(libraryPath);
    size_t count = 0;
    for (const auto& path : trashed) {
        if (PermanentlyDeleteNotebookFromTrash(path)) {
            count++;
        }
    }
    LOG_INFO(LibraryManager, "Emptied library trash for " + libraryPath + ". Erased " + std::to_string(count) + " notebooks.");
    return count;
}

} // namespace Folio
