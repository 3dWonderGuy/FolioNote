#pragma once
/**
 * =========================================================================================
 * @file io/file_writer.hpp
 * @brief Write-only I/O facade over Folio::FileManager
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE:
 * Domain subsystems that only need to WRITE or PERSIST files should depend on this
 * narrow interface rather than the full FileManager. Narrower dependencies make code
 * easier to reason about -- a component taking FileWriter cannot accidentally read.
 *
 * REPLACES: utils/file_saver.hpp (compatibility alias provided below)
 * All implementations delegate to Folio::FileManager -- no separate .cpp needed.
 */

#include "io/file_manager.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace Folio {

/**
 * @class FileWriter
 * @brief Thin write-only facade over FileManager for domain-layer persistence.
 *
 * Use FileWriter when a component only writes or saves files. For both reading
 * and writing, use FileManager directly.
 */
class FileWriter {
public:
    /**
     * @brief Atomically persists a UTF-8 text string to disk.
     *
     * Atomic Write Mechanics (crash-resilient):
     *   1. Creates a staging file: <targetPath>.tmp.<timestamp>_<random>
     *   2. Writes content and flushes to physical storage (_commit / fsync).
     *   3. Atomically replaces target via OS rename (MoveFileExW / std::filesystem::rename).
     *   If any step fails, staging file is cleaned up and the original is untouched.
     *
     * @param path  UTF-8 destination file path.
     * @param content  UTF-8 text string to persist.
     * @return true if the atomic write succeeded; false on I/O failure.
     */
    static bool WriteString(const std::string& path, const std::string& content) {
        return FileManager::WriteTextAtomic(path, content);
    }

    /**
     * @brief Atomically persists a raw binary buffer to disk.
     *
     * Follows the identical staging, physical flush, and atomic rename lifecycle
     * as WriteString. Used for critical vector page payloads (.ink) and binary blobs.
     *
     * @param path  UTF-8 destination file path.
     * @param buffer  Raw byte vector to persist.
     * @return true if successfully persisted; false on I/O failure.
     */
    static bool WriteBuffer(const std::string& path, const std::vector<uint8_t>& buffer) {
        return FileManager::WriteBinaryAtomic(path, buffer);
    }

    /**
     * @brief Ensures all parent directories for a target file path exist on disk.
     *
     * Convenience wrapper for pre-flight directory creation before writing a file
     * into a potentially non-existent directory subtree.
     *
     * @param filePath  UTF-8 path to the target file (not the directory itself).
     * @return true if parent directories exist or were successfully created.
     */
    static bool CreateParentDirectories(const std::string& filePath) {
        std::string parent = FileManager::GetParentPath(filePath);
        if (parent.empty()) return true;
        return FileManager::CreateDirectories(parent);
    }
};

} // namespace Folio

// -------------------------------------------------------------------------------------
// Compatibility alias: old call sites using FileSaver still compile unchanged.
// TODO: Migrate all call sites to FileWriter and remove this alias.
// -------------------------------------------------------------------------------------
using FileSaver = Folio::FileWriter;