#pragma once
/**
 * =========================================================================================
 * @file package_marker.hpp
 * @brief Windows Shell Package Identity and Folder Bundle Customizer for FolioNote
 * =========================================================================================
 *
 * GENERAL WORKING PROCESS & SYSTEM ARCHITECTURE:
 * ----------------------------------------------
 * In FolioNote, libraries (.foliolib) and notebooks (.notebook) are stored on the physical
 * filesystem as directory packages (bundles) housing SQLite databases, binary vector strokes,
 * and media assets.
 *
 * The Windows Shell (Explorer.exe) by default renders any filesystem directory as a generic
 * yellow folder regardless of extensions (.foliolib / .notebook) because file extension
 * associations (under HKEY_CLASSES_ROOT\.ext) only apply to FILE objects.
 *
 * To render a directory as an authentic application package with the official FolioNote
 * icon and tooltip description in Windows Explorer, the operating system requires three
 * coordinated filesystem artifacts:
 *
 * 1. Configuration Descriptor (`desktop.ini`):
 *    A system configuration file placed inside the directory containing:
 *    ```ini
 *    [.ShellClassInfo]
 *    IconResource=<PathToFolioNote.exe>,0
 *    InfoTip=<Package Description>
 *    [ViewState]
 *    Mode=
 *    Vid=
 *    FolderType=Generic
 *    ```
 *
 * 2. File and Directory Attribute Bitmasks:
 *    - `desktop.ini` attribute: `FILE_ATTRIBUTE_HIDDEN (0x02) | FILE_ATTRIBUTE_SYSTEM (0x04)`.
 *      This prevents the configuration file from cluttering user views.
 *    - Directory attribute: `FILE_ATTRIBUTE_READONLY (0x01)` (or `FILE_ATTRIBUTE_SYSTEM (0x04)`).
 *      MATHEMATICAL & BITWISE WORKING PROCESS:
 *      When Windows Explorer parses directory metadata, it checks the directory's attribute
 *      dword: `dwFileAttributes & FILE_ATTRIBUTE_READONLY`. On file objects, this bit enforces
 *      write-protection. On DIRECTORY objects, however, Win32 NTFS/FAT drivers DO NOT enforce
 *      write-protection (child files can be freely created, written, renamed, and deleted).
 *      Instead, the shell treats this bit as an explicit performance flag indicating:
 *      "This folder possesses customized desktop.ini shell configuration; parse it."
 *
 * 3. Shell Notification Broadcast:
 *    Invokes `SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, ...)` so that any active Windows
 *    Explorer windows immediately invalidate their icon cache and render the branded package icon
 *    without requiring an OS restart or manual refresh (F5).
 */

#include <string>
#include <fstream>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace Folio {

class PackageMarker {
public:
    /**
     * @brief Marks a directory as a recognized FolioNote bundle package on Windows.
     *
     * GENERAL WORKING PROCESS:
     * 1. Validates that the provided folder path exists and is a directory.
     * 2. Resolves the absolute path to the currently running FolioNote executable.
     * 3. Writes a hidden/system `desktop.ini` inside the folder specifying the executable's
     *    embedded icon (index 0) and the package description tooltip.
     * 4. Applies `FILE_ATTRIBUTE_READONLY` to the folder to instruct Explorer to load `desktop.ini`.
     * 5. Fires `SHChangeNotify` to trigger an immediate Explorer icon cache refresh.
     *
     * @param folderPath Absolute or canonical filesystem path to the target package directory.
     * @param infoTip User-facing description displayed when hovering over the folder in Explorer.
     * @return bool True if the package was successfully marked; false on failure or on non-Windows OS.
     */
    static bool MarkFolderAsPackage(const std::string& folderPath, const std::string& infoTip = "FolioNote Package") {
#if defined(_WIN32)
        if (folderPath.empty()) {
            return false;
        }

        std::error_code ec;
        std::filesystem::path dirPath = std::filesystem::u8path(folderPath);
        if (!std::filesystem::is_directory(dirPath, ec)) {
            return false;
        }

        // 1. Locate current running executable path (FolioNote.exe)
        std::wstring exePathW(MAX_PATH, L'\0');
        DWORD len = GetModuleFileNameW(NULL, &exePathW[0], static_cast<DWORD>(exePathW.size()));
        while (len >= exePathW.size()) {
            exePathW.resize(exePathW.size() * 2);
            len = GetModuleFileNameW(NULL, &exePathW[0], static_cast<DWORD>(exePathW.size()));
        }
        exePathW.resize(len);

        std::string exePathUtf8;
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, exePathW.c_str(), -1, NULL, 0, NULL, NULL);
        if (utf8Len > 0) {
            exePathUtf8.resize(utf8Len - 1);
            WideCharToMultiByte(CP_UTF8, 0, exePathW.c_str(), -1, &exePathUtf8[0], utf8Len, NULL, NULL);
        }

        // 2. Construct desktop.ini file content
        std::filesystem::path iniPath = dirPath / "desktop.ini";
        std::wstring iniPathW = iniPath.wstring();

        // If desktop.ini already exists with Hidden/System/ReadOnly flags, strip them temporarily to permit overwrite
        DWORD existingAttrs = GetFileAttributesW(iniPathW.c_str());
        if (existingAttrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(iniPathW.c_str(), FILE_ATTRIBUTE_NORMAL);
        }

        // Write desktop.ini content using ANSI/UTF-8 format accepted by Windows Shell
        std::ofstream iniFile(iniPath, std::ios::out | std::ios::trunc);
        if (!iniFile.is_open()) {
            return false;
        }

        iniFile << "[.ShellClassInfo]\r\n"
                << "IconResource=" << exePathUtf8 << ",0\r\n"
                << "InfoTip=" << (infoTip.empty() ? "FolioNote Package" : infoTip) << "\r\n"
                << "[ViewState]\r\n"
                << "Mode=\r\n"
                << "Vid=\r\n"
                << "FolderType=Generic\r\n";
        iniFile.close();

        // 3. Mark desktop.ini as HIDDEN and SYSTEM
        SetFileAttributesW(iniPathW.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        // 4. Mark the package directory as READONLY so Explorer inspects desktop.ini
        std::wstring dirPathW = dirPath.wstring();
        DWORD dirAttrs = GetFileAttributesW(dirPathW.c_str());
        if (dirAttrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(dirPathW.c_str(), dirAttrs | FILE_ATTRIBUTE_READONLY);
        }

        // 5. Broadcast shell change notification to refresh Explorer icons
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, dirPathW.c_str(), NULL);
        return true;
#else
        (void)folderPath;
        (void)infoTip;
        return true; // No-op on non-Windows platforms
#endif
    }
};

} // namespace Folio
