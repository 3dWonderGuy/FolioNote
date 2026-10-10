#include "io/paths/path_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>

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

std::filesystem::path PathUtils::Utf8ToNativePath(const std::string& utf8Str) {
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

    // If path is long (>= 240 chars) and is an absolute drive path ("C:\..."), normalize to backslashes and prepend "\\?\"
    // to bypass legacy Win32 MAX_PATH (260 character) limits for deeply nested notebooks and pages.
    if (wstr.size() >= 240 && wstr.rfind(L"\\\\?\\", 0) == std::wstring::npos) {
        if (wstr.size() >= 3 && std::iswalpha(wstr[0]) && wstr[1] == L':' && (wstr[2] == L'\\' || wstr[2] == L'/')) {
            std::replace(wstr.begin(), wstr.end(), L'/', L'\\');
            wstr.insert(0, L"\\\\?\\");
        }
    }
    return std::filesystem::path(std::move(wstr));
#else
    return std::filesystem::path(utf8Str);
#endif
}

std::string PathUtils::NativePathToUtf8(const std::filesystem::path& p) {
#if defined(_WIN32)
    std::wstring wstr = p.native();
    if (wstr.empty()) {
        return std::string();
    }
    // Strip Win32 extended-length prefix if present so user-facing strings remain canonical
    if (wstr.rfind(L"\\\\?\\", 0) == 0) {
        wstr.erase(0, 4);
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

std::string PathUtils::NormalizeSeparators(const std::string& path) {
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

std::string PathUtils::JoinPath(const std::string& base, const std::string& child) {
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

std::string PathUtils::JoinPath(const std::string& part1, const std::string& part2, const std::string& part3) {
    return JoinPath(JoinPath(part1, part2), part3);
}

std::string PathUtils::GetParentPath(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    auto parent = nativeP.parent_path();
    return NormalizeSeparators(NativePathToUtf8(parent));
}

std::string PathUtils::GetFileName(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    return NativePathToUtf8(nativeP.filename());
}

std::string PathUtils::GetStem(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    return NativePathToUtf8(nativeP.stem());
}

std::string PathUtils::GetExtension(const std::string& path) {
    if (path.empty()) return "";
    auto nativeP = Utf8ToNativePath(path);
    std::string ext = NativePathToUtf8(nativeP.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

bool PathUtils::HasExtension(const std::string& path, const std::string& ext) {
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

bool PathUtils::IsAbsolutePath(const std::string& path) {
    if (path.empty()) return false;
    return Utf8ToNativePath(path).is_absolute();
}

std::string PathUtils::SanitizeFileName(const std::string& name, char replacement) {
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

std::string PathUtils::PathToFileUri(const std::string& path) {
    if (path.empty()) return "";

    if (path.rfind("file://", 0) == 0 ||
        path.rfind("http://", 0) == 0 ||
        path.rfind("https://", 0) == 0 ||
        path.rfind("content://", 0) == 0 ||
        path.rfind("mailto:", 0) == 0) {
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

std::string PathUtils::DisambiguatePath(const std::string& parentDir, const std::string& baseStem, const std::string& extension) {
    std::string cleanStem = SanitizeFileName(baseStem.empty() ? "Untitled" : baseStem);
    std::string ext = extension;
    if (!ext.empty() && ext.front() != '.') {
        ext = "." + ext;
    }

    std::string candidate = JoinPath(parentDir, cleanStem + ext);
    int counter = 1;
    std::error_code ec;
    while (std::filesystem::exists(Utf8ToNativePath(candidate), ec)) {
        candidate = JoinPath(parentDir, cleanStem + " (" + std::to_string(counter++) + ")" + ext);
    }
    return candidate;
}

void PathUtils::EnsureTargetWritable(const std::filesystem::path& nativePath) noexcept {
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesW(nativePath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(nativePath.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
#else
    (void)nativePath;
#endif
}

} // namespace Folio
