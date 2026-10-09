#pragma once
/**
 * =========================================================================================
 * @file io/file_manager.hpp
 * @brief Centralized, Modular, Cross-Platform Filesystem Abstraction Layer for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE & FACADE PATTERN:
 * FileManager serves as the unified master coordinator for all filesystem and I/O
 * operations in FolioNote. To maintain clean modularity and keep implementation files
 * maintainable (< 300 lines each), low-level domains are partitioned into dedicated submodules:
 *
 * - PathUtils: Unicode-aware path manipulation, normalization, and sanitization.
 * - AppDirectories: Standard application and package folder resolution.
 * - SystemDialogs: Native OS file pickers, shell launchers, and clipboard bridge.
 * - FileWriter: Crash-resilient atomic persistence and physical drive synchronization.
 * - FileReader: Cross-platform asset loading, stream acquisition, and content hashing.
 *
 * All public interfaces remain accessible via FileManager to guarantee complete
 * backward compatibility across the codebase.
 */

#include "io/path_utils.hpp"
#include "io/app_directories.hpp"
#include "io/system_dialogs.hpp"
#include "io/file_writer.hpp"
#include "io/file_reader.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <SDL3/SDL.h>

namespace Folio {

/**
 * @enum FileType
 * @brief Categorization of directory entries during filesystem traversals.
 */
enum class FileType {
    RegularFile,    ///< Standard disk file or bundle asset
    Directory,      ///< Physical folder or package directory (.fn, .notebook)
    Other           ///< Symlinks, block devices, or unknown entries
};

/**
 * @struct FileEntry
 * @brief Telemetry and attributes for an enumerated file or directory entry.
 */
struct FileEntry {
    std::string fullPath;                  ///< Complete normalized UTF-8 filesystem path
    std::string fileName;                  ///< Leaf name including extension (e.g. "Notes.fn")
    std::string stem;                      ///< Base name without extension (e.g. "Notes")
    std::string extension;                 ///< Extension in lowercase with leading dot (".fn")
    FileType type = FileType::RegularFile; ///< Entry classification
    uint64_t sizeBytes = 0;                ///< File size in bytes (0 for directories)
};

/**
 * @class FileManager
 * @brief Modular cross-platform filesystem service and I/O coordinator facade.
 */
class FileManager {
public:
    // =====================================================================================
    // 1. STANDARD APPLICATION DIRECTORIES (PLATFORM-AWARE)
    // =====================================================================================

    static std::string GetAppRootDirectory(const std::string& overridePath = "") {
        return AppDirectories::GetAppRootDirectory(overridePath);
    }

    static void SetAppRootDirectory(const std::string& root) {
        AppDirectories::SetAppRootDirectory(root);
    }

    static std::string GetLibrariesDirectory() {
        return AppDirectories::GetLibrariesDirectory();
    }

    static std::string GetConfigDirectory() {
        return AppDirectories::GetConfigDirectory();
    }

    static std::string GetCacheDirectory() {
        return AppDirectories::GetCacheDirectory();
    }

    static std::string GetExportsDirectory() {
        return AppDirectories::GetExportsDirectory();
    }

    static std::string GetLogsDirectory() {
        return AppDirectories::GetLogsDirectory();
    }

    static std::string GetBackupsDirectory() {
        return AppDirectories::GetBackupsDirectory();
    }

    static std::string GetTempDirectory() {
        return AppDirectories::GetTempDirectory();
    }

    static void SetActivePackageRoot(const std::string& root) {
        AppDirectories::SetActivePackageRoot(root);
    }

    static std::string GetActivePackageRoot() {
        return AppDirectories::GetActivePackageRoot();
    }

    static std::string ResolveAssetPath(const std::string& path) {
        return AppDirectories::ResolveAssetPath(path);
    }

    // =====================================================================================
    // 2. PATH MANIPULATION & UNICODE NORMALIZATION
    // =====================================================================================

    static std::string NormalizeSeparators(const std::string& path) {
        return PathUtils::NormalizeSeparators(path);
    }

    static std::string JoinPath(const std::string& base, const std::string& child) {
        return PathUtils::JoinPath(base, child);
    }

    static std::string JoinPath(const std::string& part1, const std::string& part2, const std::string& part3) {
        return PathUtils::JoinPath(part1, part2, part3);
    }

    static std::string GetParentPath(const std::string& path) {
        return PathUtils::GetParentPath(path);
    }

    static std::string GetFileName(const std::string& path) {
        return PathUtils::GetFileName(path);
    }

    static std::string GetStem(const std::string& path) {
        return PathUtils::GetStem(path);
    }

    static std::string GetExtension(const std::string& path) {
        return PathUtils::GetExtension(path);
    }

    static bool HasExtension(const std::string& path, const std::string& ext) {
        return PathUtils::HasExtension(path, ext);
    }

    [[nodiscard]] static bool IsAbsolutePath(const std::string& path) {
        return PathUtils::IsAbsolutePath(path);
    }

    static std::string SanitizeFileName(const std::string& name, char replacement = '_') {
        return PathUtils::SanitizeFileName(name, replacement);
    }

    static std::string PathToFileUri(const std::string& path) {
        return PathUtils::PathToFileUri(path);
    }

    static std::string DisambiguatePath(const std::string& parentDir, const std::string& baseStem, const std::string& extension) {
        return PathUtils::DisambiguatePath(parentDir, baseStem, extension);
    }

    // =====================================================================================
    // 3. NATIVE OS DIALOGS, SHELL & CLIPBOARD
    // =====================================================================================

    static std::string ShowOpenFileDialog(const std::string& title = "Select File") {
        return SystemDialogs::ShowOpenFileDialog(title);
    }

    static std::string ShowSaveFileDialog(const std::string& title = "Save File", const std::string& defaultFileName = "") {
        return SystemDialogs::ShowSaveFileDialog(title, defaultFileName);
    }

    static bool OpenWithDefaultApp(const std::string& pathOrUrl) {
        return SystemDialogs::OpenWithDefaultApp(pathOrUrl);
    }

    // =====================================================================================
    // 4. ATOMIC CRASH-RESILIENT FILE I/O & READING
    // =====================================================================================

    static bool WriteTextAtomic(const std::string& targetPath, const std::string& content) {
        return FileWriter::WriteString(targetPath, content);
    }

    static bool WriteBinaryAtomic(const std::string& targetPath, const std::vector<uint8_t>& buffer) {
        return FileWriter::WriteBuffer(targetPath, buffer);
    }

    static bool ReadText(const std::string& filePath, std::string& outContent) {
        return FileReader::ReadToString(filePath, outContent);
    }

    static bool ReadBinary(const std::string& filePath, std::vector<uint8_t>& outBuffer) {
        return FileReader::ReadToBuffer(filePath, outBuffer);
    }

    [[nodiscard]] static SDL_IOStream* OpenReadStream(const std::string& filePath) {
        return FileReader::OpenAsStream(filePath);
    }

    // =====================================================================================
    // 5. FILESYSTEM QUERIES & MUTATIONS (PHYSICAL OPERATIONS)
    // =====================================================================================

    [[nodiscard]] static bool Exists(const std::string& path);
    [[nodiscard]] static bool IsDirectory(const std::string& path);
    [[nodiscard]] static bool IsRegularFile(const std::string& path);
    [[nodiscard]] static bool AreEquivalent(const std::string& pathA, const std::string& pathB);
    static bool CreateDirectories(const std::string& dirPath);
    static bool RemoveFile(const std::string& filePath);
    static bool RemoveDirectoryRecursive(const std::string& dirPath);
    static bool CopySingleFile(const std::string& sourcePath, const std::string& destinationPath, bool overwrite = true);
    static bool CopyDirectoryRecursive(const std::string& sourceDir, const std::string& destinationDir);

    static bool CopyFileTo(const std::string& sourcePath, const std::string& destinationPath) {
        return CopySingleFile(sourcePath, destinationPath, /*overwrite=*/true);
    }

    static bool Move(const std::string& sourcePath, const std::string& destinationPath);
    static std::vector<FileEntry> ListEntries(const std::string& dirPath, const std::string& extensionFilter = "");
    static std::vector<std::string> ListDirectories(const std::string& dirPath);
    [[nodiscard]] static uint64_t GetFileSize(const std::string& filePath);

    // =====================================================================================
    // 6. DIAGNOSTICS & HASHING
    // =====================================================================================

    static bool ComputeFileHash64(const std::string& filePath, uint64_t& outHash, uint64_t& outSize) {
        return FileReader::ComputeFileHash64(filePath, outHash, outSize);
    }

    static int DetectPdfPageCount(const std::string& filePath) {
        return FileReader::DetectPdfPageCount(filePath);
    }
};

} // namespace Folio