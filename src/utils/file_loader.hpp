#pragma once
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include "utils/logger.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Folio {

/**
 * @brief Converts a UTF-8 encoded string to a native std::filesystem::path.
 *
 * Unicode Conversion Mechanics on Windows:
 * Standard C++ `std::filesystem::path(const std::string&)` assumes the native narrow
 * encoding (the system ANSI code page CP_ACP, e.g. Windows-1252), NOT UTF-8.
 * Passing UTF-8 strings containing multi-byte characters (such as copyright ©,
 * registered trademark ®, accented letters, CJK characters, or emoji) directly to
 * `std::filesystem::path` results in mojibake. For example, the UTF-8 sequence
 * `0xC2 0xAE` ('®') gets misparsed as two ANSI characters ('Â®'), causing Windows
 * kernel file APIs (GetFileAttributesW, CreateFileW) to return ERROR_FILE_NOT_FOUND (2).
 *
 * This function converts the UTF-8 string to a wide std::wstring via Win32 MultiByteToWideChar(CP_UTF8),
 * which is then used to construct the std::filesystem::path natively with complete Unicode fidelity.
 * On Linux, macOS, and Android where file paths are natively UTF-8, direct path construction is used.
 *
 * @param utf8Str UTF-8 encoded string representing a disk or relative path.
 * @return std::filesystem::path correctly representing the path.
 */
inline std::filesystem::path Utf8ToPath(const std::string& utf8Str) {
    if (utf8Str.empty()) return std::filesystem::path();
#if defined(_WIN32)
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return std::filesystem::path(utf8Str);
    std::wstring wstr(static_cast<size_t>(sizeNeeded), 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), &wstr[0], sizeNeeded);
    return std::filesystem::path(std::move(wstr));
#else
    return std::filesystem::path(utf8Str);
#endif
}

/**
 * @brief Converts a std::filesystem::path to a UTF-8 encoded std::string.
 *
 * Unicode Conversion Mechanics on Windows:
 * Standard `path.string()` on Windows converts the path's wide characters back into the
 * native ANSI code page (CP_ACP), replacing any non-ANSI Unicode characters with '?'
 * or corrupting bytes.
 *
 * This function converts the wide path (path.wstring()) into a UTF-8 encoded std::string
 * via Win32 WideCharToMultiByte(CP_UTF8), preserving all characters and diacritics.
 * On POSIX systems, `path.string()` is already UTF-8 and is returned directly.
 *
 * @param p The filesystem path to convert.
 * @return UTF-8 encoded std::string.
 */
inline std::string PathToUtf8(const std::filesystem::path& p) {
#if defined(_WIN32)
    const std::wstring& wstr = p.native();
    if (wstr.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return p.string();
    std::string str(static_cast<size_t>(sizeNeeded), 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &str[0], sizeNeeded, nullptr, nullptr);
    return str;
#else
    return p.string();
#endif
}

} // namespace Folio

/**
 * @class FileLoader
 * @brief Helper utility for loading and streaming cross-platform application files
 */
class FileLoader {
public:
    static inline std::filesystem::path Utf8ToPath(const std::string& utf8Str) {
        return Folio::Utf8ToPath(utf8Str);
    }
    static inline std::string PathToUtf8(const std::filesystem::path& p) {
        return Folio::PathToUtf8(p);
    }

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
     * @param assetRelativePath - The path to the file to write (UTF-8 encoded).
     * @param content - The text content to write.
     * @return true if the file was written successfully, false otherwise.
     */
    static bool WriteString(const std::string& assetRelativePath, const std::string& content) {
        std::error_code ec;
        std::filesystem::path dirPath = Folio::Utf8ToPath(assetRelativePath).parent_path();
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
     *
     * Mathematical Algorithm (64-bit FNV-1a):
     * - Offset basis: 14695981039346656037 (0xcbf29ce484222325)
     * - Prime multiplier: 1099511628211 (0x100000001b3)
     * - State update: hash = (hash ^ byte) * prime (mod 2^64)
     *
     * Working Process:
     * 1. Opens file stream via SDL_IOFromFile.
     * 2. On Windows, if SDL_IOFromFile is unable to open a Unicode file path, falls back
     *    to wide-character `_wfopen` via Folio::Utf8ToPath.
     * 3. Streams data in 64KB blocks, updating the running FNV-1a hash and cumulative byte count.
     * 4. Closes file handle safely and returns status.
     *
     * @param filePath Full UTF-8 path to the file on disk.
     * @param outHash Output parameter receiving the 64-bit FNV-1a hash.
     * @param outSize Output parameter receiving total file size in bytes.
     * @return true if file was read and hashed successfully; false if inaccessible.
     */
    static bool ComputeFileHash64(const std::string& filePath, uint64_t& outHash, uint64_t& outSize) {
        outHash = 14695981039346656037ULL;
        outSize = 0;
        SDL_IOStream* stream = SDL_IOFromFile(filePath.c_str(), "rb");
        if (stream) {
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

#if defined(_WIN32)
        // Fallback for Windows non-ASCII/Unicode paths
        FILE* fp = _wfopen(Folio::Utf8ToPath(filePath).wstring().c_str(), L"rb");
        if (fp) {
            uint8_t buffer[65536];
            size_t bytesRead = 0;
            while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                outSize += bytesRead;
                for (size_t i = 0; i < bytesRead; ++i) {
                    outHash ^= buffer[i];
                    outHash *= 1099511628211ULL;
                }
            }
            fclose(fp);
            return true;
        }
#endif

        return false;
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
