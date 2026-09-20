#pragma once
/**
 * =========================================================================================
 * @file import_manager.hpp
 * @brief Unified Ingestion & Migration Subsystem Facade for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DESIGN RATIONALE:
 * Central coordination point for importing notebooks, multi-notebook libraries, and external
 * OneNote documents into FolioNote.
 *
 * Coordinates:
 *  1. OneNote Migration (`OneNoteImporter`):
 *     Ingests .one sections, .onetoc2 TOC files, .onepkg CAB archives, and OneNote folders.
 *  2. Standalone Package Ingestion (`PackageImporter`):
 *     Ingests .notebook folders and compressed archives (.folionb.7z, .fnpack, .7z, .zip).
 *  3. Multi-Notebook Library Ingestion (`PackageImporter`):
 *     Extracts and mounts multi-notebook library bundles (.foliolib.7z).
 *  4. External Library Mounting:
 *     Registers external disk directories directly with `LibraryManager`.
 *  5. Asynchronous Worker Thread Execution:
 *     Provides `std::future<...>` overloads dispatched onto `GetGlobalThreadPool()` to ensure
 *     large archive decompressions do not stutter the 120 FPS UI thread.
 */

#include <string>
#include <vector>
#include <memory>
#include <future>
#include "core/document/notebook.hpp"

class Section;
class CanvasPage;

namespace Folio {

class LibraryManager;
class PageRepository;

/**
 * @class ImportManager
 * @brief Unified facade for document and package ingestion into FolioNote.
 */
class ImportManager {
public:
    /**
     * @brief Imports a standalone FolioNote .notebook folder or compressed archive into a library.
     *
     * @param sourcePackagePath Path to .notebook folder or .folionb.7z / .fnpack archive.
     * @param destLibraryPath Target library directory root.
     * @return Path to the imported package directory, or empty string on failure.
     */
    static std::string ImportNotebookPackage(
        const std::string& sourcePackagePath,
        const std::string& destLibraryPath
    );

    /**
     * @brief Imports a multi-notebook library package (.foliolib.7z) into a library directory.
     *
     * @param sourceArchivePath Path to .foliolib.7z archive.
     * @param destLibraryPath Target library directory root.
     * @return List of extracted .notebook package paths.
     */
    static std::vector<std::string> ImportLibraryPackage(
        const std::string& sourceArchivePath,
        const std::string& destLibraryPath
    );

    /**
     * @brief Registers an existing directory as a FolioNote library folder.
     *
     * @param folderPath Path to folder on disk.
     * @param libManager Target LibraryManager.
     * @param customName Optional custom display name for the library.
     * @return True if folder exists and was registered successfully.
     */
    static bool ImportLibraryFolder(
        const std::string& folderPath,
        LibraryManager& libManager,
        const std::string& customName = ""
    );

    /**
     * @brief Asynchronously decompresses and imports a package on a worker thread.
     */
    static std::future<std::string> ImportNotebookPackageAsync(
        const std::string& sourcePackagePath,
        const std::string& destLibraryPath
    );
};

} // namespace Folio
