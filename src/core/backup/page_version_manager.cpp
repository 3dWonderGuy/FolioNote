/**
 * =========================================================================================
 * @file page_version_manager.cpp
 * @brief Implementation of Google Docs-Style Page Revision History & Rollback Engine
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Independent Binary Page Blobs:
 *    Each revision is serialized as a self-contained, CRC32-checksummed, zlib-compressed
 *    .ink payload. Stored under `<notebookPath>/revisions/<pageGuid>/<versionId>.ink`.
 * 2. Self-Documenting Telemetry:
 *    Sidecar metadata (`<versionId>.meta`) tracks author, milestone names, epoch timestamps,
 *    stroke counts, object counts, and byte sizes.
 * 3. Non-Destructive Rollback (Pre-Rollback Safety Checkpoint):
 *    Before any historical version is restored, an automatic safety revision of the active page
 *    is captured first. A user can never accidentally destroy uncommitted work.
 * 4. Dual Retention Model:
 *    - Named Milestone Versions (`isNamed == true`): Permanently pinned.
 *    - Unnamed Session Snapshots: Pruned via rolling FIFO window according to `maxUnnamedToKeep`.
 */

#include "core/backup/page_version_manager.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <filesystem>

#include "core/document/canvas_page.hpp"
#include "core/storage/binary_serializer.hpp"
#include "io/file_manager.hpp"
#include "utils/logger.hpp"

namespace Folio {

namespace {

/**
 * @brief Generates formatted human-readable timestamp string ("YYYY-MM-DD HH:MM:SS").
 */
std::string GetFormattedTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmVal{};
#if defined(_WIN32)
    localtime_s(&tmVal, &tt);
#else
    localtime_r(&tt, &tmVal);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmVal, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/**
 * @brief Writes atomic INI metadata marker for a page revision.
 */
bool WriteRevisionMeta(const std::string& metaPath, const PageRevisionInfo& info) {
    std::ostringstream oss;
    oss << "# FolioNote Page Revision Metadata\n";
    oss << "format=FolioNotePageRevision\n";
    oss << "version=1\n";
    oss << "id=" << info.versionId << "\n";
    oss << "pageGuid=" << info.pageGuid << "\n";
    oss << "name=" << info.versionName << "\n";
    oss << "timestamp=" << info.timestamp << "\n";
    oss << "timestampStr=" << info.timestampStr << "\n";
    oss << "strokeCount=" << info.strokeCount << "\n";
    oss << "objectCount=" << info.objectCount << "\n";
    oss << "fileSizeBytes=" << info.fileSizeBytes << "\n";
    oss << "isNamed=" << (info.isNamed ? "1" : "0") << "\n";

    return FileManager::WriteTextAtomic(metaPath, oss.str());
}

/**
 * @brief Parses an INI metadata marker into a PageRevisionInfo record.
 */
bool ParseRevisionMeta(const std::string& metaPath, PageRevisionInfo& outInfo) {
    std::string content;
    if (!FileManager::ReadText(metaPath, content)) {
        return false;
    }

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "id") outInfo.versionId = val;
        else if (key == "pageGuid") outInfo.pageGuid = val;
        else if (key == "name") outInfo.versionName = val;
        else if (key == "timestamp") {
            try { outInfo.timestamp = std::stoull(val); } catch (...) {}
        }
        else if (key == "timestampStr") outInfo.timestampStr = val;
        else if (key == "strokeCount") {
            try { outInfo.strokeCount = static_cast<uint32_t>(std::stoul(val)); } catch (...) {}
        }
        else if (key == "objectCount") {
            try { outInfo.objectCount = static_cast<uint32_t>(std::stoul(val)); } catch (...) {}
        }
        else if (key == "fileSizeBytes") {
            try { outInfo.fileSizeBytes = std::stoull(val); } catch (...) {}
        }
        else if (key == "isNamed") outInfo.isNamed = (val == "1" || val == "true");
    }

    return !outInfo.versionId.empty();
}

} // anonymous namespace

// =========================================================================================
// PageVersionManager Implementation
// =========================================================================================

std::string PageVersionManager::GetRevisionsDirectory(const std::string& notebookPath, const std::string& pageGuid) {
    return FileManager::JoinPath(notebookPath, "revisions/" + pageGuid);
}

bool PageVersionManager::CreateRevision(
    const CanvasPage& page,
    const std::string& notebookPath,
    const std::string& versionName,
    bool isNamed,
    PageRevisionInfo* outInfo
) {
    if (page.guid.empty() || notebookPath.empty() || !FileManager::IsDirectory(notebookPath)) {
        LOG_ERROR(FileManager, "PageVersionManager::CreateRevision: Invalid notebook path or empty page GUID.");
        return false;
    }

    std::string revisionsDir = GetRevisionsDirectory(notebookPath, page.guid);
    if (!FileManager::CreateDirectories(revisionsDir)) {
        LOG_ERROR(FileManager, "PageVersionManager::CreateRevision: Failed to create revisions dir: " + revisionsDir);
        return false;
    }

    auto now = std::chrono::system_clock::now();
    uint64_t epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string versionId = "rev_" + std::to_string(epochMs);

    // 1. Serialize page into compressed .ink payload
    std::vector<uint8_t> compressedBlob;
    SerializationStats stats{};
    if (!BinarySerializer::SerializePage(page, compressedBlob, &stats)) {
        LOG_ERROR(FileManager, "PageVersionManager::CreateRevision: Failed to serialize page to .ink binary.");
        return false;
    }

    // 2. Write binary blob atomically to disk
    std::string blobPath = FileManager::JoinPath(revisionsDir, versionId + ".ink");
    {
        std::string tmpPath = blobPath + ".tmp";
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            LOG_ERROR(FileManager, "PageVersionManager::CreateRevision: Cannot open temp file: " + tmpPath);
            return false;
        }
        out.write(reinterpret_cast<const char*>(compressedBlob.data()), compressedBlob.size());
        out.close();

        std::error_code ec;
        std::filesystem::rename(tmpPath, blobPath, ec);
        if (ec) {
            LOG_ERROR(FileManager, "PageVersionManager::CreateRevision: Rename failed: " + ec.message());
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

    // 3. Populate and write metadata sidecar
    PageRevisionInfo info;
    info.versionId = versionId;
    info.pageGuid = page.guid;
    info.versionName = versionName.empty() ? (isNamed ? "Named Milestone" : "Session Snapshot") : versionName;
    info.timestampStr = GetFormattedTimestamp();
    info.timestamp = epochMs;
    info.strokeCount = stats.strokeCount;
    info.objectCount = stats.objectCount;
    info.fileSizeBytes = compressedBlob.size();
    info.isNamed = isNamed;

    std::string metaPath = FileManager::JoinPath(revisionsDir, versionId + ".meta");
    if (!WriteRevisionMeta(metaPath, info)) {
        LOG_ERROR(FileManager, "PageVersionManager::CreateRevision: Failed to write metadata: " + metaPath);
        std::error_code ec;
        std::filesystem::remove(blobPath, ec);
        return false;
    }

    if (outInfo) {
        *outInfo = info;
    }

    LOG_INFO(FileManager, "PageVersionManager: Created revision '" + info.versionName + "' [" + versionId +
             "] for page: " + page.guid + " (" + std::to_string(info.fileSizeBytes) + " bytes)");
    return true;
}

std::vector<PageRevisionInfo> PageVersionManager::ListRevisions(const std::string& notebookPath, const std::string& pageGuid) {
    std::vector<PageRevisionInfo> revisions;
    std::string revisionsDir = GetRevisionsDirectory(notebookPath, pageGuid);

    std::error_code ec;
    if (!std::filesystem::exists(revisionsDir, ec) || !std::filesystem::is_directory(revisionsDir, ec)) {
        return revisions;
    }

    for (const auto& entry : std::filesystem::directory_iterator(revisionsDir, ec)) {
        if (std::filesystem::is_regular_file(entry.status()) && entry.path().extension() == ".meta") {
            PageRevisionInfo info;
            if (ParseRevisionMeta(entry.path().string(), info)) {
                // Verify matching binary blob exists
                std::string blobPath = FileManager::JoinPath(revisionsDir, info.versionId + ".ink");
                if (FileManager::Exists(blobPath)) {
                    revisions.push_back(info);
                }
            }
        }
    }

    // Sort descending: newest versions first
    std::sort(revisions.begin(), revisions.end(), [](const PageRevisionInfo& a, const PageRevisionInfo& b) {
        return a.timestamp > b.timestamp;
    });

    return revisions;
}

bool PageVersionManager::RestoreRevision(
    CanvasPage& targetPage,
    const std::string& notebookPath,
    const std::string& versionId,
    bool createSafetySnapshot
) {
    if (notebookPath.empty() || versionId.empty() || targetPage.guid.empty()) {
        LOG_ERROR(FileManager, "PageVersionManager::RestoreRevision: Invalid arguments.");
        return false;
    }

    std::string revisionsDir = GetRevisionsDirectory(notebookPath, targetPage.guid);
    std::string blobPath = FileManager::JoinPath(revisionsDir, versionId + ".ink");

    if (!FileManager::Exists(blobPath)) {
        LOG_ERROR(FileManager, "PageVersionManager::RestoreRevision: Target revision BLOB missing: " + blobPath);
        return false;
    }

    // Capture safety snapshot of current page state first so no edits are destroyed
    if (createSafetySnapshot && targetPage.isLoaded && !targetPage.objects.empty()) {
        CreateRevision(targetPage, notebookPath, "Safety Checkpoint (Pre-Rollback)", false);
    }

    // Read binary .ink blob
    std::ifstream in(blobPath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        LOG_ERROR(FileManager, "PageVersionManager::RestoreRevision: Cannot read blob: " + blobPath);
        return false;
    }

    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> blobData(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(blobData.data()), size)) {
        LOG_ERROR(FileManager, "PageVersionManager::RestoreRevision: Read failed for: " + blobPath);
        return false;
    }
    in.close();

    // Deserialize into targetPage
    SerializationStats stats{};
    if (!BinarySerializer::DeserializePage(blobData.data(), blobData.size(), targetPage, &stats)) {
        LOG_ERROR(FileManager, "PageVersionManager::RestoreRevision: Deserialization failed for: " + blobPath);
        return false;
    }

    targetPage.isLoaded = true;
    targetPage.isModified = true;
    targetPage.Touch();

    LOG_INFO(FileManager, "PageVersionManager: Successfully restored revision [" + versionId +
             "] for page: " + targetPage.guid);
    return true;
}

bool PageVersionManager::RenameRevision(
    const std::string& notebookPath,
    const std::string& pageGuid,
    const std::string& versionId,
    const std::string& newName
) {
    std::string revisionsDir = GetRevisionsDirectory(notebookPath, pageGuid);
    std::string metaPath = FileManager::JoinPath(revisionsDir, versionId + ".meta");

    PageRevisionInfo info;
    if (!ParseRevisionMeta(metaPath, info)) {
        LOG_ERROR(FileManager, "PageVersionManager::RenameRevision: Metadata missing for: " + metaPath);
        return false;
    }

    info.versionName = newName;
    info.isNamed = true; // Pin as named milestone version

    return WriteRevisionMeta(metaPath, info);
}

bool PageVersionManager::DeleteRevision(
    const std::string& notebookPath,
    const std::string& pageGuid,
    const std::string& versionId
) {
    std::string revisionsDir = GetRevisionsDirectory(notebookPath, pageGuid);
    std::string blobPath = FileManager::JoinPath(revisionsDir, versionId + ".ink");
    std::string metaPath = FileManager::JoinPath(revisionsDir, versionId + ".meta");

    std::error_code ec;
    std::filesystem::remove(blobPath, ec);
    std::filesystem::remove(metaPath, ec);

    LOG_INFO(FileManager, "PageVersionManager: Deleted revision [" + versionId + "] for page: " + pageGuid);
    return true;
}

size_t PageVersionManager::PruneRevisions(
    const std::string& notebookPath,
    const std::string& pageGuid,
    size_t maxUnnamedToKeep
) {
    // 0 means unlimited retention; never prune
    if (maxUnnamedToKeep == 0) {
        return 0;
    }

    auto allRevisions = ListRevisions(notebookPath, pageGuid);
    if (allRevisions.empty()) {
        return 0;
    }

    // Filter only unnamed revisions (named milestone versions are protected and permanently retained)
    std::vector<PageRevisionInfo> unnamed;
    for (const auto& rev : allRevisions) {
        if (!rev.isNamed) {
            unnamed.push_back(rev);
        }
    }

    if (unnamed.size() <= maxUnnamedToKeep) {
        return 0;
    }

    // unnamed is sorted newest first; purge from maxUnnamedToKeep index onwards
    size_t prunedCount = 0;
    for (size_t i = maxUnnamedToKeep; i < unnamed.size(); ++i) {
        if (DeleteRevision(notebookPath, pageGuid, unnamed[i].versionId)) {
            prunedCount++;
        }
    }

    LOG_INFO(FileManager, "PageVersionManager: Pruned " + std::to_string(prunedCount) +
             " obsolete unnamed revisions for page: " + pageGuid + " (Capped at " +
             std::to_string(maxUnnamedToKeep) + ")");
    return prunedCount;
}

} // namespace Folio
