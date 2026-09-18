/**
 * =========================================================================================
 * @file file_manager.cpp
 * @brief Implementation of Cross-Platform Filesystem & Atomic I/O Operations for FolioNote
 * =========================================================================================
 *
 * UNICODE & MULTI-PLATFORM PATH HANDLING MECHANICS:
 * On Windows, the Win32 filesystem subsystem uses 16-bit wide characters (UTF-16 LE).
 * When standard C++ streams (`std::ofstream`, `std::ifstream`) or `std::filesystem::path`
 * are initialized with narrow `std::string`, the MSVC CRT interprets the bytes using the
 * system's active ANSI code page (CP_ACP, such as Windows-1252 or Windows-932).
 * Any multi-byte UTF-8 sequences (accented letters, CJK glyphs, Cyrillic, Greek, or emojis)
 * are corrupted into mojibake, leading to immediate file-not-found or access denied errors.
 *
 * To solve this permanently and modularly:
 * - This file provides internal conversion helpers (`Utf8ToNativePath` and `NativePathToUtf8`).
 * - On Windows (`_WIN32`), `MultiByteToWideChar(CP_UTF8, ...)` translates incoming UTF-8 strings
 *   into native `std::wstring` objects, which `std::filesystem::path` and `_wfopen` consume
 *   with 100% Unicode fidelity.
 * - On POSIX platforms (Linux, macOS, Android), `std::string` is already natively treated as UTF-8,
 *   so direct pass-through is used with zero conversion overhead.
 */

#include "utils/file_manager.hpp"
#include "utils/logger.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <cstring>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Folio {

namespace {

/**
 * @brief Converts a UTF-8 encoded string to a native std::filesystem::path with full Unicode fidelity.
 *
 * Mechanics on Windows:
 * Calls Win32 MultiByteToWideChar with code page CP_UTF8 to convert the narrow byte sequence
 * into a wide UTF-16 std::wstring, which is then passed directly into std::filesystem::path.
 *
 * Mechanics on Linux/macOS/Android:
 * Direct construction since paths are already UTF-8 byte sequences.
 *
 * @param utf8Str UTF-8 encoded path string.
 * @return std::filesystem::path representing the destination path natively.
 */
std::filesystem::path Utf8ToNativePath(const std::string& utf8Str) {
    if (utf8Str.empty()) {
        return std::filesystem::path();
    }
#if defined(_WIN32)
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (sizeNeeded <= 0) {
        return std::filesystem::path(utf8Str);
    }
    std::wstring wstr(static_cast<size_t>(sizeNeeded), 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), &wstr[0], sizeNeeded);
    return std::filesystem::path(std::move(wstr));
#else
    return std::filesystem::path(utf8Str);
#endif
}

/**
 * @brief Converts a native std::filesystem::path to a UTF-8 encoded std::string.
 *
 * Mechanics on Windows:
 * Standard `p.string()` on Windows converts wide paths back through CP_ACP, corrupting
 * non-ANSI characters into '?'. This function explicitly uses WideCharToMultiByte(CP_UTF8)
 * on `p.native()` to preserve all international characters.
 *
 * @param p Native filesystem path.
 * @return UTF-8 encoded string.
 */
std::string NativePathToUtf8(const std::filesystem::path& p) {
#if defined(_WIN32)
    const std::wstring& wstr = p.native();
    if (wstr.empty()) {
        return std::string();
    }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) {
        return p.string();
    }
    std::string str(static_cast<size_t>(sizeNeeded), 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &str[0], sizeNeeded, nullptr, nullptr);
    return str;
#else
    return p.string();
#endif
}

/**
 * @brief Generates an ephemeral staging path for crash-resilient atomic writes.
 * @param targetPath Final destination path.
 * @return Path string ending in ".tmp.<timestamp>_<rand>".
 */
std::string GenerateStagingPath(const std::string& targetPath) {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    uint64_t randVal = rng();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return targetPath + ".tmp." + std::to_string(now) + "_" + std::to_string(randVal);
}

} // anonymous namespace

// =========================================================================================
// 1. STANDARD APPLICATION DIRECTORIES (PLATFORM-AWARE)
// =========================================================================================

std::string FileManager::GetAppRootDirectory(const std::string& overridePath) {
    if (!overridePath.empty()) {
        return NormalizeSeparators(overridePath);
    }

#if defined(__ANDROID__)
    // ANDROID PLATFORM HOOK:
    // SDL_GetUserFolder() is unavailable or restricted by Android Scoped Storage.
    // SDL_GetPrefPath() queries Context.getFilesDir() via JNI, providing a guaranteed
    // writable private sandbox directory: e.g. /data/user/0/org.libsdl.app/files/
    const char* pref = SDL_GetPrefPath("UniversalFramework", "FolioNote");
    if (pref && pref[0] != '\0') {
        return NormalizeSeparators(std::string(pref));
    }
    return NormalizeSeparators("./FolioNote");
#else
    // DESKTOP PLATFORMS (Windows, macOS, Linux):
    // Resolve user's Documents folder: e.g. C:/Users/<User>/Documents on Windows,
    // or ~/Documents on macOS/Linux.
    const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    if (docs && docs[0] != '\0') {
        std::filesystem::path appRoot = Utf8ToNativePath(docs) / "FolioNote";
        return NormalizeSeparators(NativePathToUtf8(appRoot));
    }

    LOG_WARN(FileManager, "SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS) returned null; falling back to './FolioNote'");
    return NormalizeSeparators("./FolioNote");
#endif
}

std::string FileManager::GetLibrariesDirectory() {
    return JoinPath(GetAppRootDirectory(), "Libraries");
}

std::string FileManager::GetConfigDirectory() {
    return JoinPath(GetAppRootDirectory(), "config");
}

std::string FileManager::GetCacheDirectory() {
    return JoinPath(GetAppRootDirectory(), "cache");
}

std::string FileManager::GetExportsDirectory() {
    return JoinPath(GetAppRootDirectory(), "exports");
}

std::string FileManager::GetLogsDirectory() {
    return JoinPath(GetAppRootDirectory(), "logs");
}

std::string FileManager::GetTempDirectory() {
    std::error_code ec;
    auto tempPath = std::filesystem::temp_directory_path(ec);
    if (ec) {
        return JoinPath(GetAppRootDirectory(), "cache/temp");
    }
    return NormalizeSeparators(NativePathToUtf8(tempPath));
}

// =========================================================================================
// 2. PATH MANIPULATION & UNICODE NORMALIZATION
// =========================================================================================

std::string FileManager::NormalizeSeparators(const std::string& path) {
    if (path.empty()) return "";
    std::string normalized = path;
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    // Trim trailing slashes (unless root path e.g. "C:/" or "/")
    while (normalized.size() > 1 && normalized.back() == '/') {
        if (normalized.size() == 3 && normalized[1] == ':') break; // Preserve "C:/"
        normalized.pop_back();
    }
    return normalized;
}

std::string FileManager::JoinPath(const std::string& base, const std::string& child) {
    if (base.empty()) return NormalizeSeparators(child);
    if (child.empty()) return NormalizeSeparators(base);

    std::string b = NormalizeSeparators(base);
    std::string c = NormalizeSeparators(child);

    if (b.back() == '/') {
        if (c.front() == '/') {
            return b + c.substr(1);
        }
        return b + c;
    }
    if (c.front() == '/') {
        return b + c;
    }
    return b + "/" + c;
}

std::string FileManager::JoinPath(const std::string& part1, const std::string& part2, const std::string& part3) {
    return JoinPath(JoinPath(part1, part2), part3);
}

std::string FileManager::GetParentPath(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    auto parent = nativeP.parent_path();
    return NormalizeSeparators(NativePathToUtf8(parent));
}

std::string FileManager::GetFileName(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    return NativePathToUtf8(nativeP.filename());
}

std::string FileManager::GetStem(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    return NativePathToUtf8(nativeP.stem());
}

std::string FileManager::GetExtension(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    std::string ext = NativePathToUtf8(nativeP.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

bool FileManager::HasExtension(const std::string& path, const std::string& ext) {
    std::string actualExt = GetExtension(path);
    std::string targetExt = ext;
    std::transform(targetExt.begin(), targetExt.end(), targetExt.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (!targetExt.empty() && targetExt.front() != '.') {
        targetExt = "." + targetExt;
    }
    return actualExt == targetExt;
}

std::string FileManager::SanitizeFileName(const std::string& name, char replacement) {
    if (name.empty()) return "Untitled";

    std::string safe = name;
    for (char& c : safe) {
        // Forbidden characters across Windows FAT/NTFS and POSIX
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<'  || c == '>' || c == '|' ||
            static_cast<unsigned char>(c) < 32) {
            c = replacement;
        }
    }

    // Trim trailing spaces and dots which Windows disallows
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) {
        safe.pop_back();
    }
    return safe.empty() ? "Untitled" : safe;
}

std::string FileManager::DisambiguatePath(const std::string& parentDir, const std::string& baseStem, const std::string& extension) {
    std::string cleanStem = SanitizeFileName(baseStem.empty() ? "Untitled" : baseStem);
    std::string ext = extension;
    if (!ext.empty() && ext.front() != '.') {
        ext = "." + ext;
    }

    std::string candidate = JoinPath(parentDir, cleanStem + ext);
    int counter = 1;
    while (Exists(candidate)) {
        candidate = JoinPath(parentDir, cleanStem + " (" + std::to_string(counter++) + ")" + ext);
    }
    return candidate;
}

// =========================================================================================
// 3. ATOMIC FILE I/O (CRASH-RESILIENT WRITES)
// =========================================================================================

bool FileManager::WriteTextAtomic(const std::string& targetPath, const std::string& content) {
    if (targetPath.empty()) {
        LOG_ERROR(FileManager, "WriteTextAtomic rejected: Target path is empty");
        return false;
    }

    std::string parentDir = GetParentPath(targetPath);
    if (!parentDir.empty() && !CreateDirectories(parentDir)) {
        LOG_ERROR(FileManager, "WriteTextAtomic failed: Cannot create parent directory: " + parentDir);
        return false;
    }

    std::string stagePath = GenerateStagingPath(targetPath);

    // 1. Write content to staging file
    bool writeOk = false;
#if defined(_WIN32)
    // On Windows, use wide-character file stream to avoid ANSI encoding bugs
    FILE* fp = _wfopen(Utf8ToNativePath(stagePath).wstring().c_str(), L"wb");
    if (fp) {
        size_t written = fwrite(content.data(), 1, content.size(), fp);
        fflush(fp);
        fclose(fp);
        writeOk = (written == content.size());
    }
#else
    // POSIX
    FILE* fp = fopen(stagePath.c_str(), "wb");
    if (fp) {
        size_t written = fwrite(content.data(), 1, content.size(), fp);
        fflush(fp);
        fclose(fp);
        writeOk = (written == content.size());
    }
#endif

    if (!writeOk) {
        LOG_ERROR(FileManager, "WriteTextAtomic failed to write staging file: " + stagePath);
        RemoveFile(stagePath);
        return false;
    }

    // 2. Atomic Rename Staging -> Target
    std::error_code ec;
    auto nativeStage = Utf8ToNativePath(stagePath);
    auto nativeTarget = Utf8ToNativePath(targetPath);

#if defined(_WIN32)
    // MoveFileExW with MOVEFILE_REPLACE_EXISTING guarantees atomic replacement on Windows
    if (!MoveFileExW(nativeStage.wstring().c_str(), nativeTarget.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        LOG_ERROR(FileManager, "WriteTextAtomic MoveFileExW failed for '" + targetPath + "' | Win32 Error: " + std::to_string(err));
        RemoveFile(stagePath);
        return false;
    }
#else
    std::filesystem::rename(nativeStage, nativeTarget, ec);
    if (ec) {
        LOG_ERROR(FileManager, "WriteTextAtomic rename failed: " + stagePath + " -> " + targetPath + " | " + ec.message());
        RemoveFile(stagePath);
        return false;
    }
#endif

    return true;
}

bool FileManager::WriteBinaryAtomic(const std::string& targetPath, const std::vector<uint8_t>& buffer) {
    if (targetPath.empty()) {
        LOG_ERROR(FileManager, "WriteBinaryAtomic rejected: Target path is empty");
        return false;
    }

    std::string parentDir = GetParentPath(targetPath);
    if (!parentDir.empty() && !CreateDirectories(parentDir)) {
        LOG_ERROR(FileManager, "WriteBinaryAtomic failed: Cannot create parent directory: " + parentDir);
        return false;
    }

    std::string stagePath = GenerateStagingPath(targetPath);

    bool writeOk = false;
#if defined(_WIN32)
    FILE* fp = _wfopen(Utf8ToNativePath(stagePath).wstring().c_str(), L"wb");
    if (fp) {
        size_t written = 0;
        if (!buffer.empty()) {
            written = fwrite(buffer.data(), 1, buffer.size(), fp);
        }
        fflush(fp);
        fclose(fp);
        writeOk = (written == buffer.size());
    }
#else
    FILE* fp = fopen(stagePath.c_str(), "wb");
    if (fp) {
        size_t written = 0;
        if (!buffer.empty()) {
            written = fwrite(buffer.data(), 1, buffer.size(), fp);
        }
        fflush(fp);
        fclose(fp);
        writeOk = (written == buffer.size());
    }
#endif

    if (!writeOk) {
        LOG_ERROR(FileManager, "WriteBinaryAtomic failed to write staging buffer: " + stagePath);
        RemoveFile(stagePath);
        return false;
    }

    // Atomic swap
    auto nativeStage = Utf8ToNativePath(stagePath);
    auto nativeTarget = Utf8ToNativePath(targetPath);

#if defined(_WIN32)
    if (!MoveFileExW(nativeStage.wstring().c_str(), nativeTarget.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        LOG_ERROR(FileManager, "WriteBinaryAtomic MoveFileExW failed for '" + targetPath + "' | Win32 Error: " + std::to_string(err));
        RemoveFile(stagePath);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(nativeStage, nativeTarget, ec);
    if (ec) {
        LOG_ERROR(FileManager, "WriteBinaryAtomic rename failed: " + stagePath + " -> " + targetPath + " | " + ec.message());
        RemoveFile(stagePath);
        return false;
    }
#endif

    return true;
}

bool FileManager::ReadText(const std::string& filePath, std::string& outContent) {
    std::vector<uint8_t> buffer;
    if (!ReadBinary(filePath, buffer)) {
        return false;
    }
    outContent.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    return true;
}

bool FileManager::ReadBinary(const std::string& filePath, std::vector<uint8_t>& outBuffer) {
    outBuffer.clear();
    if (filePath.empty()) return false;

    // 1. Primary path: Use SDL_LoadFile to transparently support both physical disk paths
    // and Android internal APK-bundled assets (e.g. assets/icons/icon.svg)
    size_t dataSize = 0;
    void* rawData = SDL_LoadFile(filePath.c_str(), &dataSize);
    if (rawData) {
        outBuffer.resize(dataSize);
        if (dataSize > 0) {
            std::memcpy(outBuffer.data(), rawData, dataSize);
        }
        SDL_free(rawData);
        return true;
    }

#if defined(_WIN32)
    // 2. Windows Fallback: If SDL_LoadFile encountered a wide Unicode path issue,
    // open directly via _wfopen
    FILE* fp = _wfopen(Utf8ToNativePath(filePath).wstring().c_str(), L"rb");
    if (fp) {
        _fseeki64(fp, 0, SEEK_END);
        int64_t sz = _ftelli64(fp);
        _fseeki64(fp, 0, SEEK_SET);

        if (sz >= 0) {
            outBuffer.resize(static_cast<size_t>(sz));
            if (sz > 0) {
                fread(outBuffer.data(), 1, static_cast<size_t>(sz), fp);
            }
            fclose(fp);
            return true;
        }
        fclose(fp);
    }
#endif

    LOG_ERROR(FileManager, "ReadBinary failed to open or read file: " + filePath);
    return false;
}

SDL_IOStream* FileManager::OpenReadStream(const std::string& filePath) {
    if (filePath.empty()) return nullptr;
    return SDL_IOFromFile(filePath.c_str(), "rb");
}

// =========================================================================================
// 4. FILESYSTEM QUERIES & MUTATIONS
// =========================================================================================

bool FileManager::Exists(const std::string& path) {
    if (path.empty()) return false;

    // Check disk filesystem
    std::error_code ec;
    auto nativeP = Utf8ToNativePath(path);
    if (std::filesystem::exists(nativeP, ec)) {
        return true;
    }

    // Check virtual package asset stream (Android APK bundled assets)
    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb");
    if (stream) {
        SDL_CloseIO(stream);
        return true;
    }
    return false;
}

bool FileManager::IsDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_directory(Utf8ToNativePath(path), ec);
}

bool FileManager::IsRegularFile(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(Utf8ToNativePath(path), ec);
}

bool FileManager::AreEquivalent(const std::string& pathA, const std::string& pathB) {
    if (pathA.empty() || pathB.empty()) return false;
    if (NormalizeSeparators(pathA) == NormalizeSeparators(pathB)) return true;

    std::error_code ec;
    auto nativeA = Utf8ToNativePath(pathA);
    auto nativeB = Utf8ToNativePath(pathB);

    if (!std::filesystem::exists(nativeA, ec) || !std::filesystem::exists(nativeB, ec)) {
        return false;
    }
    return std::filesystem::equivalent(nativeA, nativeB, ec);
}

bool FileManager::CreateDirectories(const std::string& dirPath) {
    if (dirPath.empty()) return false;
    std::error_code ec;
    auto nativeP = Utf8ToNativePath(dirPath);
    if (std::filesystem::exists(nativeP, ec)) {
        return true;
    }
    bool ok = std::filesystem::create_directories(nativeP, ec);
    if (ec) {
        LOG_ERROR(FileManager, "Failed to create directories at: " + dirPath + " | " + ec.message());
        return false;
    }
    return ok || std::filesystem::exists(nativeP, ec);
}

bool FileManager::RemoveFile(const std::string& filePath) {
    if (filePath.empty()) return false;
    std::error_code ec;
    auto nativeP = Utf8ToNativePath(filePath);
    bool removed = std::filesystem::remove(nativeP, ec);
    if (ec) {
        LOG_WARN(FileManager, "RemoveFile failed for: " + filePath + " | " + ec.message());
    }
    return removed;
}

bool FileManager::RemoveDirectoryRecursive(const std::string& dirPath) {
    if (dirPath.empty()) return false;
    std::error_code ec;
    auto nativeP = Utf8ToNativePath(dirPath);
    uintmax_t count = std::filesystem::remove_all(nativeP, ec);
    if (ec) {
        LOG_ERROR(FileManager, "RemoveDirectoryRecursive failed for: " + dirPath + " | " + ec.message());
        return false;
    }
    return count > 0;
}

bool FileManager::CopySingleFile(const std::string& sourcePath, const std::string& destinationPath, bool overwrite) {
    std::error_code ec;
    auto srcNative = Utf8ToNativePath(sourcePath);
    auto dstNative = Utf8ToNativePath(destinationPath);

    std::string parentDir = GetParentPath(destinationPath);
    if (!parentDir.empty()) {
        CreateDirectories(parentDir);
    }

    auto options = overwrite ? std::filesystem::copy_options::overwrite_existing 
                             : std::filesystem::copy_options::skip_existing;
    bool ok = std::filesystem::copy_file(srcNative, dstNative, options, ec);
    if (ec) {
        LOG_ERROR(FileManager, "CopySingleFile failed: " + sourcePath + " -> " + destinationPath + " | " + ec.message());
        return false;
    }
    return ok;
}

bool FileManager::CopyDirectoryRecursive(const std::string& sourceDir, const std::string& destinationDir) {
    std::error_code ec;
    auto srcNative = Utf8ToNativePath(sourceDir);
    auto dstNative = Utf8ToNativePath(destinationDir);

    if (!CreateDirectories(destinationDir)) {
        return false;
    }

    std::filesystem::copy(
        srcNative, 
        dstNative, 
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, 
        ec
    );

    if (ec) {
        LOG_ERROR(FileManager, "CopyDirectoryRecursive failed: " + sourceDir + " -> " + destinationDir + " | " + ec.message());
        return false;
    }
    return true;
}

bool FileManager::Move(const std::string& sourcePath, const std::string& destinationPath) {
    std::error_code ec;
    auto srcNative = Utf8ToNativePath(sourcePath);
    auto dstNative = Utf8ToNativePath(destinationPath);

    std::string parentDir = GetParentPath(destinationPath);
    if (!parentDir.empty()) {
        CreateDirectories(parentDir);
    }

    // 1. Attempt instantaneous atomic filesystem rename
    std::filesystem::rename(srcNative, dstNative, ec);
    if (!ec) {
        return true;
    }

    // 2. Cross-Volume Fallback:
    // If source and destination reside on different drives/partitions (e.g. EXDEV error on POSIX),
    // execute transactional copy followed by source deletion.
    LOG_INFO(FileManager, "Atomic rename across volumes failed ('" + ec.message() + "'). Initiating fallback copy+delete...");
    ec.clear();

    if (std::filesystem::is_directory(srcNative, ec)) {
        if (!CopyDirectoryRecursive(sourcePath, destinationPath)) {
            RemoveDirectoryRecursive(destinationPath); // Rollback
            return false;
        }
        RemoveDirectoryRecursive(sourcePath);
    } else {
        if (!CopySingleFile(sourcePath, destinationPath, true)) {
            RemoveFile(destinationPath); // Rollback
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
    auto nativeP = Utf8ToNativePath(dirPath);
    if (!std::filesystem::exists(nativeP, ec) || !std::filesystem::is_directory(nativeP, ec)) {
        return results;
    }

    auto iter = std::filesystem::directory_iterator(nativeP, ec);
    if (ec) {
        LOG_ERROR(FileManager, "ListEntries failed to iterate: " + dirPath + " | " + ec.message());
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
        FileEntry fe;
        fe.fullPath = NormalizeSeparators(NativePathToUtf8(entry.path()));
        fe.fileName = NativePathToUtf8(entry.path().filename());
        fe.stem = NativePathToUtf8(entry.path().stem());
        fe.extension = GetExtension(fe.fullPath);

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

        if (!filter.empty() && fe.extension != filter) {
            continue;
        }

        results.push_back(fe);
    }

    return results;
}

std::vector<std::string> FileManager::ListDirectories(const std::string& dirPath) {
    std::vector<std::string> results;
    if (dirPath.empty()) return results;

    std::error_code ec;
    auto nativeP = Utf8ToNativePath(dirPath);
    if (!std::filesystem::exists(nativeP, ec) || !std::filesystem::is_directory(nativeP, ec)) {
        return results;
    }

    auto iter = std::filesystem::directory_iterator(nativeP, ec);
    if (ec) return results;

    for (const auto& entry : iter) {
        if (entry.is_directory(ec)) {
            results.push_back(NormalizeSeparators(NativePathToUtf8(entry.path())));
        }
    }
    return results;
}

uint64_t FileManager::GetFileSize(const std::string& filePath) {
    if (filePath.empty()) return 0;
    std::error_code ec;
    auto nativeP = Utf8ToNativePath(filePath);
    if (std::filesystem::is_regular_file(nativeP, ec)) {
        return std::filesystem::file_size(nativeP, ec);
    }
    return 0;
}

// =========================================================================================
// 5. DIAGNOSTICS & HASHING (FNV-1a 64-BIT ALGORITHM)
// =========================================================================================

bool FileManager::ComputeFileHash64(const std::string& filePath, uint64_t& outHash, uint64_t& outSize) {
    // 64-bit FNV-1a Initial Parameters
    outHash = 14695981039346656037ULL; // Offset basis (0xcbf29ce484222325)
    outSize = 0;

    SDL_IOStream* stream = OpenReadStream(filePath);
    if (stream) {
        uint8_t buffer[65536];
        size_t bytesRead = 0;
        while ((bytesRead = SDL_ReadIO(stream, buffer, sizeof(buffer))) > 0) {
            outSize += bytesRead;
            for (size_t i = 0; i < bytesRead; ++i) {
                outHash ^= buffer[i];
                outHash *= 1099511628211ULL; // FNV prime (0x100000001b3)
            }
        }
        SDL_CloseIO(stream);
        return true;
    }

#if defined(_WIN32)
    // Windows Unicode wide-character file stream fallback
    FILE* fp = _wfopen(Utf8ToNativePath(filePath).wstring().c_str(), L"rb");
    if (fp) {
        uint8_t buffer[65536];
        size_t bytesRead = 0;
        while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            outSize += bytesRead;
            for (size_t i = 0; i < bytesRead; ++i) {
                outHash ^= buffer[i];
                outHash *= 1099511628211ULL;
            }
        }
        fclose(fp);
        return true;
    }
#endif

    return false;
}

int FileManager::DetectPdfPageCount(const std::string& filePath) {
    SDL_IOStream* stream = OpenReadStream(filePath);
    if (!stream) return 1;

    // 1. Verify standard '%PDF-' magic bytes
    char header[8] = {0};
    if (SDL_ReadIO(stream, header, 5) < 5 || std::memcmp(header, "%PDF-", 5) != 0) {
        SDL_CloseIO(stream);
        return 1;
    }

    Sint64 fileSize = SDL_GetIOSize(stream);
    if (fileSize <= 0) {
        SDL_CloseIO(stream);
        return 1;
    }

    // 2. Scan first 4KB for Linearized parameter dictionary (/Linearized ... /N <count>)
    size_t headScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 4096));
    SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
    std::vector<char> headBuf(headScanSize + 1, 0);
    SDL_ReadIO(stream, headBuf.data(), headScanSize);
    std::string headStr(headBuf.data(), headScanSize);

    size_t linPos = headStr.find("/Linearized");
    if (linPos != std::string::npos) {
        size_t nPos = headStr.find("/N", linPos);
        if (nPos != std::string::npos) {
            size_t numStart = nPos + 2;
            while (numStart < headStr.size() && (headStr[numStart] == ' ' || headStr[numStart] == '\t' || headStr[numStart] == '\r' || headStr[numStart] == '\n')) {
                numStart++;
            }
            if (numStart < headStr.size() && std::isdigit(static_cast<unsigned char>(headStr[numStart]))) {
                try {
                    int linCount = std::stoi(headStr.substr(numStart, 10));
                    if (linCount > 0) {
                        SDL_CloseIO(stream);
                        return linCount;
                    }
                } catch (...) {}
            }
        }
    }

    // 3. Scan trailing 64KB for /Type /Pages /Count
    size_t tailScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 65536));
    SDL_SeekIO(stream, fileSize - tailScanSize, SDL_IO_SEEK_SET);
    std::vector<char> buffer(tailScanSize + 1, 0);
    SDL_ReadIO(stream, buffer.data(), tailScanSize);

    std::string tailStr(buffer.data(), tailScanSize);
    int maxPagesFound = 0;
    size_t countPos = 0;

    while ((countPos = tailStr.find("/Count", countPos)) != std::string::npos) {
        size_t ctxStart = (countPos >= 80) ? countPos - 80 : 0;
        size_t ctxEnd = std::min(tailStr.size(), countPos + 80);
        std::string ctx = tailStr.substr(ctxStart, ctxEnd - ctxStart);

        // Filter out outline bookmark items containing /Title or /Dest
        bool isOutline = (ctx.find("/Title") != std::string::npos || ctx.find("/Dest") != std::string::npos);
        bool isPagesNode = (ctx.find("/Pages") != std::string::npos);

        if (!isOutline && isPagesNode) {
            size_t numStart = countPos + 6;
            while (numStart < tailStr.size() && (tailStr[numStart] == ' ' || tailStr[numStart] == '\t' || tailStr[numStart] == '\r' || tailStr[numStart] == '\n')) {
                numStart++;
            }
            if (numStart < tailStr.size() && std::isdigit(static_cast<unsigned char>(tailStr[numStart]))) {
                try {
                    int countVal = std::stoi(tailStr.substr(numStart, 10));
                    if (countVal > maxPagesFound) {
                        maxPagesFound = countVal;
                    }
                } catch (...) {}
            }
        }
        countPos += 6;
    }

    if (maxPagesFound > 0) {
        SDL_CloseIO(stream);
        return maxPagesFound;
    }

    // 4. Fallback sweep: count individual "/Type /Page" tokens
    SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
    int pageTokenCount = 0;
    std::vector<char> chunk(32768, 0);
    std::string carry = "";
    while (true) {
        size_t readCount = SDL_ReadIO(stream, chunk.data(), chunk.size());
        if (readCount == 0) break;
        std::string block = carry + std::string(chunk.data(), readCount);
        size_t p = 0;
        while ((p = block.find("/Type", p)) != std::string::npos) {
            size_t afterType = p + 5;
            while (afterType < block.size() && (block[afterType] == ' ' || block[afterType] == '\t' || block[afterType] == '\r' || block[afterType] == '\n')) afterType++;
            if (block.compare(afterType, 5, "/Page") == 0) {
                if (afterType + 5 < block.size() && block[afterType + 5] != 's') {
                    pageTokenCount++;
                }
            }
            p += 5;
        }
        carry = (block.size() > 64) ? block.substr(block.size() - 64) : block;
    }

    SDL_CloseIO(stream);
    return pageTokenCount > 0 ? pageTokenCount : 1;
}

} // namespace Folio
