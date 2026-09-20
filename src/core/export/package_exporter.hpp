#pragma once
/**
 * =========================================================================================
 * @file package_exporter.hpp
 * @brief High-Ratio 7-Zip Archival Exporter for Libraries and Standalone Notebooks
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Compresses notebooks and entire libraries into single-file portable archives:
 * 1. High-Ratio 7-Zip Integration: Prioritizes `7z.exe` (-t7z, -mx7, LZMA2 multi-threading)
 *    for maximum compression ratio and throughput.
 * 2. System Tar Fallback: Automatically falls back to `C:\Windows\system32\tar.exe` (-czf)
 *    if 7-Zip is not installed on the host system.
 * 3. Safe SQLite WAL Checkpoint: Automatically flushes and truncates SQLite write-ahead logs
 *    before packaging to prevent Windows file sharing violations (`ERROR_SHARING_VIOLATION`).
 */

#include <string>
#include <vector>

namespace Folio {

class PackageExporter {
public:
    /**
     * @brief Resolves the executable path to 7z.exe (or empty string if not installed).
     */
    static std::string Find7ZipExecutable();

    /**
     * @brief Exports a standalone .notebook directory into a compressed package archive (.folionb / .7z).
     *
     * @param notebookPath Absolute path to the source .notebook directory.
     * @param destinationArchive Target archive file path (e.g. "exports/Physics_Notes.folionb").
     * @param compressionLevel 7-Zip compression level from 1 (fastest) to 9 (ultra).
     * @return true if successfully compressed; false otherwise.
     */
    static bool ExportNotebookPackage(
        const std::string& notebookPath,
        const std::string& destinationArchive,
        int compressionLevel = 7
    );

    /**
     * @brief Exports an entire Library folder into a consolidated compressed package (.foliolib / .7z).
     *
     * @param libraryPath Absolute path to the source library directory.
     * @param destinationArchive Target archive file path (e.g. "exports/University_Library.foliolib").
     * @param compressionLevel 7-Zip compression level from 1 (fastest) to 9 (ultra).
     * @return true if successfully compressed; false otherwise.
     */
    static bool ExportLibraryPackage(
        const std::string& libraryPath,
        const std::string& destinationArchive,
        int compressionLevel = 7
    );
};

} // namespace Folio
