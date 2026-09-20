#pragma once
/**
 * =========================================================================================
 * @file markdown_exporter.hpp
 * @brief Portable Markdown Exporter with Embedded Images and Vector Drawings
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Generates portable Markdown files (.md) accompanied by an `assets/` subfolder:
 * 1. Text & Structure: Preserves headers, breadcrumbs, timestamps, and body paragraphs.
 * 2. Vector Ink Drawing Assets: Extracts hand-drawn strokes into standalone SVG assets
 *    `assets/<page_guid>_ink.svg` and references them via `![Handwritten Notes](assets/...)`.
 * 3. Raster Images: Extracts embedded images to `assets/` and references them via Markdown tags.
 * 4. Hyperlinks: Preserves web URLs and `folionote://` desktop app deep links.
 */

#include <string>
#include <vector>
#include <memory>
#include "core/export/export_manager.hpp"

class Notebook;
class Section;
class CanvasPage;

namespace Folio {

class MarkdownExporter {
public:
    /**
     * @brief Exports a single CanvasPage into Markdown content and associated assets.
     * @param page CanvasPage to export.
     * @param sectionName Containing section label.
     * @param notebookName Containing notebook label.
     * @param outputDirectory Target directory where the .md and assets/ will reside.
     * @param options Configurable export parameters.
     * @return Generated Markdown document string.
     */
    static std::string ExportPage(
        const std::shared_ptr<CanvasPage>& page,
        const std::string& sectionName,
        const std::string& notebookName,
        const std::string& outputDirectory,
        const ExportOptions& options
    );

    /**
     * @brief Exports an entire Section into a consolidated Markdown document.
     */
    static std::string ExportSection(
        const std::shared_ptr<Section>& section,
        const std::string& notebookName,
        const std::string& outputDirectory,
        const ExportOptions& options
    );

    /**
     * @brief Exports an entire Notebook into a hierarchical Markdown document.
     */
    static std::string ExportNotebook(
        const std::shared_ptr<Notebook>& notebook,
        const std::string& outputDirectory,
        const ExportOptions& options
    );

    /**
     * @brief Generates an external SVG vector file for the ink strokes on a page.
     * @param page CanvasPage containing strokes.
     * @param assetsDir Directory where the SVG will be saved.
     * @return Relative path to the generated SVG (e.g. "assets/<pageGuid>_drawing.svg"), or empty.
     */
    static std::string ExportInkToSvg(const CanvasPage& page, const std::string& assetsDir);

    /**
     * @brief Formats raw text into Markdown with link tags.
     */
    static std::string FormatMarkdownText(const std::string& rawText, const ExportOptions& options);
};

} // namespace Folio
