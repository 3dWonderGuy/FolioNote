#pragma once
/**
 * =========================================================================================
 * @file io/file_writer.hpp
 * @brief Crash-resilient atomic file writing and persistence service for FolioNote
 * =========================================================================================
 */

#include <vector>
#include <string>
#include <cstdint>

namespace Folio {

class FileWriter {
public:
    /**
     * @brief Atomically persists a UTF-8 text string to disk.
     */
    static bool WriteString(const std::string& path, const std::string& content);

    /**
     * @brief Atomically persists a raw binary buffer to disk.
     */
    static bool WriteBuffer(const std::string& path, const std::vector<uint8_t>& buffer);

    /**
     * @brief Ensures all parent directories for a target file path exist on disk.
     */
    static bool CreateParentDirectories(const std::string& filePath);

    // Backward-compatibility aliases
    static bool WriteTextAtomic(const std::string& path, const std::string& content) {
        return WriteString(path, content);
    }
    static bool WriteBinaryAtomic(const std::string& path, const std::vector<uint8_t>& buffer) {
        return WriteBuffer(path, buffer);
    }
};

} // namespace Folio

using FileSaver = Folio::FileWriter;