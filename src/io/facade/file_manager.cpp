/**
 * =========================================================================================
 * @file io/facade/file_manager.cpp
 * @brief Implementation of Core Filesystem Tree Operations & Directory Mutations
 * =========================================================================================
 */

#include "io/facade/file_manager.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"

#include <filesystem>
#include <algorithm>
#include <vector>
#include <SDL3/SDL.h>

namespace Folio {

bool FileManager::Exists(const std::string& path) {
    return FileReader::Exists(path);
}

bool FileManager::IsDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_directory(PathUtils::Utf8ToNativePath(path), ec);
}

bool FileManager::IsRegularFile(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(PathUtils::Utf8ToNativePath(path), ec);
}

bool FileManager::AreEquivalent(const std::string& pathA, const std::string& pathB) {
    if (pathA.empty() || pathB.empty()) return false;
    if (PathUtils::NormalizeSeparators(pathA) == PathUtils::NormalizeSeparators(pathB)) return true;

    std::error_code ec;
    auto nativeA = PathUtils::Utf8ToNativePath(pathA);
    auto nativeB = PathUtils::Utf8ToNativePath(pathB);

    if (!std::filesystem::exists(nativeA, ec) || !std::filesystem::exists(nativeB, ec)) {
        return false;
    }
    return std::filesystem::equivalent(nativeA, nativeB, ec);
}

bool FileManager::CreateDirectories(const std::string& dirPath) {
    if (dirPath.empty()) return false;
    std::error_code ec;
    auto nativeP = PathUtils::Utf8ToNativePath(dirPath);
    if (std::filesystem::exists(nativeP, ec)) {
        return true;
    }
    bool ok = std::filesystem::create_directories(nativeP, ec);
    if (ec) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDirectoryCreateFailed, 
            "Failed to create directories at: " + dirPath + " | " + ec.message()));
        return false;
    }
    return ok || std::filesystem::exists(nativeP, ec);
}

bool FileManager::RemoveFile(const std::string& filePath) {
    if (filePath.empty()) return false;
    std::error_code ec;
    auto nativeP = PathUtils::Utf8ToNativePath(filePath);
    bool removed = std::filesystem::remove(nativeP, ec);
    if (ec) {
        LOG_WARN(FileManager, "RemoveFile failed for: " + filePath + " | " + ec.message());
    }
    return removed;
}

bool FileManager::RemoveDirectoryRecursive(const std::string& dirPath) {
    if (dirPath.empty()) return false;
    std::error_code ec;
    auto nativeP = PathUtils::Utf8ToNativePath(dirPath);
    uintmax_t count = std::filesystem::remove_all(nativeP, ec);
    if (ec) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileDeleteFailed, 
            "RemoveDirectoryRecursive failed for: " + dirPath + " | " + ec.message()));
        return false;
    }
    return count > 0;
}

bool FileManager::CopySingleFile(const std::string& sourcePath, const std::string& destinationPath, bool overwrite) {
    if (sourcePath.empty() || destinationPath.empty()) {
        return false;
    }

    std::string parentDir = PathUtils::GetParentPath(destinationPath);
    if (!parentDir.empty()) {
        CreateDirectories(parentDir);
    }

    std::error_code ec;
    auto srcNative = PathUtils::Utf8ToNativePath(sourcePath);
    auto dstNative = PathUtils::Utf8ToNativePath(destinationPath);

    // If source is a standard disk path (not an Android content:// URI), try fast OS copy first
    if (sourcePath.rfind("content://", 0) != 0) {
        auto options = overwrite ? std::filesystem::copy_options::overwrite_existing 
                                 : std::filesystem::copy_options::skip_existing;
        if (std::filesystem::copy_file(srcNative, dstNative, options, ec) && !ec) {
            return true;
        }
    }

    // Fallback: Cross-platform stream copy via SDL_IOStream
    // Essential on Android where SAF returns content:// URIs that standard C++ filesystem cannot open
    SDL_IOStream* inStream = SDL_IOFromFile(sourcePath.c_str(), "rb");
    if (!inStream) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileReadFailed, 
            "CopySingleFile failed to open source: " + sourcePath + " (" + SDL_GetError() + ")"));
        return false;
    }

    SDL_IOStream* outStream = SDL_IOFromFile(destinationPath.c_str(), overwrite ? "wb" : "ab");
    if (!outStream) {
        SDL_CloseIO(inStream);
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, 
            "CopySingleFile failed to open destination: " + destinationPath + " (" + SDL_GetError() + ")"));
        return false;
    }

    std::vector<uint8_t> buffer(65536);
    size_t bytesRead = 0;
    bool writeSuccess = true;
    while ((bytesRead = SDL_ReadIO(inStream, buffer.data(), buffer.size())) > 0) {
        if (SDL_WriteIO(outStream, buffer.data(), bytesRead) != bytesRead) {
            writeSuccess = false;
            break;
        }
    }

    SDL_CloseIO(inStream);
    SDL_CloseIO(outStream);

    if (!writeSuccess) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, 
            "CopySingleFile stream write failed: " + sourcePath + " -> " + destinationPath));
        std::filesystem::remove(dstNative, ec);
        return false;
    }

    return true;
}

bool FileManager::CopyDirectoryRecursive(const std::string& sourceDir, const std::string& destinationDir) {
    if (AreEquivalent(sourceDir, destinationDir)) {
        return true;
    }

    std::error_code ec;
    auto srcNative = PathUtils::Utf8ToNativePath(sourceDir);
    auto dstNative = PathUtils::Utf8ToNativePath(destinationDir);

    // Prevent infinite recursive copy if destination is inside source
    std::error_code canonEc;
    auto srcCanon = std::filesystem::weakly_canonical(srcNative, canonEc);
    auto dstCanon = std::filesystem::weakly_canonical(dstNative, canonEc);
    if (!canonEc) {
        std::string srcStr = PathUtils::NormalizeSeparators(srcCanon.string());
        std::string dstStr = PathUtils::NormalizeSeparators(dstCanon.string());
        
        if (!srcStr.empty() && srcStr.back() != '/') srcStr += '/';
        if (!dstStr.empty() && dstStr.back() != '/') dstStr += '/';

        if (dstStr.find(srcStr) == 0) {
            LOG_ERROR(FileManager, "CopyDirectoryRecursive failed: destination is inside source directory.");
            return false;
        }
    }

    if (!CreateDirectories(destinationDir)) {
        return false;
    }

    std::filesystem::copy(
        srcNative, 
        dstNative, 
        std::filesystem::copy_options::recursive | 
        std::filesystem::copy_options::overwrite_existing | 
        std::filesystem::copy_options::copy_symlinks, 
        ec
    );

    if (ec) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, 
            "CopyDirectoryRecursive failed: " + sourceDir + " -> " + destinationDir + " | " + ec.message()));
        return false;
    }
    return true;
}

bool FileManager::Move(const std::string& sourcePath, const std::string& destinationPath) {
    if (AreEquivalent(sourcePath, destinationPath)) {
        return true;
    }

    std::error_code ec;
    auto srcNative = PathUtils::Utf8ToNativePath(sourcePath);
    auto dstNative = PathUtils::Utf8ToNativePath(destinationPath);

    std::string parentDir = PathUtils::GetParentPath(destinationPath);
    if (!parentDir.empty()) {
        CreateDirectories(parentDir);
    }

    std::filesystem::rename(srcNative, dstNative, ec);
    if (!ec) {
        return true;
    }

    LOG_INFO(FileManager, "Atomic rename across volumes failed ('" + ec.message() + "'). Initiating fallback copy+delete...");
    ec.clear();

    if (std::filesystem::is_directory(srcNative, ec)) {
        if (!CopyDirectoryRecursive(sourcePath, destinationPath)) {
            RemoveDirectoryRecursive(destinationPath);
            return false;
        }
        RemoveDirectoryRecursive(sourcePath);
    } else {
        if (!CopySingleFile(sourcePath, destinationPath, true)) {
            RemoveFile(destinationPath);
            return false;
        }
        RemoveFile(sourcePath);
    }

    return true;
}

std::vector<FileEntry> FileManager::ListEntries(const std::string& dirPath, const std::string& extensionFilter) {
    std::vector<FileEntry> results;
    if (dirPath.empty()) return results;

    std::error_code ec;
    auto nativeP = PathUtils::Utf8ToNativePath(dirPath);
    if (!std::filesystem::exists(nativeP, ec) || !std::filesystem::is_directory(nativeP, ec)) {
        return results;
    }

    auto iter = std::filesystem::directory_iterator(nativeP, ec);
    if (ec) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDirectoryIterateFailed, 
            "ListEntries failed to iterate: " + dirPath + " | " + ec.message()));
        return results;
    }

    std::string filter = extensionFilter;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (!filter.empty() && filter.front() != '.') {
        filter = "." + filter;
    }

    for (const auto& entry : iter) {
        if (!filter.empty()) {
            std::string entryExt = PathUtils::NativePathToUtf8(entry.path().extension());
            std::transform(entryExt.begin(), entryExt.end(), entryExt.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (entryExt != filter) {
                continue;
            }
        }

        FileEntry fe;
        fe.fullPath = PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(entry.path()));
        fe.fileName = PathUtils::NativePathToUtf8(entry.path().filename());
        fe.stem = PathUtils::NativePathToUtf8(entry.path().stem());
        fe.extension = PathUtils::GetExtension(fe.fullPath);

        if (entry.is_directory(ec)) {
            fe.type = FileType::Directory;
            fe.sizeBytes = 0;
        } else if (entry.is_regular_file(ec)) {
            fe.type = FileType::RegularFile;
            fe.sizeBytes = entry.file_size(ec);
        } else {
            fe.type = FileType::Other;
            fe.sizeBytes = 0;
        }

        results.push_back(std::move(fe));
    }

    return results;
}

std::vector<std::string> FileManager::ListDirectories(const std::string& dirPath) {
    std::vector<std::string> results;
    if (dirPath.empty()) return results;

    std::error_code ec;
    auto nativeP = PathUtils::Utf8ToNativePath(dirPath);
    if (!std::filesystem::exists(nativeP, ec) || !std::filesystem::is_directory(nativeP, ec)) {
        return results;
    }

    auto iter = std::filesystem::directory_iterator(nativeP, ec);
    if (ec) return results;

    for (const auto& entry : iter) {
        if (entry.is_directory(ec)) {
            results.push_back(PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(entry.path())));
        }
    }
    return results;
}

uint64_t FileManager::GetFileSize(const std::string& filePath) {
    if (filePath.empty()) return 0;
    std::error_code ec;
    auto nativeP = PathUtils::Utf8ToNativePath(filePath);
    if (std::filesystem::is_regular_file(nativeP, ec)) {
        return std::filesystem::file_size(nativeP, ec);
    }
    return 0;
}

} // namespace Folio