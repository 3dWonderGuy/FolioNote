#pragma once
/**
 * =========================================================================================
 * @file io/storage/file_reader.hpp
 * @brief Read-only cross-platform file reading, memory mapping, hashing, and stream acquisition
 * =========================================================================================
 */

#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace Folio {

/**
 * @class MemoryMappedView
 * @brief Zero-copy, read-only memory mapped file view for high-throughput asset loading.
 */
class MemoryMappedView {
public:
    const uint8_t* data = nullptr;
    size_t size = 0;

    [[nodiscard]] bool IsValid() const noexcept { return data != nullptr && size > 0; }

    MemoryMappedView() = default;
    ~MemoryMappedView();

    MemoryMappedView(const MemoryMappedView&) = delete;
    MemoryMappedView& operator=(const MemoryMappedView&) = delete;

    MemoryMappedView(MemoryMappedView&& other) noexcept;
    MemoryMappedView& operator=(MemoryMappedView&& other) noexcept;

    void Reset() noexcept;

private:
    void* m_fileHandle = nullptr;
    void* m_mappingHandle = nullptr;
    int m_fd = -1;

    friend class FileReader;
};

class FileReader {
public:
    /**
     * @brief Reads an entire binary file into memory as a raw byte vector.
     */
    static bool ReadToBuffer(const std::string& path, std::vector<uint8_t>& outBuffer);

    /**
     * @brief Reads an entire text file into memory as a UTF-8 string.
     */
    static bool ReadToString(const std::string& path, std::string& outString);

    /**
     * @brief Maps a file into address space for zero-copy read-only access.
     */
    [[nodiscard]] static MemoryMappedView MapFileReadOnly(const std::string& path);

    /**
     * @brief Opens a read-only cross-platform stream handle via SDL3 virtual filesystem.
     * Caller is responsible for closing via SDL_CloseIO(stream).
     */
    [[nodiscard]] static SDL_IOStream* OpenAsStream(const std::string& path) noexcept;

    /**
     * @brief Verifies whether a file or directory exists on disk or inside bundled package.
     */
    [[nodiscard]] static bool Exists(const std::string& path) noexcept;

    /**
     * @brief Computes a rapid 64-bit FNV-1a content hash and byte size for a file.
     */
    static bool ComputeFileHash64(const std::string& path, uint64_t& outHash, uint64_t& outSize);

    /**
     * @brief Non-blocking fast scan to determine the page count of a PDF file.
     */
    static int DetectPdfPageCount(const std::string& path);

    // Backward-compatibility aliases
    static bool ReadText(const std::string& path, std::string& outContent) {
        return ReadToString(path, outContent);
    }
    static bool ReadBinary(const std::string& path, std::vector<uint8_t>& outBuffer) {
        return ReadToBuffer(path, outBuffer);
    }
    [[nodiscard]] static SDL_IOStream* OpenReadStream(const std::string& path) noexcept {
        return OpenAsStream(path);
    }
};

} // namespace Folio

using FileLoader = Folio::FileReader;