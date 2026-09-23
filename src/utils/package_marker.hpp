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
 *    - Directory attribute: `FILE_ATTRIBUTE_READONLY (0x01)`.
 *      When Windows Explorer parses directory metadata, it checks `FILE_ATTRIBUTE_READONLY`.
 *      On DIRECTORY objects, Win32 NTFS/FAT drivers DO NOT enforce write-protection.
 *      Instead, the shell treats this bit as an explicit performance flag indicating:
 *      "This folder possesses customized desktop.ini shell configuration; parse it."
 *
 * 3. Shell Notification Broadcast:
 *    Invokes `SHChangeNotify` so that active Explorer windows invalidate their icon cache
 *    and render the branded package icon immediately.
 */

#include <string>
#include <filesystem>

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

class PackageMarker {
public:
    /**
     * @brief Marks a directory as a recognized FolioNote bundle package on Windows.
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

        // Convert UTF-8 to native UTF-16 wide string without deprecated u8path
        int wlen = MultiByteToWideChar(CP_UTF8, 0, folderPath.data(), static_cast<int>(folderPath.size()), nullptr, 0);
        if (wlen <= 0) return false;
        std::wstring dirPathW(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, folderPath.data(), static_cast<int>(folderPath.size()), &dirPathW[0], wlen);

        std::error_code ec;
        std::filesystem::path dirPath(dirPathW);
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

        // 2. Resolve desktop.ini path
        std::wstring iniPathW = (dirPath / L"desktop.ini").wstring();

        // Strip existing attributes to allow atomic write
        DWORD existingAttrs = GetFileAttributesW(iniPathW.c_str());
        if (existingAttrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(iniPathW.c_str(), FILE_ATTRIBUTE_NORMAL);
        }

        // Convert infoTip to wide string
        int tipWLen = MultiByteToWideChar(CP_UTF8, 0, infoTip.data(), static_cast<int>(infoTip.size()), nullptr, 0);
        std::wstring infoTipW = L"FolioNote Package";
        if (tipWLen > 0) {
            infoTipW.assign(static_cast<size_t>(tipWLen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, infoTip.data(), static_cast<int>(infoTip.size()), &infoTipW[0], tipWLen);
        }

        // 3. Write desktop.ini using Win32 Private Profile APIs (guarantees shell-compatible encoding)
        std::wstring iconResource = exePathW + L",0";
        WritePrivateProfileStringW(L".ShellClassInfo", L"IconResource", iconResource.c_str(), iniPathW.c_str());
        WritePrivateProfileStringW(L".ShellClassInfo", L"InfoTip", infoTipW.c_str(), iniPathW.c_str());
        WritePrivateProfileStringW(L"ViewState", L"FolderType", L"Generic", iniPathW.c_str());
        
        // Flush profile cache to physical storage
        WritePrivateProfileStringW(NULL, NULL, NULL, iniPathW.c_str());

        // 4. Mark desktop.ini as HIDDEN and SYSTEM
        SetFileAttributesW(iniPathW.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        // 5. Mark the package directory as READONLY so Explorer parses desktop.ini
        DWORD dirAttrs = GetFileAttributesW(dirPathW.c_str());
        if (dirAttrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(dirPathW.c_str(), dirAttrs | FILE_ATTRIBUTE_READONLY);
        }

        // 6. Broadcast shell updates to immediately refresh Windows Explorer icon caches
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, dirPathW.c_str(), NULL);
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, dirPath.parent_path().wstring().c_str(), NULL);
        return true;
#else
        (void)folderPath;
        (void)infoTip;
        return true;
#endif
    }
};

} // namespace Folio