#pragma once
/**
 * =========================================================================================
 * @file io/path_utils.hpp
 * @brief Cross-platform Unicode-aware filesystem path utilities for FolioNote
 * =========================================================================================
 */

#include <string>
#include <string_view>
#include <filesystem>

namespace Folio {

class PathUtils {
public:
    /**
     * @brief Converts UTF-8 string to native std::filesystem::path.
     * Handles Win32 UTF-16 wide strings and long path prefix (\\?\) for paths >= 240 chars.
     */
    static std::filesystem::path Utf8ToNativePath(const std::string& utf8Str);

    /**
     * @brief Converts native std::filesystem::path to UTF-8 string.
     * Strips Win32 extended-length prefix (\\?\) so user-facing strings remain canonical.
     */
    static std::string NativePathToUtf8(const std::filesystem::path& p);

    /**
     * @brief Normalizes path separators (replaces backslashes with forward slashes) and trims trailing slashes.
     */
    static std::string NormalizeSeparators(const std::string& path);

    /**
     * @brief Joins two path segments safely using canonical forward slash separators.
     */
    static std::string JoinPath(const std::string& base, const std::string& child);

    /**
     * @brief Joins three path segments safely using canonical forward slash separators.
     */
    static std::string JoinPath(const std::string& part1, const std::string& part2, const std::string& part3);

    /**
     * @brief Extracts the parent directory path from a given path string.
     */
    static std::string GetParentPath(const std::string& path);

    /**
     * @brief Extracts the filename component (stem + extension) from a path.
     */
    static std::string GetFileName(const std::string& path);

    /**
     * @brief Extracts the stem (filename without extension) from a path.
     */
    static std::string GetStem(const std::string& path);

    /**
     * @brief Extracts the extension (including leading dot) from a path in lowercase.
     */
    static std::string GetExtension(const std::string& path);

    /**
     * @brief Checks if a path has the specified extension (case-insensitive).
     */
    static bool HasExtension(const std::string& path, const std::string& ext);

    /**
     * @brief Determines whether a path is absolute (rooted) rather than relative.
     */
    [[nodiscard]] static bool IsAbsolutePath(const std::string& path);

    /**
     * @brief Sanitizes a filename or title string by replacing filesystem-illegal characters.
     */
    static std::string SanitizeFileName(const std::string& name, char replacement = '_');

    /**
     * @brief Converts a filesystem path or relative specifier into a standard URI ("file:///...").
     */
    static std::string PathToFileUri(const std::string& path);

    /**
     * @brief Generates a collision-free path in a directory by appending numeric counters if needed.
     */
    static std::string DisambiguatePath(const std::string& parentDir, const std::string& baseStem, const std::string& extension);

    /**
     * @brief Strips FILE_ATTRIBUTE_READONLY on Windows to ensure atomic overwrite/move succeeds.
     */
    static void EnsureTargetWritable(const std::filesystem::path& nativePath) noexcept;
};

} // namespace Folio
