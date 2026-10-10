#pragma once
/**
 * =========================================================================================
 * @file io/app_directories.hpp
 * @brief Platform-aware standard application directory and package root resolver
 * =========================================================================================
 */

#include <string>

namespace Folio {

class AppDirectories {
public:
    /**
     * @brief Resolves the global application document directory root.
     */
    static std::string GetAppRootDirectory(const std::string& overridePath = "");

    /**
     * @brief Explicitly overrides and configures the global application document root directory.
     * Essential on Android once private storage sandbox path is established by the Activity.
     */
    static void SetAppRootDirectory(const std::string& root);

    /**
     * @brief Resolves the directory housing user library bundles (<AppRoot>/Libraries).
     */
    static std::string GetLibrariesDirectory();

    /**
     * @brief Resolves the configuration directory (<AppRoot>/config).
     */
    static std::string GetConfigDirectory();

    /**
     * @brief Resolves the ephemeral cache directory (<AppRoot>/cache).
     */
    static std::string GetCacheDirectory();

    /**
     * @brief Resolves the directory for exports (<AppRoot>/exports).
     */
    static std::string GetExportsDirectory();

    /**
     * @brief Resolves the diagnostic logging directory (<AppRoot>/logs).
     */
    static std::string GetLogsDirectory();

    /**
     * @brief Resolves the dedicated local application data directory.
     * Guaranteed to reside deep inside the local machine file system (e.g. %LOCALAPPDATA%/FolioNote on Windows,
     * ~/.local/share/FolioNote on Linux, or app sandbox on Android/macOS).
     * Strictly isolated from cloud synchronization services like OneDrive, Dropbox, or iCloud.
     */
    static std::string GetLocalDataDirectory();

    /**
     * @brief Resolves the backup snapshot directory (defaults to <LocalData>/backups).
     */
    static std::string GetBackupsDirectory();

    /**
     * @brief Resolves a system-wide or app-private temporary working directory.
     */
    static std::string GetTempDirectory();

    /**
     * @brief Sets the active notebook or package root directory for relative asset resolution.
     */
    static void SetActivePackageRoot(const std::string& root);

    /**
     * @brief Retrieves the currently registered active notebook package root directory.
     */
    static std::string GetActivePackageRoot();

    /**
     * @brief Resolves an asset path into an absolute physical path.
     */
    static std::string ResolveAssetPath(const std::string& path);
};

} // namespace Folio
