#include "io/storage/file_writer.hpp"
#include "io/paths/path_utils.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"

#include <filesystem>
#include <cstdio>
#include <chrono>
#include <random>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>     // For _commit and _fileno
#else
#include <unistd.h> // For fsync and fileno
#endif

namespace Folio {

namespace {

bool SyncFileToPhysicalDisk(FILE* fp) noexcept {
    if (!fp) return false;
    fflush(fp);
#if defined(_WIN32)
    int fd = _fileno(fp);
    if (fd >= 0) {
        return _commit(fd) == 0;
    }
#else
    int fd = fileno(fp);
    if (fd >= 0) {
        return fsync(fd) == 0;
    }
#endif
    return false;
}

std::string GenerateStagingPath(const std::string& targetPath) {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    uint64_t randVal = rng();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return targetPath + ".tmp." + std::to_string(now) + "_" + std::to_string(randVal);
}

bool PerformAtomicRename(const std::string& stagePath, const std::string& targetPath, const char* opName) {
    auto nativeStage = PathUtils::Utf8ToNativePath(stagePath);
    auto nativeTarget = PathUtils::Utf8ToNativePath(targetPath);
    std::error_code ec;

#if defined(_WIN32)
    PathUtils::EnsureTargetWritable(nativeTarget);
    bool renamed = false;
    DWORD err = 0;
    // Micro-retry loop (up to 5 attempts with 15ms backoff) to handle transient
    // file locks from Windows Antivirus (Defender), indexing services, or cloud sync engines (OneDrive).
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(nativeStage.wstring().c_str(), nativeTarget.wstring().c_str(), 
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            renamed = true;
            break;
        }
        err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED) {
            PathUtils::EnsureTargetWritable(nativeTarget);
            Sleep(15 * (attempt + 1));
            continue;
        }
        break; // Non-transient error
    }
    if (!renamed) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileRenameFailed, 
            std::string(opName) + " MoveFileExW failed for '" + targetPath + "' | Win32 Error: " + std::to_string(err)));
        std::filesystem::remove(nativeStage, ec);
        return false;
    }
#else
    bool renamed = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::filesystem::rename(nativeStage, nativeTarget, ec);
        if (!ec) {
            renamed = true;
            break;
        }
        if (ec.value() == EBUSY || ec.value() == ETXTBSY || ec.value() == EAGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15 * (attempt + 1)));
            continue;
        }
        break;
    }
    if (!renamed) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileRenameFailed, 
            std::string(opName) + " rename failed: " + stagePath + " -> " + targetPath + " | " + ec.message()));
        std::filesystem::remove(nativeStage, ec);
        return false;
    }
#endif

    return true;
}

} // anonymous namespace

bool FileWriter::CreateParentDirectories(const std::string& filePath) {
    std::string parent = PathUtils::GetParentPath(filePath);
    if (parent.empty()) return true;
    std::error_code ec;
    return std::filesystem::create_directories(PathUtils::Utf8ToNativePath(parent), ec) || !ec;
}

bool FileWriter::WriteString(const std::string& targetPath, const std::string& content) {
    if (targetPath.empty()) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysPathResolutionFailed, "WriteTextAtomic rejected: Target path is empty"));
        return false;
    }

    if (!CreateParentDirectories(targetPath)) {
        std::string parentDir = PathUtils::GetParentPath(targetPath);
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDirectoryCreateFailed, "WriteTextAtomic failed: Cannot create parent directory: " + parentDir));
        return false;
    }

    std::string stagePath = GenerateStagingPath(targetPath);

    bool writeOk = false;
#if defined(_WIN32)
    FILE* fp = _wfopen(PathUtils::Utf8ToNativePath(stagePath).wstring().c_str(), L"wb");
#else
    FILE* fp = fopen(stagePath.c_str(), "wb");
#endif

    if (fp) {
        size_t written = 0;
        if (!content.empty()) {
            written = fwrite(content.data(), 1, content.size(), fp);
        }
        SyncFileToPhysicalDisk(fp);
        fclose(fp);
        writeOk = (written == content.size());
    }

    auto nativeStage = PathUtils::Utf8ToNativePath(stagePath);
    std::error_code ec;

    if (!writeOk) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, "WriteTextAtomic failed to write staging file: " + stagePath));
        std::filesystem::remove(nativeStage, ec);
        return false;
    }

    // Disk-full & truncation fail-safe: verify written size matches content length exactly
    uint64_t actualSize = 0;
    if (std::filesystem::is_regular_file(nativeStage, ec)) {
        actualSize = std::filesystem::file_size(nativeStage, ec);
    }
    if (actualSize != content.size()) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDiskFull, 
            "WriteTextAtomic aborted: Staging file size (" + std::to_string(actualSize) + 
            " bytes) does not match content size (" + std::to_string(content.size()) + 
            " bytes). Existing target file preserved."));
        std::filesystem::remove(nativeStage, ec);
        return false;
    }

    return PerformAtomicRename(stagePath, targetPath, "WriteTextAtomic");
}

bool FileWriter::WriteBuffer(const std::string& targetPath, const std::vector<uint8_t>& buffer) {
    if (targetPath.empty()) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysPathResolutionFailed, "WriteBinaryAtomic rejected: Target path is empty"));
        return false;
    }

    if (!CreateParentDirectories(targetPath)) {
        std::string parentDir = PathUtils::GetParentPath(targetPath);
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDirectoryCreateFailed, "WriteBinaryAtomic failed: Cannot create parent directory: " + parentDir));
        return false;
    }

    std::string stagePath = GenerateStagingPath(targetPath);

    bool writeOk = false;
#if defined(_WIN32)
    FILE* fp = _wfopen(PathUtils::Utf8ToNativePath(stagePath).wstring().c_str(), L"wb");
#else
    FILE* fp = fopen(stagePath.c_str(), "wb");
#endif

    if (fp) {
        size_t written = 0;
        if (!buffer.empty()) {
            written = fwrite(buffer.data(), 1, buffer.size(), fp);
        }
        SyncFileToPhysicalDisk(fp);
        fclose(fp);
        writeOk = (written == buffer.size());
    }

    auto nativeStage = PathUtils::Utf8ToNativePath(stagePath);
    std::error_code ec;

    if (!writeOk) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, "WriteBinaryAtomic failed to write staging buffer: " + stagePath));
        std::filesystem::remove(nativeStage, ec);
        return false;
    }

    // Disk-full & truncation fail-safe: verify written size matches buffer exactly
    uint64_t actualSize = 0;
    if (std::filesystem::is_regular_file(nativeStage, ec)) {
        actualSize = std::filesystem::file_size(nativeStage, ec);
    }
    if (actualSize != buffer.size()) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDiskFull, 
            "WriteBinaryAtomic aborted: Staging file size (" + std::to_string(actualSize) + 
            " bytes) does not match payload size (" + std::to_string(buffer.size()) + 
            " bytes). Existing target file preserved."));
        std::filesystem::remove(nativeStage, ec);
        return false;
    }

    return PerformAtomicRename(stagePath, targetPath, "WriteBinaryAtomic");
}

} // namespace Folio
