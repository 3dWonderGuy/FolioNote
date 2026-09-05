#pragma once
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cstdint>
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
};
