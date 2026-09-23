/**
 * =========================================================================================
 * @file notebook_cloner.cpp
 * @brief High-Fidelity Package Container Initializer and Pure Filesystem Replicator
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DESIGN RATIONALE:
 * In FolioNote, a .notebook package is a composite bundle comprising:
 * 1. An SQLite database (structure.db) housing notebook metadata, section
 *    hierarchies, canvas page geometries, and search indexes.
 * 2. A `pages/` subdirectory housing individual `.ink` binary vector stroke files.
 * 3. An `imports/` subdirectory housing referenced PDFs and image assets.
 *
 * PURE FILESYSTEM CLONING:
 * To maintain 100% vector fidelity and zero-cost duplication without loading megabytes of
 * stroke points into RAM, `NotebookCloner::CloneNotebook` executes a pure recursive filesystem
 * copy using `Folio::FileManager::CopyDirectoryRecursive`.
 *
 * ZERO DATABASE COUPLING:
 * This subsystem operates purely on filesystem containers and paths. It does NOT include
 * SQLite or `PageRepository`. Package database schema creation is deferred to the active
 * `Workspace` session (`workspace.repository.OpenNotebookPackage`).
 */

#include "core/document/library/library.hpp"

#include <string>
#include <memory>

#include "io/file_manager.hpp"
#include "io/package_marker.hpp"
#include "utils/logger.hpp"

namespace Folio {

/**
 * @brief Synchronously initializes a physical .notebook package directory container on disk.
 *
 * GENERAL WORKING PROCESS & INVARIANTS:
 * 1. Validates that the notebook has a non-empty `filePath`.
 * 2. Creates the parent package directory and the child `pages/` folder via `FileManager`.
 * 3. Leaves database creation and active document management to the `Workspace` layer.
 *
 * @param notebook Shared pointer to the in-memory Notebook model.
 * @return bool True if the package directories were successfully created on disk; false otherwise.
 */
bool NotebookCloner::InitializePackage(const std::shared_ptr<Notebook>& notebook) {
    if (!notebook || notebook->filePath.empty()) {
        LOG_ERROR(LibraryManager, "NotebookCloner::InitializePackage rejected: Null notebook or empty filePath.");
        return false;
    }

    // Create package folder and pages subdirectory
    if (!FileManager::CreateDirectories(notebook->filePath)) {
        LOG_ERROR(LibraryManager, "NotebookCloner failed to create package directory: " + notebook->filePath);
        return false;
    }

    std::string pagesDir = FileManager::JoinPath(notebook->filePath, "pages");
    if (!FileManager::CreateDirectories(pagesDir)) {
        LOG_ERROR(LibraryManager, "NotebookCloner failed to create pages directory: " + pagesDir);
        return false;
    }

    LOG_INFO(LibraryManager, "NotebookCloner: Initialized package container at: " + notebook->filePath);

    // Apply Windows Shell package identity (desktop.ini)
    PackageMarker::MarkFolderAsPackage(notebook->filePath, "FolioNote Notebook Package");
    return true;
}

/**
 * @brief Duplicates an existing notebook package, ensuring fresh unique GUIDs across
 * the entire document hierarchy while preserving 100% binary vector stroke fidelity.
 *
 * GENERAL WORKING PROCESS & UNIQUE UID REMAPPING ALGORITHM:
 * 1. Resolves destination library root directory (falling back to source parent or default).
 * 2. Formats a collision-free package path inside destination using `FileManager::DisambiguatePath`.
 * 3. Creates the physical package folder (`<pkgPath>/`) and `<pkgPath>/pages/`.
 * 4. Copies imported assets (`<pkgPath>/imports/`) recursively if present.
 * 5. Deep-clones the in-memory Notebook model via `sourceNb->Clone(safeName)`:
 *    - New UUID v4 assigned to Notebook.
 *    - New UUID v4 assigned to each SectionGroup (and recursively to sub-groups).
 *    - New UUID v4 assigned to each Section.
 *    - New UUID v4 assigned to each CanvasPage.
 * 6. Collects all pages from both source and cloned models via `GetAllPages(true)`:
 *    Because the clone traverses sections and groups in identical deterministic order,
 *    `srcPages[i]` maps 1:1 to `dstPages[i]`.
 * 7. For each page pair, copies `<srcPkg>/pages/{srcPages[i]->guid}.ink` directly to
 *    `<dstPkg>/pages/{dstPages[i]->guid}.ink`.
 *    - 100% vector fidelity (raw binary preserved byte-for-byte).
 *    - Zero RAM overhead (no deserializing or re-compressing strokes).
 *    - Crucially: On disk, every `.ink` file is indexed by its BRAND NEW UNIQUE GUID!
 * 8. SQLite schema and metadata persistence is deferred to `Workspace::OpenAndActivateNotebook`
 *    which writes clean tables populated with the new unique GUIDs.
 *
 * @param sourceNb Shared pointer to the source notebook being duplicated.
 * @param newName Proposed title for the duplicate notebook (e.g. "Calculus - Final").
 * @param targetLibraryPath Path to the target library folder (empty for default or source parent).
 * @param fallbackLibraryPath Fallback path if targetLibraryPath is invalid.
 * @return Shared pointer to the newly cloned Notebook object, or nullptr on failure.
 */
std::shared_ptr<Notebook> NotebookCloner::CloneNotebook(
    const std::shared_ptr<Notebook>& sourceNb,
    const std::string& newName,
    const std::string& targetLibraryPath,
    const std::string& fallbackLibraryPath
) {
    if (!sourceNb || sourceNb->filePath.empty()) {
        LOG_ERROR(LibraryManager, "NotebookCloner::CloneNotebook rejected: Source notebook pointer is null or has empty filePath.");
        return nullptr;
    }

    // Step 1: Resolve destination library directory
    std::string destLib = targetLibraryPath;
    if (destLib.empty() || !LibraryManager::IsLibraryFolder(destLib)) {
        std::string srcParent = FileManager::GetParentPath(sourceNb->filePath);
        if (LibraryManager::IsLibraryFolder(srcParent)) {
            destLib = srcParent;
        } else {
            destLib = fallbackLibraryPath;
        }
    }

    FileManager::CreateDirectories(destLib);

    // Step 2: Determine collision-free destination package path
    std::string safeName = newName.empty() ? (sourceNb->name + " - Copy") : newName;
    std::string newPkgPath = FileManager::DisambiguatePath(destLib, safeName, FOLIO_NOTEBOOK_EXTENSION);

    // Step 3: Initialize physical package directory structure
    if (!FileManager::CreateDirectories(newPkgPath)) {
        LOG_ERROR(LibraryManager, "NotebookCloner failed to create package directory: " + newPkgPath);
        return nullptr;
    }

    std::string dstPagesDir = FileManager::JoinPath(newPkgPath, "pages");
    if (!FileManager::CreateDirectories(dstPagesDir)) {
        LOG_ERROR(LibraryManager, "NotebookCloner failed to create pages directory: " + dstPagesDir);
        return nullptr;
    }

    // Step 4: Copy imported assets (PDFs, images) if present
    std::string srcImportsDir = FileManager::JoinPath(sourceNb->filePath, "imports");
    if (FileManager::IsDirectory(srcImportsDir)) {
        std::string dstImportsDir = FileManager::JoinPath(newPkgPath, "imports");
        FileManager::CopyDirectoryRecursive(srcImportsDir, dstImportsDir);
    }

    // Step 5: Deep-clone in-memory model generating fresh unique UUIDs across all tiers
    auto copyNb = sourceNb->Clone(safeName);
    copyNb->filePath = newPkgPath;

    // Step 6: Map source pages to cloned pages and copy raw .ink files under the new unique GUIDs
    std::string srcPagesDir = FileManager::JoinPath(sourceNb->filePath, "pages");
    if (FileManager::IsDirectory(srcPagesDir)) {
        auto srcPages = sourceNb->GetAllPages(true);
        auto dstPages = copyNb->GetAllPages(true);

        if (srcPages.size() == dstPages.size()) {
            size_t copiedInkCount = 0;
            for (size_t i = 0; i < srcPages.size(); ++i) {
                if (!srcPages[i] || !dstPages[i]) continue;
                std::string srcInk = FileManager::JoinPath(srcPagesDir, srcPages[i]->guid + ".ink");
                std::string dstInk = FileManager::JoinPath(dstPagesDir, dstPages[i]->guid + ".ink");
                if (FileManager::IsRegularFile(srcInk)) {
                    if (FileManager::CopySingleFile(srcInk, dstInk, true)) {
                        copiedInkCount++;
                    }
                }
            }
            LOG_INFO(LibraryManager, "NotebookCloner: Re-mapped and copied " + std::to_string(copiedInkCount) +
                     " .ink files with unique GUIDs to: " + dstPagesDir);
        } else {
            LOG_WARN(LibraryManager, "NotebookCloner: Page count mismatch between source (" +
                     std::to_string(srcPages.size()) + ") and clone (" +
                     std::to_string(dstPages.size()) + ")");
        }
    }

    LOG_INFO(LibraryManager, "NotebookCloner: Successfully cloned notebook package with unique GUIDs to: " + newPkgPath);

    // Apply Windows Shell package identity (desktop.ini)
    PackageMarker::MarkFolderAsPackage(newPkgPath, "FolioNote Notebook Package");
    return copyNb;
}

/**
 * @brief Directly copies raw `.ink` stroke binary files between package `pages/` directories.
 *
 * @param srcPackagePath Absolute filesystem path to source .notebook package directory.
 * @param dstPackagePath Absolute filesystem path to destination .notebook package directory.
 * @return size_t Count of `.ink` page stroke files copied.
 */
size_t NotebookCloner::CopyPageInkFiles(
    const std::string& srcPackagePath,
    const std::string& dstPackagePath
) {
    size_t count = 0;
    std::string srcPagesDir = FileManager::JoinPath(srcPackagePath, "pages");
    std::string dstPagesDir = FileManager::JoinPath(dstPackagePath, "pages");

    if (FileManager::IsDirectory(srcPagesDir)) {
        FileManager::CreateDirectories(dstPagesDir);
        auto pageEntries = FileManager::ListEntries(srcPagesDir, ".ink");
        for (const auto& entry : pageEntries) {
            std::string dstInk = FileManager::JoinPath(dstPagesDir, entry.fileName);
            if (FileManager::CopySingleFile(entry.fullPath, dstInk, true)) {
                count++;
            }
        }
    }
    return count;
}

} // namespace Folio
