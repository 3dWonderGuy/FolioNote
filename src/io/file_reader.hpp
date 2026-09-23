#pragma once
/**
 * =========================================================================================
 * @file io/file_reader.hpp
 * @brief Read-only I/O facade over Folio::FileManager
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE:
 * Domain subsystems that only need to READ files (assets, configs, blobs) should
 * depend on this narrow interface rather than the full FileManager. This enforces
 * a clean separation between read paths and write paths at the call-site level.
 *
 * REPLACES: utils/file_loader.hpp (compatibility alias provided below)
 * All implementations delegate to Folio::FileManager -- no separate .cpp needed.
 */

#include "io/file_manager.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cstdint>

namespace Folio {

/**
 * @class FileReader
 * @brief Thin read-only facade over FileManager for domain-layer asset loading.
 *
 * Use FileReader when a component only reads files. For both reading and writing,
 * use FileManager directly.
 */
class FileReader {
public:
    /**
     * @brief Reads an entire binary file into memory as a raw byte vector.
     *
     * Tries SDL3 virtual filesystem first (supports Android APK assets),
     * then falls back to Win32 wide-path fopen on Windows.
     *
     * @param path  UTF-8 file path (disk or APK-relative).
     * @param outBuffer  Output vector receiving raw bytes.
     * @return true on success; false if file cannot be opened or read.
     */
    static bool ReadToBuffer(const std::string& path, std::vector<uint8_t>& outBuffer) {
        return FileManager::ReadBinary(path, outBuffer);
    }

    /**
     * @brief Reads an entire text file into memory as a UTF-8 string.
     *
     * @param path  UTF-8 file path.
     * @param outString  Output string receiving file contents.
     * @return true on success; false if file cannot be opened or read.
     */
    static bool ReadToString(const std::string& path, std::string& outString) {
        return FileManager::ReadText(path, outString);
    }

    /**
     * @brief Opens a read-only cross-platform stream handle via SDL3 virtual filesystem.
     *
     * Abstracts disk files, Android APK assets, and in-memory streams uniformly.
     * IMPORTANT: Caller is responsible for closing the stream via SDL_CloseIO(stream).
     *
     * @param path  UTF-8 file path.
     * @return SDL_IOStream* handle, or nullptr if file cannot be opened.
     */
    [[nodiscard]] static SDL_IOStream* OpenAsStream(const std::string& path) noexcept {
        return FileManager::OpenReadStream(path);
    }

    /**
     * @brief Verifies whether a file or directory exists on disk or inside a bundled package.
     *
     * @param path  UTF-8 path.
     * @return true if entity exists.
     */
    [[nodiscard]] static bool Exists(const std::string& path) noexcept {
        return FileManager::Exists(path);
    }

    /**
     * @brief Computes a rapid 64-bit FNV-1a content hash and byte size for a file.
     *
     * FNV-1a algorithm (Fowler-Noll-Vo):
     *   hash = FNV_offset_basis (14695981039346656037)
     *   for each byte b: hash = (hash XOR b) * FNV_prime (1099511628211)
     * Produces a 64-bit fingerprint with excellent avalanche properties.
     *
     * @param path  UTF-8 file path.
     * @param outHash  Output receiving the 64-bit hash.
     * @param outSize  Output receiving the exact byte count.
     * @return true if file was successfully read and hashed.
     */
    static bool ComputeFileHash64(const std::string& path, uint64_t& outHash, uint64_t& outSize) {
        return FileManager::ComputeFileHash64(path, outHash, outSize);
    }

    /**
     * @brief Non-blocking fast scan to determine the page count of a PDF file.
     *
     * Scanning strategy (zero-heap window approach):
     *   1. Checks header for /Linearized /N tag (fast path for linearized PDFs).
     *   2. Scans the last 64KB for /Count entries adjacent to /Pages nodes.
     *   3. Falls back to counting /Type /Page tokens in 32KB streaming chunks.
     *
     * @param path  UTF-8 encoded PDF file path.
     * @return Detected page count (>= 1).
     */
    static int DetectPdfPageCount(const std::string& path) {
        return FileManager::DetectPdfPageCount(path);
    }
};

} // namespace Folio

// -------------------------------------------------------------------------------------
// Compatibility alias: old call sites using FileLoader still compile unchanged.
// TODO: Migrate all call sites to FileReader and remove this alias.
// -------------------------------------------------------------------------------------
using FileLoader = Folio::FileReader;