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
     * @brief Resolves the backup snapshot directory (<AppRoot>/backups).
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
