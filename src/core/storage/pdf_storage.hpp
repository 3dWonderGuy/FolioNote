#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include "utils/file_loader.hpp"
#include "utils/logger.hpp"
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
     * 1. Validates that the input source file exists and is accessible.
     * 2. Computes a 64-bit content hash for package deduplication.
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
        std::error_code ec;
        if (srcPath.empty() || !std::filesystem::exists(srcPath, ec)) {
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
        // Handles compressed object streams (/ObjStm), multi-level page trees, and linearized PDFs
        int pages = PdfRenderer::GetPageCount(srcPath);

        // 2. Fallback to lightweight byte scan if PDFium is not compiled or returned 0
        if (pages <= 0) {
            pages = FileLoader::DetectPdfPageCount(srcPath);
        }
        if (pages <= 0) {
            pages = 1;
        }

        outInfo.originalPath = srcPath;
        outInfo.originalFileName = std::filesystem::path(srcPath).filename().string();
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

        std::error_code ec;
        std::filesystem::path pkgPath(activeNb->filePath);
        std::filesystem::path pdfDir = pkgPath / "imports" / "pdfs";
        std::filesystem::create_directories(pdfDir, ec);

        std::string filename = std::string("pdf_") + outInfo.contentHash + ".pdf";
        std::filesystem::path destFile = pdfDir / filename;

        // Copy only if not already deduplicated
        if (!std::filesystem::exists(destFile, ec)) {
            std::filesystem::copy_file(srcPath, destFile, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                LOG_ERROR(PdfStorage, "Failed to copy PDF into imports/pdfs/: " + ec.message());
                return false;
            }
            LOG_INFO(PdfStorage, "Copied new PDF to package: " + destFile.string());
        } else {
            LOG_INFO(PdfStorage, "PDF content hash already exists in package, deduplicating: " + filename);
        }

        outInfo.packagePath = (std::filesystem::path("imports") / "pdfs" / filename).string();
        outInfo.diskPath = destFile.string();
        outInfo.isExternal = false;
        return true;
    }

    /**
     * @brief Resolves a stored packagePath (relative or absolute) to a full accessible disk path.
     */
    static std::string ResolveDiskPath(const std::string& packagePath, const DocumentSession* session) {
        if (packagePath.empty()) return "";

        std::filesystem::path p(packagePath);
        if (p.is_absolute()) {
            return packagePath;
        }

        if (session) {
            auto activeNb = session->workspace.GetActiveNotebook();
            if (activeNb && !activeNb->filePath.empty()) {
                return (std::filesystem::path(activeNb->filePath) / p).string();
            }
        }

        return packagePath;
    }
};

} // namespace Folio
