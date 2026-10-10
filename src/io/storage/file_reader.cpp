#include "io/storage/file_reader.hpp"
#include "io/paths/path_utils.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <string_view>
#include <cctype>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Folio {

// =========================================================================================
// MemoryMappedView Lifecycle
// =========================================================================================

MemoryMappedView::~MemoryMappedView() {
    Reset();
}

MemoryMappedView::MemoryMappedView(MemoryMappedView&& other) noexcept
    : data(other.data)
    , size(other.size)
    , m_fileHandle(other.m_fileHandle)
    , m_mappingHandle(other.m_mappingHandle)
    , m_fd(other.m_fd)
{
    other.data = nullptr;
    other.size = 0;
    other.m_fileHandle = nullptr;
    other.m_mappingHandle = nullptr;
    other.m_fd = -1;
}

MemoryMappedView& MemoryMappedView::operator=(MemoryMappedView&& other) noexcept {
    if (this != &other) {
        Reset();
        data = other.data;
        size = other.size;
        m_fileHandle = other.m_fileHandle;
        m_mappingHandle = other.m_mappingHandle;
        m_fd = other.m_fd;

        other.data = nullptr;
        other.size = 0;
        other.m_fileHandle = nullptr;
        other.m_mappingHandle = nullptr;
        other.m_fd = -1;
    }
    return *this;
}

void MemoryMappedView::Reset() noexcept {
#if defined(_WIN32)
    if (data) {
        UnmapViewOfFile(data);
        data = nullptr;
    }
    if (m_mappingHandle) {
        CloseHandle(static_cast<HANDLE>(m_mappingHandle));
        m_mappingHandle = nullptr;
    }
    if (m_fileHandle) {
        CloseHandle(static_cast<HANDLE>(m_fileHandle));
        m_fileHandle = nullptr;
    }
#else
    if (data && size > 0) {
        munmap(const_cast<uint8_t*>(data), size);
        data = nullptr;
    }
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
#endif
    size = 0;
}

MemoryMappedView FileReader::MapFileReadOnly(const std::string& path) {
    MemoryMappedView view;
    if (path.empty()) return view;

#if defined(_WIN32)
    auto nativeP = PathUtils::Utf8ToNativePath(path);
    HANDLE hFile = CreateFileW(
        nativeP.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return view;
    }

    LARGE_INTEGER li;
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart <= 0) {
        CloseHandle(hFile);
        return view;
    }

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) {
        CloseHandle(hFile);
        return view;
    }

    void* mapped = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!mapped) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return view;
    }

    view.data = static_cast<const uint8_t*>(mapped);
    view.size = static_cast<size_t>(li.QuadPart);
    view.m_fileHandle = static_cast<void*>(hFile);
    view.m_mappingHandle = static_cast<void*>(hMap);
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return view;
    }

    struct stat sb;
    if (fstat(fd, &sb) < 0 || sb.st_size <= 0) {
        close(fd);
        return view;
    }

    void* mapped = mmap(nullptr, static_cast<size_t>(sb.st_size), PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return view;
    }

    view.data = static_cast<const uint8_t*>(mapped);
    view.size = static_cast<size_t>(sb.st_size);
    view.m_fd = fd;
#endif

    return view;
}

// =========================================================================================
// Reading & Streaming
// =========================================================================================

bool FileReader::ReadToString(const std::string& path, std::string& outString) {
    std::vector<uint8_t> buffer;
    if (!ReadToBuffer(path, buffer)) {
        return false;
    }
    outString.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    return true;
}

bool FileReader::ReadToBuffer(const std::string& path, std::vector<uint8_t>& outBuffer) {
    outBuffer.clear();
    if (path.empty()) return false;

    size_t dataSize = 0;
    void* rawData = SDL_LoadFile(path.c_str(), &dataSize);
    if (rawData) {
        outBuffer.resize(dataSize);
        if (dataSize > 0) {
            std::memcpy(outBuffer.data(), rawData, dataSize);
        }
        SDL_free(rawData);
        return true;
    }

#if defined(_WIN32)
    FILE* fp = _wfopen(PathUtils::Utf8ToNativePath(path).wstring().c_str(), L"rb");
    if (fp) {
        _fseeki64(fp, 0, SEEK_END);
        int64_t sz = _ftelli64(fp);
        _fseeki64(fp, 0, SEEK_SET);

        if (sz >= 0) {
            outBuffer.resize(static_cast<size_t>(sz));
            if (sz > 0) {
                fread(outBuffer.data(), 1, static_cast<size_t>(sz), fp);
            }
            fclose(fp);
            return true;
        }
        fclose(fp);
    }
#endif

    LOG_ERROR(FileManager, FormatError(FolioErrorCode::SysFileReadFailed, "ReadBinary failed to open or read file: " + path));
    return false;
}

SDL_IOStream* FileReader::OpenAsStream(const std::string& path) noexcept {
    if (path.empty()) return nullptr;
    return SDL_IOFromFile(path.c_str(), "rb");
}

bool FileReader::Exists(const std::string& path) noexcept {
    if (path.empty()) return false;

    std::error_code ec;
    auto nativeP = PathUtils::Utf8ToNativePath(path);
    if (std::filesystem::exists(nativeP, ec)) {
        return true;
    }

    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb");
    if (stream) {
        SDL_CloseIO(stream);
        return true;
    }
    return false;
}

bool FileReader::ComputeFileHash64(const std::string& path, uint64_t& outHash, uint64_t& outSize) {
    outHash = 14695981039346656037ULL;
    outSize = 0;

    SDL_IOStream* stream = OpenAsStream(path);
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
    FILE* fp = _wfopen(PathUtils::Utf8ToNativePath(path).wstring().c_str(), L"rb");
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

int FileReader::DetectPdfPageCount(const std::string& path) {
    SDL_IOStream* stream = OpenAsStream(path);
    if (!stream) return 1;

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

    size_t headScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 4096));
    SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
    std::vector<char> headBuf(headScanSize + 1, 0);
    SDL_ReadIO(stream, headBuf.data(), headScanSize);
    std::string_view headView(headBuf.data(), headScanSize);

    size_t linPos = headView.find("/Linearized");
    if (linPos != std::string_view::npos) {
        size_t nPos = headView.find("/N", linPos);
        if (nPos != std::string_view::npos) {
            size_t numStart = nPos + 2;
            while (numStart < headView.size() && (headView[numStart] == ' ' || headView[numStart] == '\t' || headView[numStart] == '\r' || headView[numStart] == '\n')) {
                numStart++;
            }
            if (numStart < headView.size() && std::isdigit(static_cast<unsigned char>(headView[numStart]))) {
                try {
                    int linCount = std::stoi(std::string(headView.substr(numStart, 10)));
                    if (linCount > 0) {
                        SDL_CloseIO(stream);
                        return linCount;
                    }
                } catch (...) {}
            }
        }
    }

    size_t tailScanSize = static_cast<size_t>(std::min<Sint64>(fileSize, 65536));
    SDL_SeekIO(stream, fileSize - tailScanSize, SDL_IO_SEEK_SET);
    std::vector<char> buffer(tailScanSize + 1, 0);
    SDL_ReadIO(stream, buffer.data(), tailScanSize);

    std::string_view tailView(buffer.data(), tailScanSize);
    int maxPagesFound = 0;
    size_t countPos = 0;

    while ((countPos = tailView.find("/Count", countPos)) != std::string_view::npos) {
        size_t ctxStart = (countPos >= 80) ? countPos - 80 : 0;
        size_t ctxEnd = std::min(tailView.size(), countPos + 80);
        std::string_view ctx = tailView.substr(ctxStart, ctxEnd - ctxStart);

        bool isOutline = (ctx.find("/Title") != std::string_view::npos || ctx.find("/Dest") != std::string_view::npos);
        bool isPagesNode = (ctx.find("/Pages") != std::string_view::npos);

        if (!isOutline && isPagesNode) {
            size_t numStart = countPos + 6;
            while (numStart < tailView.size() && (tailView[numStart] == ' ' || tailView[numStart] == '\t' || tailView[numStart] == '\r' || tailView[numStart] == '\n')) {
                numStart++;
            }
            if (numStart < tailView.size() && std::isdigit(static_cast<unsigned char>(tailView[numStart]))) {
                try {
                    int countVal = std::stoi(std::string(tailView.substr(numStart, 10)));
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

    SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
    int pageTokenCount = 0;
    constexpr size_t CHUNK_SIZE = 32768;
    std::vector<char> chunk(CHUNK_SIZE + 64, 0);
    size_t carrySize = 0;

    while (true) {
        size_t readCount = SDL_ReadIO(stream, chunk.data() + carrySize, CHUNK_SIZE);
        size_t totalBytes = carrySize + readCount;
        if (totalBytes == 0) break;

        std::string_view block(chunk.data(), totalBytes);
        size_t p = 0;
        while ((p = block.find("/Type", p)) != std::string_view::npos) {
            size_t afterType = p + 5;
            while (afterType < block.size() && (block[afterType] == ' ' || block[afterType] == '\t' || block[afterType] == '\r' || block[afterType] == '\n')) {
                afterType++;
            }
            if (block.substr(afterType).starts_with("/Page")) {
                if (afterType + 5 < block.size() && block[afterType + 5] != 's') {
                    pageTokenCount++;
                }
            }
            p += 5;
        }

        if (readCount == 0) break;

        carrySize = std::min<size_t>(totalBytes, 64);
        std::memmove(chunk.data(), chunk.data() + totalBytes - carrySize, carrySize);
    }

    SDL_CloseIO(stream);
    return pageTokenCount > 0 ? pageTokenCount : 1;
}

} // namespace Folio
