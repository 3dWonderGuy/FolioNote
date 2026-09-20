#pragma once
/**
 * =========================================================================================
 * @file html_svg_exporter.hpp
 * @brief High-Fidelity Sheet-Tiled Vector HTML & SVG Exporter
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Generates standalone, responsive, print-perfect HTML documents embedding:
 * 1. Sheet-tiled pages: Discrete standard sheets (A4, Letter) with exact CSS page sizes.
 * 2. Searchable Text: Positions text boxes with exact coordinates and typography.
 * 3. Vector SVG Ink: High-resolution vector paths with rounded caps and opacity.
 * 4. Interactive Hyperlinks: Web URLs, intra-document sheet jumps, and folionote:// deep links.
 */

#include <string>
#include <vector>
#include <memory>
#include "core/export/sheet_tiler.hpp"

class Notebook;
class Section;
class CanvasPage;

namespace Folio {

struct ExportOptions;

class HtmlSvgExporter {
public:
    /**
     * @brief Generates complete HTML document for a single page with sheet tiling.
     */
    static std::string ExportPage(const std::shared_ptr<CanvasPage>& page,
                                  const std::string& sectionName,
                                  const std::string& notebookName,
                                  const ExportOptions& options);

    /**
     * @brief Generates complete HTML document for an entire section.
     */
    static std::string ExportSection(const std::shared_ptr<Section>& section,
                                     const std::string& notebookName,
                                     const ExportOptions& options);

    /**
     * @brief Generates complete HTML document for an entire notebook.
     */
    static std::string ExportNotebook(const std::shared_ptr<Notebook>& notebook,
                                      const ExportOptions& options);

    /**
     * @brief Renders a single SheetTile into an HTML/SVG sheet block.
     */
    static std::string RenderSheetBlock(const SheetTile& tile,
                                        const std::shared_ptr<CanvasPage>& page,
                                        const ExportOptions& options);

    /**
     * @brief Formats raw text into HTML with hyperlinking support.
     */
    static std::string FormatHtmlText(const std::string& rawText, const ExportOptions& options);
};

} // namespace Folio
