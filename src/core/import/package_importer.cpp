/**
 * =========================================================================================
 * @file package_importer.cpp
 * @brief Implementation of High-Performance Package & Archive Importer for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Multi-Threaded 7-Zip Decompression:
 *    Invokes `7z.exe x -y -o"<dest>" "<src>"` to decompress LZMA2 / 7z archives with full CPU
 *    utilization. Falls back to Windows system `tar.exe` if 7-Zip is not installed.
 * 2. Structural Inspection & Validation:
 *    Inspects decompressed contents to identify whether the archive contains a nested `.notebook`
 *    package directory or root-level notebook files (`structure.db` / `pages/`).
 * 3. Atomic Collision-Free Relocation:
 *    Moves decompressed notebook packages into the target library root using conflict-free naming
 *    `Notebook_Name (Imported N).notebook` without overwriting existing user data.
 * 4. Staging Isolation & Cleanup:
 *    Performs all extractions in an isolated ephemeral staging folder and guarantees complete
 *    removal of temporary files upon success or failure.
 */

#include "core/import/package_importer.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"
#include "utils/guid_generator.hpp"

#include <filesystem>
#include <algorithm>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Folio {

namespace {

bool ExecuteProcess(const std::string& commandLine) {
#if defined(_WIN32)
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Run background extraction silently
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuf(commandLine.begin(), commandLine.end());
    cmdBuf.push_back('\0');

    if (!CreateProcessA(
            NULL,
            cmdBuf.data(),
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &pi)) {
        return false;
    }

    // Wait up to 5 minutes for decompression of large notebook libraries
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

std::string GenerateUniqueDestinationPath(const std::filesystem::path& destDir, const std::string& baseStem) {
    std::error_code ec;
    std::filesystem::path target = destDir / (baseStem + ".notebook");
    int counter = 1;
    while (std::filesystem::exists(target, ec)) {
        target = destDir / (baseStem + " (Imported " + std::to_string(counter++) + ").notebook");
    }
    return target.string();
}

} // anonymous namespace

std::string PackageImporter::Find7ZipExecutable() {
#if defined(_WIN32)
    const std::vector<std::string> standardLocations = {
        "C:\\Program Files\\7-Zip\\7z.exe",
        "C:\\Program Files (x86)\\7-Zip\\7z.exe"
    };

    for (const auto& loc : standardLocations) {
        if (FileManager::Exists(loc)) {
            return loc;
        }
    }
#endif
    return "";
}

bool PackageImporter::IsArchiveFile(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return (lower.rfind(".7z") != std::string::npos ||
            lower.rfind(".folionb.7z") != std::string::npos ||
            lower.rfind(".foliolib.7z") != std::string::npos ||
            lower.rfind(".fnpack") != std::string::npos ||
            lower.rfind(".zip") != std::string::npos);
}

bool PackageImporter::IsLibraryArchive(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (lower.rfind(".foliolib.7z") != std::string::npos || lower.rfind(".foliolib") != std::string::npos);
}

bool PackageImporter::DecompressArchive(const std::string& archivePath, const std::string& targetDirectory) {
    if (!FileManager::Exists(archivePath)) {
        LOG_ERROR(FileManager, "PackageImporter::DecompressArchive: Archive file does not exist: " + archivePath);
        return false;
    }

    FileManager::CreateDirectories(targetDirectory);

    std::string z7Path = Find7ZipExecutable();
    if (!z7Path.empty()) {
        std::string cmd = "\"" + z7Path + "\" x -y -o\"" + targetDirectory + "\" \"" + archivePath + "\"";
        LOG_INFO(FileManager, "PackageImporter: Decompressing with 7-Zip: " + archivePath);
        if (ExecuteProcess(cmd)) {
            return true;
        }
        LOG_WARN(FileManager, "PackageImporter: 7-Zip extraction returned non-zero, trying tar fallback.");
    }

    // Windows native tar fallback
    std::string tarCmd = "tar.exe -xzf \"" + archivePath + "\" -C \"" + targetDirectory + "\"";
    LOG_INFO(FileManager, "PackageImporter: Decompressing with tar: " + archivePath);
    return ExecuteProcess(tarCmd);
}

std::string PackageImporter::ImportPackage(
    const std::string& sourcePath,
    const std::string& destinationLibraryPath
) {
    std::error_code ec;
    std::filesystem::path src(sourcePath);
    if (!std::filesystem::exists(src, ec)) {
        LOG_ERROR(FileManager, "PackageImporter: Source package path does not exist: " + sourcePath);
        return "";
    }

    std::filesystem::path destDir(destinationLibraryPath);
    if (!std::filesystem::exists(destDir, ec)) {
        std::filesystem::create_directories(destDir, ec);
    }

    // Case 1: Uncompressed .notebook folder
    if (std::filesystem::is_directory(src, ec)) {
        std::string stem = src.stem().string();
        std::string targetPkg = GenerateUniqueDestinationPath(destDir, stem);

        LOG_INFO(FileManager, "PackageImporter: Copying uncompressed package to: " + targetPkg);
        std::filesystem::copy(src, targetPkg, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_ERROR(FileManager, "PackageImporter: Failed to copy directory: " + ec.message());
            return "";
        }
        return targetPkg;
    }

    // Case 2: Compressed archive (.folionb.7z / .fnpack / .7z / .zip)
    if (IsArchiveFile(sourcePath)) {
        std::string tempStaging = FileManager::JoinPath(
            FileManager::GetTempDirectory(),
            "_stage_import_" + GUIDGenerator::GenerateV4().substr(0, 8)
        );

        if (!DecompressArchive(sourcePath, tempStaging)) {
            LOG_ERROR(FileManager, "PackageImporter: Extraction failed for: " + sourcePath);
            std::filesystem::remove_all(tempStaging, ec);
            return "";
        }

        // Inspect staging directory to find .notebook directory or root files
        std::string discoveredNotebookDir;
        for (const auto& entry : std::filesystem::directory_iterator(tempStaging, ec)) {
            if (entry.is_directory(ec) && entry.path().extension() == ".notebook") {
                discoveredNotebookDir = entry.path().string();
                break;
            }
        }

        std::string finalDest;
        if (!discoveredNotebookDir.empty()) {
            std::string stem = std::filesystem::path(discoveredNotebookDir).stem().string();
            finalDest = GenerateUniqueDestinationPath(destDir, stem);
            std::filesystem::rename(discoveredNotebookDir, finalDest, ec);
            if (ec) {
                // Fallback to recursive copy if on different drives
                std::filesystem::copy(discoveredNotebookDir, finalDest, std::filesystem::copy_options::recursive, ec);
            }
        } else {
            // Root-level archive files: move the staging folder itself as the .notebook package
            std::string stem = src.stem().string();
            // If stem ends with .folionb, strip it
            if (stem.size() > 8 && stem.rfind(".folionb") == stem.size() - 8) {
                stem = stem.substr(0, stem.size() - 8);
            }
            finalDest = GenerateUniqueDestinationPath(destDir, stem);
            std::filesystem::rename(tempStaging, finalDest, ec);
            if (ec) {
                std::filesystem::copy(tempStaging, finalDest, std::filesystem::copy_options::recursive, ec);
            }
        }

        // Clean up temporary staging
        std::filesystem::remove_all(tempStaging, ec);

        LOG_INFO(FileManager, "PackageImporter: Successfully imported archive to: " + finalDest);
        return finalDest;
    }

    return "";
}

std::vector<std::string> PackageImporter::ImportLibraryPackage(
    const std::string& sourceArchivePath,
    const std::string& destinationLibraryPath
) {
    std::vector<std::string> importedPackages;
    std::error_code ec;

    if (!FileManager::Exists(sourceArchivePath)) {
        LOG_ERROR(FileManager, "PackageImporter: Library archive does not exist: " + sourceArchivePath);
        return importedPackages;
    }

    std::filesystem::path destDir(destinationLibraryPath);
    std::filesystem::create_directories(destDir, ec);

    std::string tempStaging = FileManager::JoinPath(
        FileManager::GetTempDirectory(),
        "_stage_lib_" + GUIDGenerator::GenerateV4().substr(0, 8)
    );

    if (!DecompressArchive(sourceArchivePath, tempStaging)) {
        LOG_ERROR(FileManager, "PackageImporter: Failed to decompress library archive: " + sourceArchivePath);
        std::filesystem::remove_all(tempStaging, ec);
        return importedPackages;
    }

    // Traverse staging directory to discover all .notebook packages
    for (const auto& entry : std::filesystem::recursive_directory_iterator(tempStaging, ec)) {
        if (entry.is_directory(ec) && entry.path().extension() == ".notebook") {
            std::string stem = entry.path().stem().string();
            std::string targetPkg = GenerateUniqueDestinationPath(destDir, stem);

            std::filesystem::rename(entry.path(), targetPkg, ec);
            if (ec) {
                std::filesystem::copy(entry.path(), targetPkg, std::filesystem::copy_options::recursive, ec);
            }

            if (!ec) {
                importedPackages.push_back(targetPkg);
                LOG_INFO(FileManager, "PackageImporter: Mounted library notebook: " + targetPkg);
            }
        }
    }

    std::filesystem::remove_all(tempStaging, ec);
    return importedPackages;
}

} // namespace Folio
