#pragma once
/**
 * =========================================================================================
 * @file pdf_vector_exporter.hpp
 * @brief Standalone High-Fidelity Vector PDF Exporter with Searchable Text and Link Annotations
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Generates standards-compliant, standalone PDF 1.4 documents directly without external dependencies:
 * 1. Discrete Standard Sheet Pages: Converts SheetTiles into physical PDF pages (A4, Letter, etc.).
 * 2. Resolution-Independent Vector Strokes: Emits native PDF vector path operators (`m`, `l`, `w`, `RG`, `S`).
 * 3. Searchable Text Streams: Embeds real text objects (`BT ... /F1 ... Tf (...) Tj ET`) so words
 *    are 100% searchable (`Ctrl+F`), selectable, and indexable in Acrobat and search engines.
 * 4. Interactive Link Annotations: Emits clickable `/Subtype /Link` URI annotations for both
 *    web links (`https://...`) and desktop deep links (`folionote://page/...`).
 */

#include <string>
#include <vector>
#include <memory>
#include "core/export/export_manager.hpp"

class Notebook;
class Section;
class CanvasPage;

namespace Folio {

class PdfVectorExporter {
public:
    /**
     * @brief Exports a single CanvasPage into a vector PDF file with sheet tiling.
     */
    static bool ExportPage(
        const std::shared_ptr<CanvasPage>& page,
        const std::string& destinationPdfPath,
        const ExportOptions& options
    );

    /**
     * @brief Exports an entire Section into a consolidated multi-sheet vector PDF file.
     */
    static bool ExportSection(
        const std::shared_ptr<Section>& section,
        const std::string& destinationPdfPath,
        const ExportOptions& options
    );

    /**
     * @brief Exports an entire Notebook into a comprehensive multi-sheet vector PDF file.
     */
    static bool ExportNotebook(
        const std::shared_ptr<Notebook>& notebook,
        const std::string& destinationPdfPath,
        const ExportOptions& options
    );
};

} // namespace Folio
