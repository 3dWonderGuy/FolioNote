#pragma once
/**
 * =========================================================================================
 * @file package_importer.hpp
 * @brief High-Performance Package & Archive Importer for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DESIGN RATIONALE:
 * FolioNote distributes and backs up notebooks and multi-notebook libraries as:
 * 1. Uncompressed directories (`.notebook` packages housing `structure.db` and page blobs).
 * 2. High-compression standalone notebook archives (`.folionb.7z`, `.fnpack`, `.7z`, `.zip`).
 * 3. Complete library archive bundles (`.foliolib.7z`).
 *
 * This subsystem handles:
 * - Detecting archive formats vs. folder structures.
 * - Decompressing packages using multi-threaded 7-Zip CLI (`7z.exe`) with fallback to `tar.exe`.
 * - Validating package integrity (ensuring presence of SQLite database or page folders).
 * - Automated conflict resolution (renaming to `Name (Imported N).notebook` if name exists).
 * - Safe staging and atomic movement into the destination library directory.
 */

#include <string>
#include <vector>
#include <memory>

class Notebook;

namespace Folio {

class LibraryManager;

/**
 * @class PackageImporter
 * @brief Coordinates decompression, validation, and mounting of notebook and library packages.
 */
class PackageImporter {
public:
    /**
     * @brief Checks if a given path corresponds to a supported compressed archive.
     * @param path Filesystem path to inspect.
     * @return True if extension matches .7z, .folionb.7z, .foliolib.7z, .fnpack, or .zip.
     */
    static bool IsArchiveFile(const std::string& path);

    /**
     * @brief Checks if a given archive path represents a multi-notebook library bundle (.foliolib.7z).
     * @param path Filesystem path to inspect.
     * @return True if archive represents a whole library bundle.
     */
    static bool IsLibraryArchive(const std::string& path);

    /**
     * @brief Imports a standalone notebook package (folder or archive) into the target library.
     *
     * Working Process:
     * 1. If @p sourcePath is an uncompressed `.notebook` directory, safely copies it into
     *    @p destinationLibraryPath with collision-free naming.
     * 2. If @p sourcePath is a compressed archive (`.folionb.7z`, `.fnpack`, `.7z`, `.zip`):
     *    a. Extracts to a unique temporary staging directory via 7-Zip / tar.
     *    b. Inspects extracted contents to locate the valid `.notebook` package folder.
     *    c. Moves the package into @p destinationLibraryPath with conflict resolution.
     *    d. Cleans up temporary staging artifacts.
     *
     * @param sourcePath Absolute path to a .notebook folder or compressed archive.
     * @param destinationLibraryPath Target library directory root.
     * @return Absolute path to the imported `.notebook` package on disk, or empty on failure.
     */
    static std::string ImportPackage(
        const std::string& sourcePath,
        const std::string& destinationLibraryPath
    );

    /**
     * @brief Imports a multi-notebook library package (.foliolib.7z), extracting and mounting all notebooks.
     *
     * Working Process:
     * 1. Extracts the archive into a staging area.
     * 2. Iterates over all discovered `.notebook` packages within the archive.
     * 3. Moves each package into @p destinationLibraryPath, resolving collisions.
     * 4. Returns the list of imported package paths.
     *
     * @param sourceArchivePath Path to .foliolib.7z file.
     * @param destinationLibraryPath Target library directory root.
     * @return Vector of paths to all imported `.notebook` packages.
     */
    static std::vector<std::string> ImportLibraryPackage(
        const std::string& sourceArchivePath,
        const std::string& destinationLibraryPath
    );

    /**
     * @brief Decompresses any supported archive file into a target directory.
     *
     * @param archivePath Path to compressed archive.
     * @param targetDirectory Directory where contents will be extracted.
     * @return True if extraction succeeded with exit code 0.
     */
    static bool DecompressArchive(
        const std::string& archivePath,
        const std::string& targetDirectory
    );

    /**
     * @brief Resolves the absolute path to 7-Zip executable (7z.exe) if available on the system.
     * @return Path to 7z.exe or empty string if not installed.
     */
    static std::string Find7ZipExecutable();
};

} // namespace Folio
