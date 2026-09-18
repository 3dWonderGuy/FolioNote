#pragma once
/**
 * =========================================================================================
 * @file file_loader.hpp
 * @brief Compatibility Facade delegating asset loading to Folio::FileManager
 * =========================================================================================
 */

#include "utils/file_manager.hpp"
#include "utils/logger.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>

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

/**
 * @brief Converts a UTF-8 encoded string to a native std::filesystem::path.
 */
inline std::filesystem::path Utf8ToPath(const std::string& utf8Str) {
    if (utf8Str.empty()) return std::filesystem::path();
#if defined(_WIN32)
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return std::filesystem::path(utf8Str);
    std::wstring wstr(static_cast<size_t>(sizeNeeded), 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), &wstr[0], sizeNeeded);
    return std::filesystem::path(std::move(wstr));
#else
    return std::filesystem::path(utf8Str);
#endif
}

/**
 * @brief Converts a std::filesystem::path to a UTF-8 encoded std::string.
 */
inline std::string PathToUtf8(const std::filesystem::path& p) {
#if defined(_WIN32)
    const std::wstring& wstr = p.native();
    if (wstr.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return p.string();
    std::string str(static_cast<size_t>(sizeNeeded), 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &str[0], sizeNeeded, nullptr, nullptr);
    return str;
#else
    return p.string();
#endif
}

} // namespace Folio

/**
 * @class FileLoader
 * @brief Helper facade forwarding asset loading and streaming to Folio::FileManager.
 */
class FileLoader {
public:
    static inline std::filesystem::path Utf8ToPath(const std::string& utf8Str) {
        return Folio::Utf8ToPath(utf8Str);
    }
    static inline std::string PathToUtf8(const std::filesystem::path& p) {
        return Folio::PathToUtf8(p);
    }

    static bool ReadToBuffer(const std::string& assetRelativePath, std::vector<uint8_t>& outBuffer) {
        return Folio::FileManager::ReadBinary(assetRelativePath, outBuffer);
    }

    static bool ReadToString(const std::string& assetRelativePath, std::string& outString) {
        return Folio::FileManager::ReadText(assetRelativePath, outString);
    }

    [[nodiscard]] static SDL_IOStream* OpenAsStream(const std::string& assetRelativePath) noexcept {
        return Folio::FileManager::OpenReadStream(assetRelativePath);
    }

    [[nodiscard]] static bool Exists(const std::string& assetRelativePath) noexcept {
        return Folio::FileManager::Exists(assetRelativePath);
    }

    static bool WriteString(const std::string& assetRelativePath, const std::string& content) {
        return Folio::FileManager::WriteTextAtomic(assetRelativePath, content);
    }

    static bool ComputeFileHash64(const std::string& filePath, uint64_t& outHash, uint64_t& outSize) {
        return Folio::FileManager::ComputeFileHash64(filePath, outHash, outSize);
    }

    static int DetectPdfPageCount(const std::string& filePath) {
        return Folio::FileManager::DetectPdfPageCount(filePath);
    }
};
