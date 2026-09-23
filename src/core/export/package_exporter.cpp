/**
 * =========================================================================================
 * @file package_exporter.cpp
 * @brief Implementation of High-Ratio 7-Zip Archival Exporter
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Safe SQLite WAL Flushing:
 *    Invokes `BackupManager::FlushSqliteWal` across all constituent notebooks to merge and
 *    truncate write-ahead logs (`structure.db-wal`) before compression begins.
 * 2. Multi-Threaded 7-Zip Archiving:
 *    Executes 7-Zip with `-mmt=on` (parallel multi-threading) and `-mx{level}` (configurable
 *    compression level 1-9) producing compact `.7z` or `.zip` archives.
 * 3. System Tar Fallback:
 *    When 7-Zip is not detected on the machine, delegates to Windows built-in `tar.exe -czf`.
 */

#include "core/export/package_exporter.hpp"
#include "core/backup/backup_manager.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"
#include <filesystem>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Folio {

std::string PackageExporter::Find7ZipExecutable() {
#if defined(_WIN32)
    static const std::vector<std::string> searchPaths = {
        "C:\\Program Files\\7-Zip\\7z.exe",
        "C:\\Program Files (x86)\\7-Zip\\7z.exe"
    };

    for (const auto& path : searchPaths) {
        if (FileManager::Exists(path)) {
            return path;
        }
    }
#endif
    return "";
}

namespace {

bool ExecuteProcess(const std::string& commandLine) {
#if defined(_WIN32)
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Run background compression without popping up a console window
    ZeroMemory(&pi, sizeof(pi));

    std::string mutableCmd = commandLine;
    if (!CreateProcessA(
        NULL,
        &mutableCmd[0],
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        LOG_ERROR(FileManager, "PackageExporter: CreateProcess failed for: " + commandLine);
        return false;
    }

    // Wait for compression process to finish (up to 5 minutes)
    WaitForSingleObject(pi.hProcess, 300000);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
#else
    int res = std::system(commandLine.c_str());
    return (res == 0);
#endif
}

} // anonymous namespace

bool PackageExporter::ExportNotebookPackage(
    const std::string& notebookPath,
    const std::string& destinationArchive,
    int compressionLevel
) {
    if (notebookPath.empty() || !FileManager::IsDirectory(notebookPath)) {
        LOG_ERROR(FileManager, "ExportNotebookPackage: Invalid notebook path: " + notebookPath);
        return false;
    }

    std::string destDir = FileManager::GetParentPath(destinationArchive);
    if (!destDir.empty()) {
        FileManager::CreateDirectories(destDir);
    }

    // Checkpoint SQLite WAL buffers so the archive contains a clean, consistent structure.db
    BackupManager::FlushSqliteWal(notebookPath);

    std::string z7Path = Find7ZipExecutable();
    if (!z7Path.empty()) {
        int level = std::clamp(compressionLevel, 1, 9);
        std::string cmd = "\"" + z7Path + "\" a -t7z -mx" + std::to_string(level) +
                          " -mmt=on -y \"" + destinationArchive + "\" \"" + notebookPath + "\\*\"";

        LOG_INFO(FileManager, "ExportNotebookPackage: Running 7-Zip compression: " + destinationArchive);
        if (ExecuteProcess(cmd)) {
            LOG_INFO(FileManager, "ExportNotebookPackage: Successfully exported archive: " + destinationArchive);
            return true;
        }
    }

    // Fallback to tar.exe if 7-Zip was not found or failed
    std::string tarCmd = "tar.exe -czf \"" + destinationArchive + "\" -C \"" +
                         FileManager::GetParentPath(notebookPath) + "\" \"" +
                         FileManager::GetFileName(notebookPath) + "\"";
    LOG_INFO(FileManager, "ExportNotebookPackage: Running tar fallback: " + destinationArchive);
    return ExecuteProcess(tarCmd);
}

bool PackageExporter::ExportLibraryPackage(
    const std::string& libraryPath,
    const std::string& destinationArchive,
    int compressionLevel
) {
    if (libraryPath.empty() || !FileManager::IsDirectory(libraryPath)) {
        LOG_ERROR(FileManager, "ExportLibraryPackage: Invalid library path: " + libraryPath);
        return false;
    }

    std::string destDir = FileManager::GetParentPath(destinationArchive);
    if (!destDir.empty()) {
        FileManager::CreateDirectories(destDir);
    }

    // Checkpoint SQLite WAL buffers across all notebooks in the library
    BackupManager::FlushSqliteWal(libraryPath);

    std::string z7Path = Find7ZipExecutable();
    if (!z7Path.empty()) {
        int level = std::clamp(compressionLevel, 1, 9);
        std::string cmd = "\"" + z7Path + "\" a -t7z -mx" + std::to_string(level) +
                          " -mmt=on -y \"" + destinationArchive + "\" \"" + libraryPath + "\\*\"";

        LOG_INFO(FileManager, "ExportLibraryPackage: Running 7-Zip library compression: " + destinationArchive);
        if (ExecuteProcess(cmd)) {
            LOG_INFO(FileManager, "ExportLibraryPackage: Successfully exported library archive: " + destinationArchive);
            return true;
        }
    }

    // Fallback to tar.exe
    std::string tarCmd = "tar.exe -czf \"" + destinationArchive + "\" -C \"" +
                         FileManager::GetParentPath(libraryPath) + "\" \"" +
                         FileManager::GetFileName(libraryPath) + "\"";
    LOG_INFO(FileManager, "ExportLibraryPackage: Running tar fallback: " + destinationArchive);
    return ExecuteProcess(tarCmd);
}

} // namespace Folio
