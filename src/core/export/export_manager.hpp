#pragma once
/**
 * =========================================================================================
 * @file export_manager.hpp
 * @brief Top-Level Coordinator for Document Export, Vector Graphics, and Archival Packaging
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * ExportManager provides high-level dispatching for all FolioNote export formats:
 * 1. Sheet-Tiled Vector HTML & SVG: Print-ready discrete standard pages (A4 / Letter).
 * 2. Vector PDF: Multi-page vector PDF documents with selectable text and link annotations.
 * 3. Markdown with Assets: Portable markdown with accompanying image and vector assets.
 * 4. High-Ratio 7-Zip Packages: Complete, compressed archival packages (.folionb, .foliolib).
 * 5. Automatic Aspect-Ratio Width Fit: Dynamically fits wide canvas drawings to standard paper.
 * 6. Interactive Hyperlinks: Preserves web URLs and `folionote://` desktop deep links.
 */

#include <string>
#include <vector>
#include <memory>
#include <future>
#include "core/export/sheet_tiler.hpp"

class Notebook;
class Section;
class CanvasPage;

namespace Folio {

/**
 * @enum ExportScope
 * @brief Hierarchy level targeted for export.
 */
enum class ExportScope {
    CurrentPage,    ///< Active CanvasPage
    CurrentSection, ///< All pages in the active Section
    EntireNotebook, ///< All sections and section groups in the Notebook
    EntireLibrary   ///< All notebooks within the Library bundle
};

/**
 * @enum ExportFormat
 * @brief Target file format or package bundle.
 */
enum class ExportFormat {
    PDF_Print,      ///< HTML document dispatched to system print-to-PDF dialog
    HTML_Document,  ///< Responsive, print-styled sheet-tiled HTML/SVG document
    FolioPackage,   ///< 7-Zip compressed standalone notebook package (.folionb / .fnpack)
    MarkdownText,   ///< Markdown text document with extracted images and SVG drawings
    PDF_Vector,     ///< Direct standalone vector PDF binary with searchable text
    LibraryPackage  ///< 7-Zip compressed multi-notebook library bundle (.foliolib)
};

/**
 * @struct ExportOptions
 * @brief User-configurable parameters governing export generation and fidelity.
 */
struct ExportOptions {
    SheetTraversalOrder traversalOrder = SheetTraversalOrder::ColumnMajor_TopBottom_LeftRight;
    PageScalingMode scalingMode = PageScalingMode::OriginalScale;
    bool enableLinks = true;         ///< Master toggle: enable hyperlinks across exported document
    bool enableAppDeepLinks = true;  ///< Allow folionote:// external protocol links to open desktop app
    bool enableWebLinks = true;      ///< Allow https:// and mailto: external web links
    bool skipEmptySheets = true;     ///< Omit 2D grid tiles that contain zero drawn content
    bool triggerPrintDialog = false; ///< Automatically invoke OS print workflow (PDF_Print)
    int compressionLevel = 7;        ///< 7-Zip compression level (1 = fastest, 9 = ultra)
    std::string libraryPath = "";    ///< Source directory for EntireLibrary export scope

    ExportOptions() = default;
    ExportOptions(bool triggerPrint) : triggerPrintDialog(triggerPrint) {}
};

/**
 * @class ExportManager
 * @brief High-level facade for executing synchronous and asynchronous document exports.
 */
class ExportManager {
public:
    /**
     * @brief Exports document content according to specified scope, format, and options.
     */
    static bool Export(
        ExportScope scope,
        ExportFormat format,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page,
        const std::string& destinationPath = "",
        const ExportOptions& options = ExportOptions()
    );

    /**
     * @brief Backward-compatible overload accepting a boolean triggerPrintDialog flag.
     */
    static bool Export(
        ExportScope scope,
        ExportFormat format,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page,
        const std::string& destinationPath,
        bool triggerPrintDialog
    ) {
        ExportOptions opts(triggerPrintDialog);
        return Export(scope, format, notebook, section, page, destinationPath, opts);
    }

    /**
     * @brief Asynchronously dispatches export execution on the background ThreadPool.
     */
    static std::future<bool> ExportAsync(
        ExportScope scope,
        ExportFormat format,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page,
        const std::string& destinationPath = "",
        const ExportOptions& options = ExportOptions()
    );

    /**
     * @brief Computes a standardized, filesystem-safe filename for the export output.
     */
    static std::string GetExportFilename(
        ExportScope scope,
        ExportFormat format,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page,
        const std::string& libraryPath = ""
    );

    /**
     * @brief Sanitizes a user-supplied string into a clean, filesystem-safe basename.
     */
    static std::string SanitizeFilename(const std::string& name);
};

} // namespace Folio

using Folio::ExportScope;
using Folio::ExportFormat;
using Folio::ExportOptions;
using Folio::ExportManager;
using Folio::SheetTraversalOrder;
using Folio::PageScalingMode;
