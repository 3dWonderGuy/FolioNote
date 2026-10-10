#include "io/platform/system_dialogs.hpp"
#include "io/paths/path_utils.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"

#include <vector>
#include <cstdlib>
#include <cstdio>
#include <SDL3/SDL.h>

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
#endif

namespace Folio {

std::string SystemDialogs::ShowOpenFileDialog(const std::string& title) {
#if defined(_WIN32)
    // 32KB dynamic wide character buffer prevents MAX_PATH buffer overflows on deep hierarchies
    std::vector<wchar_t> fileBuf(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuf.size());
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    std::filesystem::path nativeTitle = PathUtils::Utf8ToNativePath(title);
    if (!title.empty()) {
        ofn.lpstrTitle = nativeTitle.c_str();
    }

    if (GetOpenFileNameW(&ofn)) {
        return PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(fileBuf.data()));
    }
    return "";
#elif defined(__APPLE__)
    // Native macOS file picker via AppleScript NSOpenPanel modal dialog
    std::string prompt = title.empty() ? "Select File" : title;
    std::string cmd = "osascript -e 'POSIX path of (choose file with prompt \"" + prompt + "\")' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[2048] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    }
    pclose(pipe);
    return PathUtils::NormalizeSeparators(result);
#elif defined(__linux__) && !defined(__ANDROID__)
    // Native Linux file picker via standard desktop portal (Zenity or KDialog)
    std::string cmd;
    if (std::system("which zenity >/dev/null 2>&1") == 0) {
        cmd = "zenity --file-selection --title=\"" + (title.empty() ? "Select File" : title) + "\" 2>/dev/null";
    } else if (std::system("which kdialog >/dev/null 2>&1") == 0) {
        cmd = "kdialog --getopenfilename . --title \"" + (title.empty() ? "Select File" : title) + "\" 2>/dev/null";
    }
    if (cmd.empty()) return "";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[2048] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    }
    pclose(pipe);
    return PathUtils::NormalizeSeparators(result);
#else
    (void)title;
    return "";
#endif
}

std::string SystemDialogs::ShowSaveFileDialog(const std::string& title, const std::string& defaultFileName) {
#if defined(_WIN32)
    std::vector<wchar_t> fileBuf(32768, L'\0');
    if (!defaultFileName.empty()) {
        std::filesystem::path defPath = PathUtils::Utf8ToNativePath(defaultFileName);
        wcsncpy_s(fileBuf.data(), fileBuf.size(), defPath.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuf.size());
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0PNG Image (*.png)\0*.png\0JPEG Image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0WebP Image (*.webp)\0*.webp\0GIF Image (*.gif)\0*.gif\0\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    std::filesystem::path nativeTitle = PathUtils::Utf8ToNativePath(title);
    if (!title.empty()) {
        ofn.lpstrTitle = nativeTitle.c_str();
    }

    if (GetSaveFileNameW(&ofn)) {
        return PathUtils::NormalizeSeparators(PathUtils::NativePathToUtf8(fileBuf.data()));
    }
    return "";
#elif defined(__APPLE__)
    // Native macOS save file picker via AppleScript NSSavePanel modal dialog
    std::string prompt = title.empty() ? "Save File" : title;
    std::string defArg = defaultFileName.empty() ? "" : (" default name \"" + defaultFileName + "\"");
    std::string cmd = "osascript -e 'POSIX path of (choose file name with prompt \"" + prompt + "\"" + defArg + ")' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[2048] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    }
    pclose(pipe);
    return PathUtils::NormalizeSeparators(result);
#elif defined(__linux__) && !defined(__ANDROID__)
    // Native Linux save file picker via standard desktop portal (Zenity or KDialog)
    std::string cmd;
    std::string defArg = defaultFileName.empty() ? "" : (" --filename=\"" + defaultFileName + "\"");
    if (std::system("which zenity >/dev/null 2>&1") == 0) {
        cmd = "zenity --file-selection --save --confirm-overwrite --title=\"" + (title.empty() ? "Save File" : title) + "\"" + defArg + " 2>/dev/null";
    } else if (std::system("which kdialog >/dev/null 2>&1") == 0) {
        cmd = "kdialog --getsavefilename . --title \"" + (title.empty() ? "Save File" : title) + "\" 2>/dev/null";
    }
    if (cmd.empty()) return "";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[2048] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    }
    pclose(pipe);
    return PathUtils::NormalizeSeparators(result);
#else
    (void)title;
    (void)defaultFileName;
    return "";
#endif
}

bool SystemDialogs::OpenWithDefaultApp(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) {
        LOG_WARN_CODE(FileManager, FolioErrorCode::SysFileNotFound, "OpenWithDefaultApp rejected: path/URL string is empty.");
        return false;
    }

    std::string targetUri = PathUtils::PathToFileUri(pathOrUrl);

    // Cross-platform dispatch via SDL3 (Windows, macOS, Linux, Android)
    if (SDL_OpenURL(targetUri.c_str())) {
        return true;
    }

    LOG_WARN(FileManager, "SDL_OpenURL failed for (" + targetUri + "): " + SDL_GetError());

#if defined(_WIN32)
    // Native Win32 fallback via ShellExecuteW
    std::filesystem::path nativeP = PathUtils::Utf8ToNativePath(pathOrUrl);
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

} // namespace Folio
