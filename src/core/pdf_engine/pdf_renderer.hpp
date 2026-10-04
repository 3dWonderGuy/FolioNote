#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <blend2d/blend2d.h>

#include "core/pdf_engine/pdf_types.hpp"
#include "core/pdf_engine/pdf_text_layer.hpp"

#if defined(FOLIO_HAS_PDFIUM) && __has_include(<fpdfview.h>)
#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>
#endif

struct SDL_IOStream;

namespace Folio {

#if defined(FOLIO_HAS_PDFIUM)
/**
 * @brief RAII container managing FPDF_DOCUMENT lifecycles across platforms.
 * Supports both standard native FPDF_LoadDocument descriptors and custom
 * FPDF_FILEACCESS streams via SDL_IOStream to handle Unicode paths on Windows.
 * Automatically releases PDFium document handles and underlying file streams upon destruction.
 */
struct PdfDocHolder {
    FPDF_DOCUMENT doc = nullptr;
    SDL_IOStream* fileStream = nullptr;

    PdfDocHolder() = default;
    PdfDocHolder(FPDF_DOCUMENT d, SDL_IOStream* f = nullptr) noexcept : doc(d), fileStream(f) {}
    ~PdfDocHolder();

    // Move semantics (non-copyable)
    PdfDocHolder(const PdfDocHolder&) = delete;
    PdfDocHolder& operator=(const PdfDocHolder&) = delete;

    PdfDocHolder(PdfDocHolder&& other) noexcept;
    PdfDocHolder& operator=(PdfDocHolder&& other) noexcept;

    explicit operator bool() const noexcept { return doc != nullptr; }
    [[nodiscard]] FPDF_DOCUMENT get() const noexcept { return doc; }
};
#endif

/**
 * @brief High-performance static utility bridge interfacing with PDFium.
 */
class PdfRenderer {
public:
    using PdfDocSummary = Folio::PdfDocSummary;

    static void InitializeLibrary();
    static void DestroyLibrary();

#if defined(FOLIO_HAS_PDFIUM)
    /**
     * @brief Cross-platform, Unicode-aware loader to safely open a PDF document with PDFium.
     * @param filePath Full UTF-8 encoded path to the PDF document.
     * @return PdfDocHolder RAII container wrapping the opened FPDF_DOCUMENT and any custom file handle.
     */
    static PdfDocHolder OpenDocument(const std::string& filePath);
#endif

    /**
     * @brief Accurately queries the total number of pages in a PDF document using PDFium.
     */
    static int GetPageCount(const std::string& filePath);

    /**
     * @brief Retrieves physical dimensions of a given page in millimeters.
     */
    static bool GetPageDimensions(const std::string& filePath, int pageIndex, double& outWidthMm, double& outHeightMm);

    /**
     * @brief Renders a vector PDF page into a Blend2D raster image at the requested DPI.
     */
    static PdfPageRenderResult RenderPage(const std::string& filePath, int pageIndex, double targetDpi = 150.0, bool invertColors = false);

    /**
     * @brief Extracts selectable text information from a page into a PdfTextLayer.
     */
    static bool LoadTextLayer(const std::string& filePath, int pageIndex, PdfTextLayer& outTextLayer);

    /**
     * @brief Extracts document table-of-contents outline items.
     */
    static std::vector<PdfOutlineItem> LoadOutline(const std::string& filePath);

    /**
     * @brief Performs a fast, single-pass inspection of a PDF document:
     * Opens FPDF_DOCUMENT once, queries all page dimensions and outline, and reports progress.
     */
    static bool InspectAndLoadDocStructure(
        const std::string& filePath,
        PdfDocSummary& outSummary,
        std::function<void(int current, int total)> progressCallback = nullptr
    );
};

} // namespace Folio
