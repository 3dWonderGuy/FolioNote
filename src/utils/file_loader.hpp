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
     * @brief Fast, non-blocking fallback scanner to inspect a PDF file and extract its page count.
     * Uses SDL_IOStream for unified cross-platform file access (Windows, Linux, macOS, Android).
     *
     * Working Process & PDF Specification Details:
     * 1. Header Validation: Confirms standard '%PDF-' magic bytes at byte offset 0.
     * 2. Linearized Fast Web View Detection:
     *    Per ISO 32000-1 Section 10.2.2 ('Linearization Parameter Dictionary'), linearized documents
     *    define a dictionary near the file beginning containing the key '/N <integer>', which specifies
     *    the exact total page count of the complete document. We scan the first 4KB for this key.
     * 3. Document Catalog Page Tree (/Pages) /Count Extraction:
     *    Standard non-linearized PDFs define a page tree with root dictionary '<< /Type /Pages /Count N >>'.
     *    We scan the trailing 64KB (where trailers and xref catalogs typically reside). To prevent false
     *    positives from outline bookmark items (which also use '/Count' to indicate child bookmarks, often
     *    leading to false 1-page reports), we strictly verify that the dictionary context contains '/Pages'
     *    and excludes outline tokens ('/Title', '/Dest'). We retain the maximum valid count found.
     * 4. Fallback Token Sweep:
     *    As a last resort for uncompressed PDFs without standard count headers, scans for '/Type /Page'
     *    tokens while strictly excluding '/Type /Pages'.
     *
     * @param filePath UTF-8 encoded file path.
     * @return Detected page count (>= 1).
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

        // ---------------------------------------------------------------------
        // 1. Check for Linearized PDF Parameter Dictionary in initial 4KB
        // ---------------------------------------------------------------------
        size_t headScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 4096));
        SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
        std::vector<char> headBuf(headScanSize + 1, 0);
        SDL_ReadIO(stream, headBuf.data(), headScanSize);
        std::string headStr(headBuf.data(), headScanSize);

        size_t linPos = headStr.find("/Linearized");
        if (linPos != std::string::npos) {
            // Find '/N' followed by whitespace and integer
            size_t nPos = headStr.find("/N", linPos);
            if (nPos != std::string::npos) {
                size_t numStart = nPos + 2;
                while (numStart < headStr.size() && (headStr[numStart] == ' ' || headStr[numStart] == '\t' || headStr[numStart] == '\r' || headStr[numStart] == '\n')) {
                    numStart++;
                }
                if (numStart < headStr.size() && std::isdigit(static_cast<unsigned char>(headStr[numStart]))) {
                    try {
                        int linCount = std::stoi(headStr.substr(numStart, 10));
                        if (linCount > 0) {
                            SDL_CloseIO(stream);
                            return linCount;
                        }
                    } catch (...) {}
                }
            }
        }

        // ---------------------------------------------------------------------
        // 2. Scan trailing 64KB for /Type /Pages /Count metadata
        // ---------------------------------------------------------------------
        size_t tailScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 65536));
        SDL_SeekIO(stream, fileSize - tailScanSize, SDL_IO_SEEK_SET);
        std::vector<char> buffer(tailScanSize + 1, 0);
        SDL_ReadIO(stream, buffer.data(), tailScanSize);

        std::string tailStr(buffer.data(), tailScanSize);
        int maxPagesFound = 0;
        size_t countPos = 0;

        while ((countPos = tailStr.find("/Count", countPos)) != std::string::npos) {
            // Context filter: Examine surrounding 80-byte neighborhood
            size_t ctxStart = (countPos >= 80) ? countPos - 80 : 0;
            size_t ctxEnd = std::min(tailStr.size(), countPos + 80);
            std::string ctx = tailStr.substr(ctxStart, ctxEnd - ctxStart);

            // Reject outline items (bookmarks containing '/Title', '/Dest')
            bool isOutline = (ctx.find("/Title") != std::string::npos || ctx.find("/Dest") != std::string::npos);
            bool isPagesNode = (ctx.find("/Pages") != std::string::npos);

            if (!isOutline && isPagesNode) {
                size_t numStart = countPos + 6;
                while (numStart < tailStr.size() && (tailStr[numStart] == ' ' || tailStr[numStart] == '\t' || tailStr[numStart] == '\r' || tailStr[numStart] == '\n')) {
                    numStart++;
                }
                if (numStart < tailStr.size() && std::isdigit(static_cast<unsigned char>(tailStr[numStart]))) {
                    try {
                        int countVal = std::stoi(tailStr.substr(numStart, 10));
                        if (countVal > maxPagesFound) {
                            maxPagesFound = countVal;
                        }
                    } catch (...) {}
                }
            }
            countPos += 6;
        }

        if (maxPagesFound > 0) {
            SDL_CloseIO(stream);
            return maxPagesFound;
        }

        // ---------------------------------------------------------------------
        // 3. Fallback: Full stream sweep counting "/Type /Page" tokens
        // ---------------------------------------------------------------------
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
