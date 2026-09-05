#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>
#include "utils/logger.hpp"

/**
 * @class FileSaver
 * @brief Helper utility for saving and exporting cross-platform application data
 *        (Handles standard filesystem writing, directory creation, and exports).
 */
class FileSaver {
public:

    /**
     * @brief Automatically ensures that the parent directories for a file path exist.
     * 
     * HOW IT WORKS:
     * 1. Extracts the parent folder from the file path.
     * 2. Checks if it exists, and if not, creates the entire folder chain.
     */
    static bool CreateParentDirectories(const std::string& filePath) {
        try {
            std::filesystem::path p(filePath);
            if (p.has_parent_path() && !std::filesystem::exists(p.parent_path())) {
                std::filesystem::create_directories(p.parent_path());
            }
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR(FileLoader, std::string("Failed to create parent directories for: ") + filePath + " | Error: " + e.what());
            return false;
        }
    }

    /**
     * @brief Saves a text string directly to a file (useful for JSON, text files, logs).
     * 
     * HOW IT WORKS:
     * 1. Ensures parent directory chain exists.
     * 2. Opens file in standard write/truncate mode.
     * 3. Writes the content string and flushes it immediately.
     */
    static bool WriteString(const std::string& filePath, const std::string& content) {
        if (!CreateParentDirectories(filePath)) {
            return false;
        }

        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            LOG_ERROR(FileLoader, "Failed to open file for writing text: " + filePath);
            return false;
        }

        file << content;
        file.flush();
        return true;
    }

    /**
     * @brief Saves a raw binary buffer directly to a file (useful for cached attachments, zip blobs).
     *
     * HOW IT WORKS:
     * 1. Ensures parent directory chain exists.
     * 2. Opens file in standard write mode.
     * 3. Writes the binary data and flushes it immediately.
     *
     * @param filePath - The path to the file to save the binary data to.
     * @param buffer - The binary data to save to the file.
     * @return true if the binary data was saved successfully, false otherwise.
     */
    static bool WriteBuffer(const std::string& filePath, const std::vector<uint8_t>& buffer) {
        if (!CreateParentDirectories(filePath)) {
            return false;
        }

        std::ofstream file(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            LOG_ERROR(FileLoader, "Failed to open file for writing binary: " + filePath);
            return false;
        }

        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        file.flush();
        return true;
    }


};
