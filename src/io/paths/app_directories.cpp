#include "io/app_directories.hpp"
#include "io/path_utils.hpp"
#include "utils/logger.hpp"

#include <mutex>
#include <filesystem>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace Folio {

namespace {
std::mutex s_packageRootMutex;
std::string s_activePackageRoot;
std::mutex s_appRootMutex;
std::string s_activeAppRoot;
std::mutex s_localDataMutex;
std::string s_activeLocalDataRoot;
} // anonymous namespace

void AppDirectories::SetAppRootDirectory(const std::string& root) {
    std::lock_guard<std::mutex> lock(s_appRootMutex);
    s_activeAppRoot = PathUtils::NormalizeSeparators(root);
}

std::string AppDirectories::GetAppRootDirectory(const std::string& overridePath) {
    if (!overridePath.empty()) {
        return PathUtils::NormalizeSeparators(overridePath);
    }

    {
        std::lock_guard<std::mutex> lock(s_appRootMutex);
        if (!s_activeAppRoot.empty()) {
            return s_activeAppRoot;
        }
    }

#if defined(__ANDROID__)
    // CRITICAL: On Android, SDL_GetPrefPath() uses JNI (context.getFilesDir()).
    // Calling it before SDL is initialized (SDL_WasInit) or during static initialization
    // causes a fatal SIGSEGV in ART JNI because the JVM environment is unattached.
    // Guard with SDL_WasInit to ensure JNI is initialized before querying.
    if (SDL_WasInit(0) != 0) {
        char* pref = SDL_GetPrefPath("UniversalFramework", "FolioNote");
        if (pref && pref[0] != '\0') {
            std::string res = PathUtils::NormalizeSeparators(std::string(pref));
            SDL_free(pref);
            std::lock_guard<std::mutex> lock(s_appRootMutex);
            s_activeAppRoot = res;
            return res;
        }
        if (pref) {
            SDL_free(pref);
        }
    }
    return PathUtils::NormalizeSeparators("./FolioNote");
#else
    const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    if (docs && docs[0] != '\0') {
        std::filesystem::path appRoot = PathUtils::Utf8ToNativePath(docs) / "FolioNote";
        std::string res = PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(appRoot));
        std::lock_guard<std::mutex> lock(s_appRootMutex);
        s_activeAppRoot = res;
        return res;
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

/**
 * @brief Resolves the dedicated, local-only application data directory deep in the system file system.
 *
 * GENERAL WORKING PROCESS & ARCHITECTURAL MOTIVATION:
 * 1. Isolation from Cloud Services (OneDrive, Dropbox, iCloud):
 *    User documents (active notebooks) live in the Documents folder so they can sync to the cloud if the user desires.
 *    However, backups, ephemeral caches, and disaster recovery snapshots must NEVER be placed in a cloud-synced folder
 *    because doing so triggers continuous upload churn, eats storage quotas, causes file-locking collisions,
 *    and fails when encountering dehydrated cloud reparse points.
 * 2. Platform-Specific Resolution:
 *    - Windows: Uses SHGetKnownFolderPath with FOLDERID_LocalAppData (%LOCALAPPDATA%/FolioNote).
 *      Guaranteed to be local machine storage excluded from OneDrive synchronization.
 *    - Android: Resolves via SDL_GetPrefPath to internal app sandboxed storage.
 *    - Linux/macOS: Uses SDL_FOLDER_LOCAL_APP_DATA or SDL_FOLDER_APPDATA (e.g. ~/.local/share/FolioNote).
 *
 * @return Canonical normalized UTF-8 filesystem path to local-only app data.
 */
std::string AppDirectories::GetLocalDataDirectory() {
    {
        std::lock_guard<std::mutex> lock(s_localDataMutex);
        if (!s_activeLocalDataRoot.empty()) {
            return s_activeLocalDataRoot;
        }
    }

#if defined(_WIN32)
    PWSTR localAppPathW = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppPathW))) {
        std::filesystem::path localRoot = std::filesystem::path(localAppPathW) / "FolioNote";
        CoTaskMemFree(localAppPathW);
        std::string res = PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(localRoot));
        std::lock_guard<std::mutex> lock(s_localDataMutex);
        s_activeLocalDataRoot = res;
        return res;
    }
#elif defined(__ANDROID__)
    if (SDL_WasInit(0) != 0) {
        char* pref = SDL_GetPrefPath("UniversalFramework", "FolioNote");
        if (pref && pref[0] != '\0') {
            std::string res = PathUtils::NormalizeSeparators(std::string(pref));
            SDL_free(pref);
            std::lock_guard<std::mutex> lock(s_localDataMutex);
            s_activeLocalDataRoot = res;
            return res;
        }
        if (pref) SDL_free(pref);
    }
#else
    const char* appData = SDL_GetUserFolder(SDL_FOLDER_LOCAL_APP_DATA);
    if (!appData) appData = SDL_GetUserFolder(SDL_FOLDER_APPDATA);
    if (appData && appData[0] != '\0') {
        std::filesystem::path localRoot = PathUtils::Utf8ToNativePath(appData) / "FolioNote";
        std::string res = PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(localRoot));
        std::lock_guard<std::mutex> lock(s_localDataMutex);
        s_activeLocalDataRoot = res;
        return res;
    }
#endif

    std::string fallback = PathUtils::JoinPath(GetAppRootDirectory(), "local");
    std::lock_guard<std::mutex> lock(s_localDataMutex);
    s_activeLocalDataRoot = fallback;
    return fallback;
}

std::string AppDirectories::GetCacheDirectory() {
    return PathUtils::JoinPath(GetLocalDataDirectory(), "cache");
}

std::string AppDirectories::GetExportsDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "exports");
}

std::string AppDirectories::GetLogsDirectory() {
    return PathUtils::JoinPath(GetAppRootDirectory(), "logs");
}

std::string AppDirectories::GetBackupsDirectory() {
    return PathUtils::JoinPath(GetLocalDataDirectory(), "backups");
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
