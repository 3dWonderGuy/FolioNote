#pragma once
/**
 * =========================================================================================
 * @file io/file_manager.hpp
 * @brief Centralized, Modular, Cross-Platform Filesystem Abstraction Layer for FolioNote
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DESIGN RATIONALE:
 * In a cross-platform application spanning Windows, Linux, macOS, and Android, file operations
 * and filesystem semantics diverge substantially:
 *
 * 1. Unicode & Encoding Fidelity:
 *    - Windows APIs natively operate on UTF-16 wide strings (`wchar_t`). Passing UTF-8 strings
 *      into standard narrow streams (`std::ofstream`, `fopen`, or narrow `std::filesystem::path`)
 *      causes the MSVC runtime to interpret paths through the system ANSI code page (e.g. CP_ACP 1252),
 *      causing mojibake and `ERROR_FILE_NOT_FOUND` whenever paths contain non-ASCII characters
 *      (e.g., diacritics, CJK glyphs, Cyrillic, Greek, or emojis).
 *    - POSIX systems (Linux, macOS, Android) treat paths natively as UTF-8 byte sequences.
 *
 * 2. Sandboxing & System Directories:
 *    - Desktop operating systems store user documents under `Documents/FolioNote` (queried via
 *      `SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)`).
 *    - Mobile systems (Android) forbid arbitrary filesystem access; application data must be
 *      routed through the app-private sandboxed internal directory (`SDL_GetPrefPath`).
 *    - Bundled read-only assets (icons, default presets, shaders) on Android reside inside the
 *      zipped `.apk` package, requiring abstract streaming (`SDL_IOStream` / `SDL_LoadFile`).
 *
 * 3. Crash-Resilience & Physical Persistence:
 *    - Overwriting files directly (via `std::ofstream(..., std::ios::trunc)`) is susceptible to
 *      truncation and permanent data loss if the application crashes or power is interrupted
 *      during serialization.
 *    - Atomic writes isolate file construction in an adjacent `.tmp` staging file, flush user-space
 *      buffers, force physical OS drive sync (`_commit` / `fsync`), and replace the destination via
 *      an atomic filesystem rename.
 *
 * 4. Modularity & Decoupling:
 *    - Higher-level domain subsystems (e.g., `LibraryManager`, `Notebook`, `CanvasPage`,
 *      `PageRepository`, `SettingsManager`) focus exclusively on in-memory data structures,
 *      business logic, and vector graphics without coupling to OS filesystem calls.
 *    - All physical disk interactions, path transformations, and platform hooks are encapsulated
 *      behind this `FileManager` facade.
 */
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <SDL3/SDL.h>

namespace Folio {

/**
 * =========================================================================================
 * @enum FileType
 * @brief Categorization of directory entries during filesystem traversals.
 * =========================================================================================
 */
enum class FileType {
    RegularFile,    ///< Standard disk file or bundle asset
    Directory,      ///< Physical folder or package directory (e.g. .notebook, .foliolib)
    Other           ///< Symlinks, block devices, or unknown entries
};

/**
 * =========================================================================================
 * @struct FileEntry
 * @brief Telemetry and attributes for an enumerated file or directory entry.
 * =========================================================================================
 */
struct FileEntry {
    std::string fullPath;               ///< Complete normalized UTF-8 filesystem path
    std::string fileName;               ///< Leaf name including extension (e.g. "Calculus.notebook")
    std::string stem;                   ///< Base name without extension (e.g. "Calculus")
    std::string extension;              ///< Extension including leading dot (e.g. ".notebook")
    FileType type = FileType::RegularFile; ///< Entry classification
    uint64_t sizeBytes = 0;             ///< File size in bytes (0 for directories)
};

/**
 * =========================================================================================
 * @class FileManager
 * @brief Modular cross-platform filesystem service and I/O coordinator.
 * =========================================================================================
 */
class FileManager {
public:
    // =====================================================================================
    // 1. STANDARD APPLICATION DIRECTORIES (PLATFORM-AWARE)
    // =====================================================================================

    /**
     * @brief Resolves the global application document directory root.
     *
     * Working Process:
     * - On Android: Returns the sandboxed private application storage path via
     *   `SDL_GetPrefPath("UniversalFramework", "FolioNote")` (e.g., `/data/user/0/.../files/`).
     * - On Desktop (Windows, macOS, Linux): Resolves `SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)`
     *   and appends `FolioNote/`. If the OS denies access, falls back to `./FolioNote/` in CWD.
     *
     * @param overridePath Optional user-configured root override.
     * @return Normalized UTF-8 path to the application root directory.
     */
    static std::string GetAppRootDirectory(const std::string& overridePath = "");

    /**
     * @brief Resolves the directory housing user library bundles (`<AppRoot>/Libraries`).
     */
    static std::string GetLibrariesDirectory();

    /**
     * @brief Resolves the configuration directory (`<AppRoot>/config`).
     */
    static std::string GetConfigDirectory();

    /**
     * @brief Resolves the ephemeral cache directory for render tiles and thumbnails (`<AppRoot>/cache`).
     */
    static std::string GetCacheDirectory();

    /**
     * @brief Resolves the directory for user PDF and bitmap exports (`<AppRoot>/exports`).
     */
    static std::string GetExportsDirectory();

    /**
     * @brief Resolves the diagnostic logging directory (`<AppRoot>/logs`).
     */
    static std::string GetLogsDirectory();

    /**
     * @brief Resolves the directory for automated and manual snapshots (`<AppRoot>/backups`).
     */
    static std::string GetBackupsDirectory();

    /**
     * @brief Resolves a system-wide or app-private temporary working directory.
     */
    static std::string GetTempDirectory();

    // =====================================================================================
    // 2. PATH MANIPULATION & UNICODE NORMALIZATION
    // =====================================================================================

    /**
     * @brief Normalizes path separators (replaces backslashes with forward slashes) and trims trailing slashes.
     * @param path Input filesystem path.
     * @return Cleaned normalized UTF-8 path string.
     */
    static std::string NormalizeSeparators(const std::string& path);

    /**
     * @brief Joins two path segments safely using canonical forward slash separators.
     * @param base Parent path segment.
     * @param child Child file or folder segment.
     * @return Combined UTF-8 path.
     */
    static std::string JoinPath(const std::string& base, const std::string& child);

    /**
     * @brief Joins three path segments safely.
     */
    static std::string JoinPath(const std::string& part1, const std::string& part2, const std::string& part3);

    /**
     * @brief Extracts the parent directory path from a given path string.
     * @param path Input filesystem path.
     * @return Parent folder path, or empty string if root or relative leaf.
     */
    static std::string GetParentPath(const std::string& path);

    /**
     * @brief Extracts the filename component (stem + extension) from a path.
     * @param path Input filesystem path.
     * @return File or directory name (e.g., "Notes.notebook").
     */
    static std::string GetFileName(const std::string& path);

    /**
     * @brief Extracts the stem (filename without extension) from a path.
     * @param path Input filesystem path.
     * @return Stem string (e.g., "Notes" from "Notes.notebook").
     */
    static std::string GetStem(const std::string& path);

    /**
     * @brief Extracts the extension (including leading dot) from a path.
     * @param path Input filesystem path.
     * @return Extension string in lowercase (e.g., ".notebook", ".ink").
     */
    static std::string GetExtension(const std::string& path);

    /**
     * @brief Checks if a path has the specified extension (case-insensitive).
     * @param path Input filesystem path.
     * @param ext Expected extension with leading dot (e.g. ".foliolib").
     * @return true if extension matches.
     */
    static bool HasExtension(const std::string& path, const std::string& ext);

    /**
     * @brief Determines whether a path is absolute (rooted) rather than relative.
     *
     * Working Process:
     * - On Windows: absolute paths begin with a drive letter (e.g. 'C:') or a UNC prefix ('\\\\').
     * - On POSIX (Linux/macOS/Android): absolute paths begin with '/'.
     * - Paths starting with '/' are treated as absolute on all platforms.
     *
     * @param path UTF-8 path string to evaluate.
     * @return true if the path is absolute; false if relative or empty.
     */
    [[nodiscard]] static bool IsAbsolutePath(const std::string& path);

    /**
     * @brief Sanitizes a filename or title string by replacing filesystem-illegal characters.
     *
     * Working Process:
     * Removes or replaces characters prohibited by Windows NTFS/FAT and POSIX:
     * `\`, `/`, `:`, `*`, `?`, `"`, `<`, `>`, `|`, and control characters (ASCII < 32).
     *
     * @param name Raw title or filename.
     * @param replacement Replacement character for forbidden runes (default: '_').
     * @return Sanitized filesystem-safe name string.
     */
    static std::string SanitizeFileName(const std::string& name, char replacement = '_');

    /**
     * @brief Converts a filesystem path or relative specifier into a standard URI ("file:///...").
     * 
     * Working Process:
     * - If the string already starts with a protocol ("file://", "http://", "https://"), returns as-is.
     * - On Windows: normalizes backslashes to forward slashes; if starting with a drive letter (e.g. "C:/..."),
     *   prepends "file:///"; otherwise prepends "file://".
     * - On POSIX: prepends "file://" to the path.
     * 
     * @param path Local filesystem path or existing URL.
     * @return Formatted URI string.
     */
    static std::string PathToFileUri(const std::string& path);

    /**
     * @brief Opens a native OS file picker dialog to select an existing file.
     *
     * Working Process:
     * - Desktop (Windows): Invokes GetOpenFileNameW with Unicode UTF-16 wide string conversion.
     * - Returns the selected absolute normalized UTF-8 path, or an empty string if cancelled.
     *
     * @param title Dialog window title (e.g. "Select Attachment File").
     * @return Selected UTF-8 path string, or empty string on cancellation or error.
     */
    static std::string ShowOpenFileDialog(const std::string& title = "Select File");

    /**
     * @brief Launches a file or URL with the operating system's default application.
     * 
     * Cross-Platform Working Process:
     * - Converts file paths to standard file URIs via `PathToFileUri()`.
     * - Calls `SDL_OpenURL()` which dispatches to:
     *     - Windows: ShellExecuteW
     *     - Linux:   xdg-open or freedesktop Portal OpenURI
     *     - macOS:   NSWorkspace openURL
     *     - Android: Intent (ACTION_VIEW)
     * - If SDL_OpenURL reports failure on Windows, falls back to native Win32 `ShellExecuteW`.
     * 
     * @param pathOrUrl Target filesystem path or URL to open.
     * @return true if successfully dispatched to the OS; false otherwise.
     */
    static bool OpenWithDefaultApp(const std::string& pathOrUrl);

    /**
     * @brief Generates a collision-free path in a directory by appending numeric counters if needed.
     *
     * Working Process:
     * Checks if `<parentDir>/<baseStem><extension>` exists. If it does, iterates
     * `<parentDir>/<baseStem> (1)<extension>`, `<parentDir>/<baseStem> (2)<extension>`, etc.,
     * until an unused target path is found.
     *
     * @param parentDir Directory where the item will reside.
     * @param baseStem Desired filename stem.
     * @param extension File extension including dot (e.g. ".notebook").
     * @return Collision-free absolute or relative path string.
     */
    static std::string DisambiguatePath(const std::string& parentDir, const std::string& baseStem, const std::string& extension);

    // =====================================================================================
    // 3. ATOMIC FILE I/O (CRASH-RESILIENT WRITES)
    // =====================================================================================

    /**
     * @brief Atomically writes a text string to disk with UTF-8 encoding.
     *
     * Atomic Write Mechanics:
     * 1. Ensures the target parent directory exists via `CreateDirectories`.
     * 2. Constructs a staging filename in the same directory: `<targetPath>.tmp.<random_suffix>`.
     * 3. Opens the staging file and writes the UTF-8 payload.
     * 4. Flushes CRT buffers and commits to physical storage (`_commit` on Windows, `fsync` on POSIX).
     * 5. Atomically renames the staging file to the target path via OS atomic swap
     *    (`std::filesystem::rename` or Win32 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`).
     *
     * @param targetPath UTF-8 destination file path.
     * @param content Text string to persist.
     * @return true if the atomic write succeeded; false on I/O failure.
     */
    static bool WriteTextAtomic(const std::string& targetPath, const std::string& content);

    /**
     * @brief Atomically writes a raw binary buffer to disk.
     *
     * Follows the identical staging, physical flush, and atomic rename lifecycle as `WriteTextAtomic`.
     * Used for critical vector page payloads (`.ink`), databases, and binary blobs.
     *
     * @param targetPath UTF-8 destination file path.
     * @param buffer Raw byte vector.
     * @return true if successfully persisted; false on I/O failure.
     */
    static bool WriteBinaryAtomic(const std::string& targetPath, const std::vector<uint8_t>& buffer);

    /**
     * @brief Reads an entire text file into memory as a UTF-8 string.
     *
     * Supports both regular disk files (Windows wide Unicode paths) and bundled Android
     * package assets (via SDL3 streaming).
     *
     * @param filePath UTF-8 path to the file.
     * @param outContent Output string receiving the file contents.
     * @return true on success; false if inaccessible or read fails.
     */
    static bool ReadText(const std::string& filePath, std::string& outContent);

    /**
     * @brief Reads an entire binary file into memory as a byte buffer.
     *
     * @param filePath UTF-8 path to the file.
     * @param outBuffer Output vector receiving raw bytes.
     * @return true on success; false if inaccessible or read fails.
     */
    static bool ReadBinary(const std::string& filePath, std::vector<uint8_t>& outBuffer);

    /**
     * @brief Opens a read-only cross-platform stream handle to a file.
     *
     * Uses SDL3's virtual filesystem (`SDL_IOFromFile`), abstracting disk files,
     * read-only APK assets on Android, and memory streams.
     *
     * IMPORTANT: Caller is responsible for closing the stream via `SDL_CloseIO(stream)`.
     *
     * @param filePath UTF-8 file path.
     * @return Pointer to SDL_IOStream, or nullptr if file cannot be opened.
     */
    [[nodiscard]] static SDL_IOStream* OpenReadStream(const std::string& filePath);

    // =====================================================================================
    // 4. FILESYSTEM QUERIES & MUTATIONS
    // =====================================================================================

    /**
     * @brief Checks if a file or directory exists at the given path.
     * @param path UTF-8 path.
     * @return true if the entity exists on disk or inside the bundled asset package.
     */
    [[nodiscard]] static bool Exists(const std::string& path);

    /**
     * @brief Checks if the given path represents an existing directory.
     * @param path UTF-8 path.
     * @return true if path exists and is a directory.
     */
    [[nodiscard]] static bool IsDirectory(const std::string& path);

    /**
     * @brief Checks if the given path represents a regular file.
     * @param path UTF-8 path.
     * @return true if path exists and is a regular file.
     */
    [[nodiscard]] static bool IsRegularFile(const std::string& path);

    /**
     * @brief Checks if two paths resolve to the same underlying physical filesystem entity.
     *
     * @param pathA First UTF-8 path.
     * @param pathB Second UTF-8 path.
     * @return true if both paths refer to the same physical file/directory.
     */
    [[nodiscard]] static bool AreEquivalent(const std::string& pathA, const std::string& pathB);

    /**
     * @brief Recursively ensures that the specified directory chain exists on disk.
     * @param dirPath UTF-8 directory path.
     * @return true if the directory already exists or was successfully created.
     */
    static bool CreateDirectories(const std::string& dirPath);

    /**
     * @brief Deletes a single file from disk.
     * @param filePath UTF-8 path to the file.
     * @return true if file was removed; false on error.
     */
    static bool RemoveFile(const std::string& filePath);

    /**
     * @brief Recursively deletes a directory and all of its contents.
     * @param dirPath UTF-8 path to directory.
     * @return true if directory was completely removed.
     */
    static bool RemoveDirectoryRecursive(const std::string& dirPath);

    /**
     * @brief Copies a single file from source to destination.
     * @param sourcePath UTF-8 source file path.
     * @param destinationPath UTF-8 target file path.
     * @param overwrite If true, replaces existing destination file.
     * @return true on success; false on failure.
     */
    static bool CopySingleFile(const std::string& sourcePath, const std::string& destinationPath, bool overwrite = true);

    /**
     * @brief Recursively copies an entire directory tree to a new location.
     * @param sourceDir UTF-8 source directory.
     * @param destinationDir UTF-8 target directory.
     * @return true on success; false on failure.
     */
    static bool CopyDirectoryRecursive(const std::string& sourceDir, const std::string& destinationDir);

    /**
     * @brief Compatibility alias for CopySingleFile with overwrite=true.
     *
     * NOTE: Named `CopyFileTo` (not `CopyFile`) to avoid collision with the Win32
     * `#define CopyFile CopyFileA` preprocessor macro from <windows.h>, which expands
     * any token named `CopyFile` to `CopyFileA` before the compiler can resolve it as
     * a class member — even with parenthesized suppression `(FileManager::CopyFile)`.
     *
     * @param sourcePath UTF-8 source file path.
     * @param destinationPath UTF-8 target file path.
     * @return true on success; false on failure.
     */
    static bool CopyFileTo(const std::string& sourcePath, const std::string& destinationPath) {
        return CopySingleFile(sourcePath, destinationPath, /*overwrite=*/true);
    }

    /**
     * @brief Moves or renames a file or directory tree with cross-volume safety fallbacks.
     * @param sourcePath Source file or folder.
     * @param destinationPath Target file or folder.
     * @return true if move succeeded; false on failure.
     */
    static bool Move(const std::string& sourcePath, const std::string& destinationPath);

    /**
     * @brief Lists all direct children within a directory matching an optional filter.
     *
     * Optimization: Filters by native extension early to avoid unnecessary UTF-8 string
     * conversions, path normalizations, and memory allocations for discarded entries.
     *
     * @param dirPath Directory to scan.
     * @param extensionFilter Optional extension filter (e.g. ".notebook"). Leave empty for all.
     * @return Vector of FileEntry metadata structs for matching children.
     */
    static std::vector<FileEntry> ListEntries(const std::string& dirPath, const std::string& extensionFilter = "");

    /**
     * @brief Lists all direct subdirectories inside a given directory.
     * @param dirPath Directory to scan.
     * @return Vector of absolute UTF-8 directory paths.
     */
    static std::vector<std::string> ListDirectories(const std::string& dirPath);

    /**
     * @brief Returns the size of a file in bytes.
     * @param filePath UTF-8 file path.
     * @return Total bytes, or 0 if inaccessible.
     */
    [[nodiscard]] static uint64_t GetFileSize(const std::string& filePath);

    // =====================================================================================
    // 5. DIAGNOSTICS & HASHING (FNV-1a 64-BIT ALGORITHM)
    // =====================================================================================

    /**
     * @brief Computes a rapid 64-bit content hash (FNV-1a) and byte size for any file on disk.
     *
     * @param filePath Full UTF-8 path to the file.
     * @param outHash Output receiving the 64-bit hash.
     * @param outSize Output receiving the exact file size in bytes.
     * @return true if file was read and hashed successfully; false if inaccessible.
     */
    static bool ComputeFileHash64(const std::string& filePath, uint64_t& outHash, uint64_t& outSize);

    /**
     * @brief Rapid non-blocking inspector to scan a PDF file and extract its page count.
     *
     * Employs zero-heap window scanning across trailer structures and linearized tags
     * to avoid memory overhead on multi-hundred-megabyte PDF files.
     *
     * @param filePath UTF-8 encoded PDF file path.
     * @return Detected page count (>= 1).
     */
    static int DetectPdfPageCount(const std::string& filePath);
};

} // namespace Folio