/**
 * =========================================================================================
 * @file import_manager.cpp
 * @brief Implementation of Unified Ingestion & Migration Subsystem Facade for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Delegation & Separation of Concerns:
 *    Routes OneNote requests to `OneNoteImporter`, standalone/archive package imports to
 *    `PackageImporter`, and library registrations to `LibraryManager`.
 * 2. SQLite WAL & Packaging Pipeline:
 *    When OneNote documents are converted in-memory, `ImportManager` creates the target
 *    `.notebook` directory on disk and invokes `PageRepository::SaveNotebookAsync` to
 *    persist the hierarchy into `structure.db`.
 * 3. Threadpool Offloading:
 *    Async variants queue jobs onto `GetGlobalThreadPool()` to avoid blocking the main UI loop.
 */

#include "core/import/import_manager.hpp"
#include "core/import/package_importer.hpp"
#include "core/document/notebook.hpp"
#include "core/document/library/library.hpp"
#include "core/storage/page_repository.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"
#include "utils/thread_pool.hpp"

#include <filesystem>

namespace Folio {

std::string ImportManager::ImportNotebookPackage(
    const std::string& sourcePackagePath,
    const std::string& destLibraryPath
) {
    return PackageImporter::ImportPackage(sourcePackagePath, destLibraryPath);
}

std::vector<std::string> ImportManager::ImportLibraryPackage(
    const std::string& sourceArchivePath,
    const std::string& destLibraryPath
) {
    return PackageImporter::ImportLibraryPackage(sourceArchivePath, destLibraryPath);
}

bool ImportManager::ImportLibraryFolder(
    const std::string& folderPath,
    LibraryManager& libManager,
    const std::string& customName
) {
    std::error_code ec;
    if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
        LOG_ERROR(FileManager, "ImportManager::ImportLibraryFolder: Invalid directory: " + folderPath);
        return false;
    }

    std::string libName = customName.empty() ? std::filesystem::path(folderPath).filename().string() : customName;
    libManager.AddLibrary(libName, folderPath);
    LOG_INFO(FileManager, "ImportManager: Mounted external library directory: " + folderPath);
    return true;
}

std::future<std::string> ImportManager::ImportNotebookPackageAsync(
    const std::string& sourcePackagePath,
    const std::string& destLibraryPath
) {
    return GetGlobalThreadPool().Enqueue([sourcePackagePath, destLibraryPath]() -> std::string {
        return ImportNotebookPackage(sourcePackagePath, destLibraryPath);
    });
}

} // namespace Folio
