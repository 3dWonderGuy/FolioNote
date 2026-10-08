#include "io/app_directories.hpp"
#include "io/path_utils.hpp"
#include "utils/logger.hpp"

#include <mutex>
#include <filesystem>
#include <SDL3/SDL.h>

namespace Folio {

namespace {
std::mutex s_packageRootMutex;
std::string s_activePackageRoot;
} // anonymous namespace

std::string AppDirectories::GetAppRootDirectory(const std::string& overridePath) {
    if (!overridePath.empty()) {
        return PathUtils::NormalizeSeparators(overridePath);
    }

#if defined(__ANDROID__)
    const char* pref = SDL_GetPrefPath("UniversalFramework", "FolioNote");
    if (pref && pref[0] != '\0') {
        return PathUtils::NormalizeSeparators(std::string(pref));
    }
    return PathUtils::NormalizeSeparators("./FolioNote");
#else
    const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    if (docs && docs[0] != '\0') {
        std::filesystem::path appRoot = PathUtils::Utf8ToNativePath(docs) / "FolioNote";
        return PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(appRoot));
    }

    LOG_WARN(FileManager, "SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS) returned null; falling back to './FolioNote'");
    return PathUtils::NormalizeSeparators("./FolioNote");
#endif
}

std::string AppDirectories::GetLibrariesDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "Libraries");
}

std::string AppDirectories::GetConfigDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "config");
}

std::string AppDirectories::GetCacheDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "cache");
}

std::string AppDirectories::GetExportsDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "exports");
}

std::string AppDirectories::GetLogsDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "logs");
}

std::string AppDirectories::GetBackupsDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "backups");
}

std::string AppDirectories::GetTempDirectory() {
    std::error_code ec;
    auto tempPath = std::filesystem::temp_directory_path(ec);
    if (ec) {
        return PathUtils::JoinPath(GetAppRootDirectory(), "cache/temp");
    }
    return PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(tempPath));
}

void AppDirectories::SetActivePackageRoot(const std::string& root) {
    std::lock_guard<std::mutex> lock(s_packageRootMutex);
    s_activePackageRoot = PathUtils::NormalizeSeparators(root);
}

std::string AppDirectories::GetActivePackageRoot() {
    std::lock_guard<std::mutex> lock(s_packageRootMutex);
    return s_activePackageRoot;
}

std::string AppDirectories::ResolveAssetPath(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    std::error_code ec;

    // 1. If path is already absolute and exists on disk, normalize and return
    if (PathUtils::IsAbsolutePath(path) && std::filesystem::exists(PathUtils::Utf8ToNativePath(path), ec)) {
        return PathUtils::NormalizeSeparators(path);
    }

    // 2. Check against the registered active notebook package root
    std::string root = GetActivePackageRoot();
    if (!root.empty()) {
        std::string candidate = PathUtils::JoinPath(root, path);
        if (std::filesystem::exists(PathUtils::Utf8ToNativePath(candidate), ec)) {
            return candidate;
        }
    }

    // 3. Fallback: check if relative to application current working directory
    if (std::filesystem::exists(PathUtils::Utf8ToNativePath(path), ec)) {
        return PathUtils::NormalizeSeparators(path);
    }

    // 4. If not physically on disk yet, return candidate joined with active package root if available
    if (!root.empty() && !PathUtils::IsAbsolutePath(path)) {
        return PathUtils::JoinPath(root, path);
    }

    return PathUtils::NormalizeSeparators(path);
}

} // namespace Folio
