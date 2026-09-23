#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <algorithm>

#include "io/file_reader.hpp"
#include "utils/logger.hpp"
#include "io/file_manager.hpp"
#include "core/document/document_session.hpp"
#include "core/render/pdf_renderer.hpp"

namespace Folio {

enum class PdfImportMode {
    LocalCopy = 0,    // Deduplicated inside notebook package: imports/pdfs/pdf_<hash>.pdf (portable)
    ExternalLink = 1  // References external path directly (saves disk space, but shows warning about moved/renamed files)
};

struct PdfDocumentInfo {
    std::string originalPath;       // Absolute path from which the file was loaded
    std::string originalFileName;   // e.g. "Lecture3_Calculus.pdf"
    std::string packagePath;        // Relative path in package ("imports/pdfs/pdf_<hash>.pdf") or external path
    std::string diskPath;           // Direct absolute path on disk to load/render from
    std::string contentHash;        // Hex string of 64-bit content hash
    uint64_t fileSizeBytes = 0;
    int pageCount = 1;
    bool isExternal = false;
    bool isLongDocument = false;    // Page count >= 20: recommended for dedicated PDF viewer mode
    std::string warningMessage;     // Warnings (e.g. external link volatility or document size)
};

class PdfStorage {
public:
    /**
     * @brief Inspects an external PDF file without modifying it, returning metadata,
     * detected page count, and recommendation flags.
     *
     * Working Process:
     * 1. Validates that the input source file exists via FileManager::Exists.
     * 2. Computes a 64-bit FNV-1a content hash for package deduplication.
     * 3. Uses PdfRenderer::GetPageCount (PDFium) to accurately parse the document catalog,
     *    page tree B-tree, compressed object streams (/ObjStm), and cross-reference streams.
     * 4. If PDFium is unavailable or fails, gracefully falls back to FileLoader::DetectPdfPageCount.
     * 5. Flags documents with >= 20 pages as long documents and populates recommendation warnings.
     *
     * @param srcPath UTF-8 path to the PDF on disk.
     * @param outInfo Destination struct for populated metadata and page count.
     * @return true on successful inspection, false if file does not exist or cannot be read.
     */
    static bool InspectPdf(const std::string& srcPath, PdfDocumentInfo& outInfo) {
        if (srcPath.empty() || !FileManager::Exists(srcPath)) {
            LOG_WARN(PdfStorage, "PDF file does not exist: " + srcPath);
            return false;
        }

        uint64_t hash = 0;
        uint64_t sz = 0;
        if (!FileLoader::ComputeFileHash64(srcPath, hash, sz)) {
            LOG_ERROR(PdfStorage, "Failed to compute content hash for: " + srcPath);
            return false;
        }

        char hashStr[32];
        std::snprintf(hashStr, sizeof(hashStr), "%016llx", static_cast<unsigned long long>(hash));

        // 1. Primary inspection: Query page count via PDFium engine
        int pages = PdfRenderer::GetPageCount(srcPath);

        // 2. Fallback to lightweight byte scan if PDFium is not compiled or returned 0
        if (pages <= 0) {
            pages = FileLoader::DetectPdfPageCount(srcPath);
        }
        if (pages <= 0) {
            pages = 1;
        }

        outInfo.originalPath = srcPath;
        outInfo.originalFileName = FileManager::GetFileName(srcPath);
        outInfo.contentHash = hashStr;
        outInfo.fileSizeBytes = sz;
        outInfo.pageCount = pages;
        outInfo.isLongDocument = (pages >= 20);
        outInfo.isExternal = false;
        outInfo.diskPath = srcPath;
        outInfo.warningMessage.clear();

        if (outInfo.isLongDocument) {
            outInfo.warningMessage = "This PDF has " + std::to_string(pages) +
                " pages. Highly recommended to import as a dedicated PDF Viewer page rather than spreading onto canvas.";
        }

        LOG_INFO(PdfStorage, "Inspected PDF: '" + outInfo.originalFileName + "', pages=" +
                 std::to_string(pages) + ", size=" + std::to_string(sz) + " bytes, hash=" + hashStr);
        return true;
    }

    /**
     * @brief Ingests a PDF document into the notebook package, either copying and deduplicating
     * into imports/pdfs/ or linking externally based on mode.
     *
     * Working Process:
     * 1. Calls InspectPdf to validate source file and extract content hash.
     * 2. For ExternalLink mode, stores direct absolute path without copying bytes.
     * 3. For LocalCopy mode, ensures destination directory <notebookPkg>/imports/pdfs exists.
     * 4. Copies the source file into imports/pdfs/pdf_<contentHash>.pdf via FileManager::CopyFileTo.
     *    (Named CopyFileTo rather than CopyFile to avoid the Win32 CopyFileA macro collision.)
     *
     * @param srcPath UTF-8 path to the source PDF file.
     * @param session Active document session providing notebook workspace root.
     * @param mode Local copy vs external link mode.
     * @param outInfo Destination struct receiving paths and metadata.
     * @return true if successfully ingested, false otherwise.
     */
    static bool IngestPdf(const std::string& srcPath, DocumentSession* session, PdfImportMode mode, PdfDocumentInfo& outInfo) {
        if (!InspectPdf(srcPath, outInfo)) {
            return false;
        }

        if (mode == PdfImportMode::ExternalLink) {
            outInfo.packagePath = srcPath;
            outInfo.diskPath = srcPath;
            outInfo.isExternal = true;
            outInfo.warningMessage = "Linked as external path. If the file is moved, renamed, or deleted, FolioNote will lose access.";
            LOG_INFO(PdfStorage, "Linked external PDF path: " + srcPath);
            return true;
        }

        // LocalCopy mode: deduplicate into active notebook's imports/pdfs/
        if (!session) {
            LOG_ERROR(PdfStorage, "Cannot ingest local copy without active DocumentSession");
            return false;
        }

        auto activeNb = session->workspace.GetActiveNotebook();
        if (!activeNb || activeNb->filePath.empty()) {
            LOG_ERROR(PdfStorage, "No active notebook found with valid directory path");
            return false;
        }

        std::string importsDir = FileManager::JoinPath(activeNb->filePath, "imports");
        std::string pdfDir = FileManager::JoinPath(importsDir, "pdfs");
        FileManager::CreateDirectories(pdfDir);

        std::string filename = std::string("pdf_") + outInfo.contentHash + ".pdf";
        std::string destFile = FileManager::JoinPath(pdfDir, filename);

        // Copy only if not already deduplicated
        // NOTE: Using CopyFileTo (not CopyFile) to avoid Win32 CopyFileA macro expansion
        if (!FileManager::Exists(destFile)) {
            if (!FileManager::CopyFileTo(srcPath, destFile)) {
                LOG_ERROR(PdfStorage, FormatError(FolioErrorCode::SysFileWriteFailed, 
                    "Failed to copy PDF into imports/pdfs/: " + destFile));
                return false;
            }
            LOG_INFO(PdfStorage, "Copied new PDF to package: " + destFile);
        } else {
            LOG_INFO(PdfStorage, "PDF content hash already exists in package, deduplicating: " + filename);
        }

        outInfo.packagePath = FileManager::JoinPath(FileManager::JoinPath("imports", "pdfs"), filename);
        outInfo.diskPath = destFile;
        outInfo.isExternal = false;
        return true;
    }

    /**
     * @brief Resolves a stored packagePath (relative or absolute) to a full accessible disk path.
     *
     * Working Process:
     * 1. Checks if packagePath is already an absolute path (external link).
     * 2. If relative, prepends the active notebook directory root path via FileManager::JoinPath.
     *
     * @param packagePath Relative package path or external absolute path.
     * @param session Pointer to active document session for notebook directory lookup.
     * @return Resolved absolute or relative disk path as UTF-8 string.
     */
    static std::string ResolveDiskPath(const std::string& packagePath, const DocumentSession* session) {
        if (packagePath.empty()) return "";

        if (FileManager::IsAbsolutePath(packagePath)) {
            return packagePath;
        }

        if (session) {
            auto activeNb = session->workspace.GetActiveNotebook();
            if (activeNb && !activeNb->filePath.empty()) {
                return FileManager::JoinPath(activeNb->filePath, packagePath);
            }
        }

        return packagePath;
    }
};

} // namespace Folio