/**
 * =========================================================================================
 * @file io/file_manager.cpp
 * @brief Implementation of Cross-Platform Filesystem & Atomic I/O Operations for FolioNote
 * =========================================================================================
 */

#include "io/file_manager.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <io.h>     // For _commit and _fileno
#else
#include <unistd.h> // For fsync and fileno
#endif

namespace Folio {

namespace {

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

} // anonymous namespace

// =========================================================================================
// 1. STANDARD APPLICATION DIRECTORIES (PLATFORM-AWARE)
// =========================================================================================

std::string FileManager::GetAppRootDirectory(const std::string& overridePath) {
    if (!overridePath.empty()) {
        return NormalizeSeparators(overridePath);
    }

#if defined(__ANDROID__)
    const char* pref = SDL_GetPrefPath("UniversalFramework", "FolioNote");
    if (pref && pref[0] != '\0') {
        return NormalizeSeparators(std::string(pref));
    }
    return NormalizeSeparators("./FolioNote");
#else
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

std::string FileManager::GetBackupsDirectory() {
    return JoinPath(GetAppRootDirectory(), "backups");
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
    while (normalized.size() > 1 && normalized.back() == '/') {
        if (normalized.size() == 3 && normalized[1] == ':') break;
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
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<'  || c == '>' || c == '|' ||
            static_cast<unsigned char>(c) < 32) {
            c = replacement;
        }
    }

    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) {
        safe.pop_back();
    }
    return safe.empty() ? "Untitled" : safe;
}

bool FileManager::IsAbsolutePath(const std::string& path) {
    if (path.empty()) return false;
    // Delegate to std::filesystem which handles:
    //   Windows: drive letters ("C:\"), UNC ("\\server\share\"), rooted ("\")
    //   POSIX: Unix root ("/")
    return Utf8ToNativePath(path).is_absolute();
}

std::string FileManager::PathToFileUri(const std::string& path) {
    if (path.empty()) return "";

    // Already an absolute URI
    if (path.rfind("file://", 0) == 0 ||
        path.rfind("http://", 0) == 0 ||
        path.rfind("https://", 0) == 0) {
        return path;
    }

#if defined(_WIN32)
    std::string normalized = NormalizeSeparators(path);
    if (normalized.size() > 1 && normalized[1] == ':') {
        return "file:///" + normalized;
    }
    return "file://" + normalized;
#else
    return "file://" + path;
#endif
}

std::string FileManager::ShowOpenFileDialog(const std::string& title) {
#if defined(_WIN32)
    wchar_t fileBuf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    std::filesystem::path nativeTitle = Utf8ToNativePath(title);
    if (!title.empty()) {
        ofn.lpstrTitle = nativeTitle.c_str();
    }

    if (GetOpenFileNameW(&ofn)) {
        return NormalizeSeparators(NativePathToUtf8(fileBuf));
    }
    return "";
#else
    (void)title;
    return "";
#endif
}

bool FileManager::OpenWithDefaultApp(const std::string& pathOrUrl) {
    // Centralized guard: reject empty paths with an explicit error code
    if (pathOrUrl.empty()) {
        LOG_WARN_CODE(FileManager, FolioErrorCode::SysFileNotFound, "OpenWithDefaultApp rejected: path/URL string is empty.");
        return false;
    }

    std::string targetUri = PathToFileUri(pathOrUrl);

    // Cross-platform dispatch via SDL3 (Windows, macOS, Linux, Android)
    if (SDL_OpenURL(targetUri.c_str())) {
        return true;
    }

    LOG_WARN(FileManager, "SDL_OpenURL failed for (" + targetUri + "): " + SDL_GetError());

#if defined(_WIN32)
    // Native Win32 fallback via ShellExecuteW
    std::filesystem::path nativeP = Utf8ToNativePath(pathOrUrl);
    HINSTANCE res = ShellExecuteW(nullptr, L"open", nativeP.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(res) > 32) {
        return true;
    }
    LOG_ERROR_CODE(FileManager, FolioErrorCode::SysFileAccessDenied, "ShellExecuteW fallback also failed for: " + pathOrUrl);
#else
    LOG_ERROR_CODE(FileManager, FolioErrorCode::SysFileAccessDenied, "Failed to launch default application for: " + pathOrUrl);
#endif

    return false;
}

bool FileManager::SetClipboardText(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    return SDL_SetClipboardText(text.c_str()) == 0;
}

std::string FileManager::GetClipboardText() {
    if (!SDL_HasClipboardText()) {
        return "";
    }
    char* clip = SDL_GetClipboardText();
    if (!clip) {
        return "";
    }
    std::string result(clip);
    SDL_free(clip);
    return result;
}

bool FileManager::HasClipboardText() {
    return SDL_HasClipboardText();
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
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysPathResolutionFailed, "WriteTextAtomic rejected: Target path is empty"));
        return false;
    }

    std::string parentDir = GetParentPath(targetPath);
    if (!parentDir.empty() && !CreateDirectories(parentDir)) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDirectoryCreateFailed, "WriteTextAtomic failed: Cannot create parent directory: " + parentDir));
        return false;
    }

    std::string stagePath = GenerateStagingPath(targetPath);

    bool writeOk = false;
#if defined(_WIN32)
    FILE* fp = _wfopen(Utf8ToNativePath(stagePath).wstring().c_str(), L"wb");
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

    if (!writeOk) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, "WriteTextAtomic failed to write staging file: " + stagePath));
        RemoveFile(stagePath);
        return false;
    }

    auto nativeStage = Utf8ToNativePath(stagePath);
    auto nativeTarget = Utf8ToNativePath(targetPath);

#if defined(_WIN32)
    if (!MoveFileExW(nativeStage.wstring().c_str(), nativeTarget.wstring().c_str(), 
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileRenameFailed, 
            "WriteTextAtomic MoveFileExW failed for '" + targetPath + "' | Win32 Error: " + std::to_string(err)));
        RemoveFile(stagePath);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(nativeStage, nativeTarget, ec);
    if (ec) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileRenameFailed, 
            "WriteTextAtomic rename failed: " + stagePath + " -> " + targetPath + " | " + ec.message()));
        RemoveFile(stagePath);
        return false;
    }
#endif

    return true;
}

bool FileManager::WriteBinaryAtomic(const std::string& targetPath, const std::vector<uint8_t>& buffer) {
    if (targetPath.empty()) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysPathResolutionFailed, "WriteBinaryAtomic rejected: Target path is empty"));
        return false;
    }

    std::string parentDir = GetParentPath(targetPath);
    if (!parentDir.empty() && !CreateDirectories(parentDir)) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDirectoryCreateFailed, "WriteBinaryAtomic failed: Cannot create parent directory: " + parentDir));
        return false;
    }

    std::string stagePath = GenerateStagingPath(targetPath);

    bool writeOk = false;
#if defined(_WIN32)
    FILE* fp = _wfopen(Utf8ToNativePath(stagePath).wstring().c_str(), L"wb");
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

    if (!writeOk) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, "WriteBinaryAtomic failed to write staging buffer: " + stagePath));
        RemoveFile(stagePath);
        return false;
    }

    auto nativeStage = Utf8ToNativePath(stagePath);
    auto nativeTarget = Utf8ToNativePath(targetPath);

#if defined(_WIN32)
    if (!MoveFileExW(nativeStage.wstring().c_str(), nativeTarget.wstring().c_str(), 
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileRenameFailed, 
            "WriteBinaryAtomic MoveFileExW failed for '" + targetPath + "' | Win32 Error: " + std::to_string(err)));
        RemoveFile(stagePath);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(nativeStage, nativeTarget, ec);
    if (ec) {
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileRenameFailed, 
            "WriteBinaryAtomic rename failed: " + stagePath + " -> " + targetPath + " | " + ec.message()));
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

    LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileReadFailed, "ReadBinary failed to open or read file: " + filePath));
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

    std::error_code ec;
    auto nativeP = Utf8ToNativePath(path);
    if (std::filesystem::exists(nativeP, ec)) {
        return true;
    }

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
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysDirectoryCreateFailed, 
            "Failed to create directories at: " + dirPath + " | " + ec.message()));
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
        // Warning: Non-catastrophic, simple descriptive string
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
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileDeleteFailed, 
            "RemoveDirectoryRecursive failed for: " + dirPath + " | " + ec.message()));
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
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, 
            "CopySingleFile failed: " + sourcePath + " -> " + destinationPath + " | " + ec.message()));
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
        LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileWriteFailed, 
            "CopyDirectoryRecursive failed: " + sourceDir + " -> " + destinationDir + " | " + ec.message()));
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
    auto nativeP = Utf8ToNativePath(dirPath);
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
            std::string entryExt = NativePathToUtf8(entry.path().extension());
            std::transform(entryExt.begin(), entryExt.end(), entryExt.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (entryExt != filter) {
                continue;
            }
        }

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

        results.push_back(std::move(fe));
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
    outHash = 14695981039346656037ULL;
    outSize = 0;

    SDL_IOStream* stream = OpenReadStream(filePath);
    if (stream) {
        uint8_t buffer[65536];
        size_t bytesRead = 0;
        while ((bytesRead = SDL_ReadIO(stream, buffer, sizeof(buffer))) > 0) {
            outSize += bytesRead;
            for (size_t i = 0; i < bytesRead; ++i) {
                outHash ^= buffer[i];
                outHash *= 1099511628211ULL;
            }
        }
        SDL_CloseIO(stream);
        return true;
    }

#if defined(_WIN32)
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

    size_t headScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 4096));
    SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
    std::vector<char> headBuf(headScanSize + 1, 0);
    SDL_ReadIO(stream, headBuf.data(), headScanSize);
    std::string_view headView(headBuf.data(), headScanSize);

    size_t linPos = headView.find("/Linearized");
    if (linPos != std::string_view::npos) {
        size_t nPos = headView.find("/N", linPos);
        if (nPos != std::string_view::npos) {
            size_t numStart = nPos + 2;
            while (numStart < headView.size() && (headView[numStart] == ' ' || headView[numStart] == '\t' || headView[numStart] == '\r' || headView[numStart] == '\n')) {
                numStart++;
            }
            if (numStart < headView.size() && std::isdigit(static_cast<unsigned char>(headView[numStart]))) {
                try {
                    int linCount = std::stoi(std::string(headView.substr(numStart, 10)));
                    if (linCount > 0) {
                        SDL_CloseIO(stream);
                        return linCount;
                    }
                } catch (...) {}
            }
        }
    }

    size_t tailScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 65536));
    SDL_SeekIO(stream, fileSize - tailScanSize, SDL_IO_SEEK_SET);
    std::vector<char> buffer(tailScanSize + 1, 0);
    SDL_ReadIO(stream, buffer.data(), tailScanSize);

    std::string_view tailView(buffer.data(), tailScanSize);
    int maxPagesFound = 0;
    size_t countPos = 0;

    while ((countPos = tailView.find("/Count", countPos)) != std::string_view::npos) {
        size_t ctxStart = (countPos >= 80) ? countPos - 80 : 0;
        size_t ctxEnd = std::min(tailView.size(), countPos + 80);
        std::string_view ctx = tailView.substr(ctxStart, ctxEnd - ctxStart);

        bool isOutline = (ctx.find("/Title") != std::string_view::npos || ctx.find("/Dest") != std::string_view::npos);
        bool isPagesNode = (ctx.find("/Pages") != std::string_view::npos);

        if (!isOutline && isPagesNode) {
            size_t numStart = countPos + 6;
            while (numStart < tailView.size() && (tailView[numStart] == ' ' || tailView[numStart] == '\t' || tailView[numStart] == '\r' || tailView[numStart] == '\n')) {
                numStart++;
            }
            if (numStart < tailView.size() && std::isdigit(static_cast<unsigned char>(tailView[numStart]))) {
                try {
                    int countVal = std::stoi(std::string(tailView.substr(numStart, 10)));
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

    SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
    int pageTokenCount = 0;
    constexpr size_t CHUNK_SIZE = 32768;
    std::vector<char> chunk(CHUNK_SIZE + 64, 0);
    size_t carrySize = 0;

    while (true) {
        size_t readCount = SDL_ReadIO(stream, chunk.data() + carrySize, CHUNK_SIZE);
        size_t totalBytes = carrySize + readCount;
        if (totalBytes == 0) break;

        std::string_view block(chunk.data(), totalBytes);
        size_t p = 0;
        while ((p = block.find("/Type", p)) != std::string_view::npos) {
            size_t afterType = p + 5;
            while (afterType < block.size() && (block[afterType] == ' ' || block[afterType] == '\t' || block[afterType] == '\r' || block[afterType] == '\n')) {
                afterType++;
            }
            if (block.substr(afterType).starts_with("/Page")) {
                if (afterType + 5 < block.size() && block[afterType + 5] != 's') {
                    pageTokenCount++;
                }
            }
            p += 5;
        }

        if (readCount == 0) break;

        carrySize = std::min<size_t>(totalBytes, 64);
        std::memmove(chunk.data(), chunk.data() + totalBytes - carrySize, carrySize);
    }

    SDL_CloseIO(stream);
    return pageTokenCount > 0 ? pageTokenCount : 1;
}

} // namespace Folio