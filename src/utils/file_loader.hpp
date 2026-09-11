#pragma once
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include "utils/logger.hpp"

/**
 * @class FileLoader
 * @brief Helper utility for loading and streaming cross-platform application files
 */
class FileLoader {
public:

    /**
     * @brief Reads an entire file into a raw binary buffer (std::vector<uint8_t>).
     * 
     * HOW IT WORKS:
     * 1. Calls SDL_LoadFile which reads the file and allocates memory.
     * 2. Resizes outBuffer to match the file size.
     * 3. Copies the data from the temporary SDL pointer into outBuffer.
     * 4. Frees the temporary pointer using SDL_free.
     * 
     * WHEN TO USE IT:
     * Use this for small binary assets (e.g. vector graphics files like SVG/Lunasvg, 
     * small binary configuration tables, custom font files) that you need fully 
     * loaded into memory at once.
     */
    static bool ReadToBuffer(const std::string& assetRelativePath, std::vector<uint8_t>& outBuffer) {
        size_t dataSize = 0;
        
        // Load the file into a temporary buffer allocated by SDL.
        void* rawData = SDL_LoadFile(assetRelativePath.c_str(), &dataSize);

        // If the file couldn't be opened, log why and return false.
        if (!rawData) {
            LOG_ERROR(FileLoader, "Failed to load asset: " + assetRelativePath + " | SDL Error: " + SDL_GetError());
            return false;
        }

        // Copy the data from SDL's temporary memory pool into our vector.
        outBuffer.resize(dataSize);
        std::memcpy(outBuffer.data(), rawData, dataSize);
        
        // Clean up the memory allocated by SDL_LoadFile.
        SDL_free(rawData);
        return true;
    }

    /**
     * @brief Reads an entire text file into a standard C++ string.
     * 
     * HOW IT WORKS:
     * 1. Calls ReadToBuffer to get the raw bytes.
     * 2. Reinterprets the bytes as a sequence of characters and assigns them to outString.
     * 
     * WHEN TO USE IT:
     * Use this for text-based assets like JSON files (default settings files)
     * and GPU shaders (GLSL/HLSL files) where you need to parse the content as text.
     * @param assetRelativePath - The path to the asset file relative to the application's asset directory.
     * @param outString - The string to store the contents of the asset file.
     * @return true if the asset was read successfully, false otherwise.
     */
    static bool ReadToString(const std::string& assetRelativePath, std::string& outString) {
        std::vector<uint8_t> buffer;
        if (!ReadToBuffer(assetRelativePath, buffer)) {
            return false;
        }
        
        // Reinterpret the binary buffer pointer as a char pointer and assign it to the string.
        outString.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        return true;
    }

    /**
     * @brief Opens a read-only stream interface to a file.
     * 
     * HOW IT WORKS:
     * 1. Opens the file in binary read mode ("rb") using SDL's cross-platform VFS.
     * 2. Returns an abstract SDL_IOStream pointer representing the open file handle.
     * 
     * WHEN TO USE IT:
     * Use this for HUGE files (like PDFs or large background images). It does not 
     * load the file into memory yet. Other libraries (like PDFium) can read 
     * page data from this stream incrementally as needed.
     * 
     * IMPORTANT: The caller is responsible for calling SDL_CloseIO() on the returned stream.
     */
    [[nodiscard]] static SDL_IOStream* OpenAsStream(const std::string& assetRelativePath) noexcept {
        return SDL_IOFromFile(assetRelativePath.c_str(), "rb");
    }

    /**
     * @brief Instantly checks if a file exists on disk or inside the package.
     * 
     * HOW IT WORKS:
     * 1. Attempts to open the file stream using SDL_IOFromFile.
     * 2. If it succeeds, closes the handle immediately and returns true.
     * 3. If it returns nullptr, returns false.
     * 
     * WHEN TO USE IT:
     * Use this for quick checks (e.g., verifying if a default settings file exists 
     * before attempting to load it, or checking if an attachment is missing).
     */
    [[nodiscard]] static bool Exists(const std::string& assetRelativePath) noexcept {
        SDL_IOStream* stream = SDL_IOFromFile(assetRelativePath.c_str(), "rb");
        if (stream) {
            SDL_CloseIO(stream);
            return true;
        }
        return false;
    }

    /**
     * @brief Writes an entire text string to a file using SDL3 cross-platform I/O.
     * 
     * WHEN TO USE IT:
     * Use this for writing text files such as JSON configuration and settings files.
     * @param assetRelativePath - The path to the file to write.
     * @param content - The text content to write.
     * @return true if the file was written successfully, false otherwise.
     */
    static bool WriteString(const std::string& assetRelativePath, const std::string& content) {
        std::error_code ec;
        std::filesystem::path dirPath = std::filesystem::path(assetRelativePath).parent_path();
        if (!dirPath.empty() && !std::filesystem::exists(dirPath, ec)) {
            std::filesystem::create_directories(dirPath, ec);
        }

        if (!SDL_SaveFile(assetRelativePath.c_str(), content.data(), content.size())) {
            LOG_ERROR(FileLoader, "Failed to save file: " + assetRelativePath + " | SDL Error: " + SDL_GetError());
            return false;
        }
        return true;
    }

    /**
     * @brief Computes a fast 64-bit content hash (FNV-1a) and file size for any file on disk.
     */
    static bool ComputeFileHash64(const std::string& filePath, uint64_t& outHash, uint64_t& outSize) {
        outHash = 14695981039346656037ULL;
        outSize = 0;
        SDL_IOStream* stream = SDL_IOFromFile(filePath.c_str(), "rb");
        if (!stream) return false;

        uint8_t buffer[65536];
        size_t bytesRead = 0;
        while ((bytesRead = SDL_ReadIO(stream, buffer, sizeof(buffer))) > 0) {
            outSize += bytesRead;
            for (size_t i = 0; i < bytesRead; ++i) {
                outHash ^= buffer[i];
                outHash *= 1099511628211ULL;
            }
        }
        SDL_CloseIO(stream);
        return true;
    }

    /**
     * @brief Fast, non-blocking scan to inspect a PDF file and extract its page count.
     * Searches for standard PDF /Count metadata and /Type /Page entries without external dependencies.
     */
    static int DetectPdfPageCount(const std::string& filePath) {
        SDL_IOStream* stream = SDL_IOFromFile(filePath.c_str(), "rb");
        if (!stream) return 1;

        // Check header '%PDF-'
        char header[8] = {0};
        if (SDL_ReadIO(stream, header, 5) < 5 || std::memcmp(header, "%PDF-", 5) != 0) {
            SDL_CloseIO(stream);
            return 1;
        }

        Sint64 fileSize = SDL_GetIOSize(stream);
        if (fileSize <= 0) {
            SDL_CloseIO(stream);
            return 1;
        }

        // 1. Scan from file end backwards (trailers / page catalog are almost always in the last 64KB)
        size_t tailScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 65536));
        SDL_SeekIO(stream, fileSize - tailScanSize, SDL_IO_SEEK_SET);
        std::vector<char> buffer(tailScanSize + 1, 0);
        SDL_ReadIO(stream, buffer.data(), tailScanSize);

        std::string tailStr(buffer.data(), tailScanSize);
        // Look for /Count followed by number
        size_t countPos = tailStr.rfind("/Count");
        while (countPos != std::string::npos) {
            size_t numStart = countPos + 6;
            while (numStart < tailStr.size() && (tailStr[numStart] == ' ' || tailStr[numStart] == '\t' || tailStr[numStart] == '\r' || tailStr[numStart] == '\n')) {
                numStart++;
            }
            if (numStart < tailStr.size() && std::isdigit(static_cast<unsigned char>(tailStr[numStart]))) {
                try {
                    int countVal = std::stoi(tailStr.substr(numStart, 10));
                    if (countVal > 0) {
                        SDL_CloseIO(stream);
                        return countVal;
                    }
                } catch (...) {}
            }
            if (countPos == 0) break;
            countPos = tailStr.rfind("/Count", countPos - 1);
        }

        // 2. Fallback: Full stream sweep counting "/Type /Page" tokens (excluding "/Type /Pages")
        SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
        int pageTokenCount = 0;
        std::vector<char> chunk(32768, 0);
        std::string carry = "";
        while (true) {
            size_t readCount = SDL_ReadIO(stream, chunk.data(), chunk.size());
            if (readCount == 0) break;
            std::string block = carry + std::string(chunk.data(), readCount);
            size_t p = 0;
            while ((p = block.find("/Type", p)) != std::string::npos) {
                size_t afterType = p + 5;
                while (afterType < block.size() && (block[afterType] == ' ' || block[afterType] == '\t' || block[afterType] == '\r' || block[afterType] == '\n')) afterType++;
                if (block.compare(afterType, 5, "/Page") == 0) {
                    if (afterType + 5 < block.size() && block[afterType + 5] != 's') {
                        pageTokenCount++;
                    }
                }
                p += 5;
            }
            carry = (block.size() > 64) ? block.substr(block.size() - 64) : block;
        }

        SDL_CloseIO(stream);
        return pageTokenCount > 0 ? pageTokenCount : 1;
    }
};
