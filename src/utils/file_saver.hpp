#pragma once
/**
 * =========================================================================================
 * @file file_saver.hpp
 * @brief Compatibility Facade delegating file persistence to Folio::FileManager
 * =========================================================================================
 */

#include "utils/file_manager.hpp"
#include <vector>
#include <string>
#include <cstdint>

/**
 * @class FileSaver
 * @brief Helper facade forwarding saving and exporting calls to Folio::FileManager.
 */
class FileSaver {
public:
    /**
     * @brief Automatically ensures that parent directories for a file path exist.
     */
    static bool CreateParentDirectories(const std::string& filePath) {
        std::string parent = Folio::FileManager::GetParentPath(filePath);
        if (parent.empty()) return true;
        return Folio::FileManager::CreateDirectories(parent);
    }

    /**
     * @brief Saves a text string atomically to a file with UTF-8 encoding.
     */
    static bool WriteString(const std::string& filePath, const std::string& content) {
        return Folio::FileManager::WriteTextAtomic(filePath, content);
    }

    /**
     * @brief Saves a raw binary buffer atomically to disk.
     */
    static bool WriteBuffer(const std::string& filePath, const std::vector<uint8_t>& buffer) {
        return Folio::FileManager::WriteBinaryAtomic(filePath, buffer);
    }
};
