/**
 * =========================================================================================
 * @file html_svg_exporter.cpp
 * @brief Implementation of High-Fidelity Sheet-Tiled HTML & SVG Vector Exporter
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Sheet-Tiled Page Isolation:
 *    Each SheetTile is emitted as a discrete CSS-styled sheet (`.folio-sheet`) with physical
 *    millimeter dimensions matching standard paper (e.g. A4: 210 x 297 mm, Letter: 215.9 x 279.4 mm).
 *    In `@media print`, each sheet has `page-break-after: always;` ensuring crisp, 1:1 printing.
 * 2. High-Resolution SVG Vectors:
 *    Renders vector ink strokes into inline SVG viewports with rounded line caps/joins and
 *    alpha transparency.
 * 3. Searchable Text & Typography:
 *    TextBoxObjects are rendered as HTML text nodes positioned with millimeter offsets,
 *    preserving full selectability, clipboard copy, and search engine indexability.
 * 4. Interactive Hyperlinks:
 *    Supports standard web URLs (`https://...`), in-document sheet anchors (`#sheet_...`),
 *    and external application deep links (`folionote://page/...`).
 */

#include "core/export/html_svg_exporter.hpp"
#include "core/export/export_manager.hpp"
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/ink_container.hpp"
#include "core/objects/text_box.hpp"
#include "core/objects/image_container.hpp"
#include <sstream>
#include <iomanip>
#include <regex>

namespace Folio {

namespace {

std::string EscapeHtml(const std::string& input) {
    std::string output;
    for (char c : input) {
        switch (c) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output += c; break;
        }
    }
    return output;
}

std::string GetHtmlHeader(const std::string& title, double sheetWidthMm, double sheetHeightMm) {
    std::ostringstream ss;
    ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    ss << "  <meta charset=\"UTF-8\">\n";
    ss << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    ss << "  <title>" << EscapeHtml(title) << "</title>\n";
    ss << "  <style>\n";
    ss << "    * { box-sizing: border-box; margin: 0; padding: 0; }\n";
    ss << "    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: #525659; color: #1a1a1a; padding: 24px; }\n";
    ss << "    .folio-sheet { background: #ffffff; width: " << sheetWidthMm << "mm; height: " << sheetHeightMm << "mm; margin: 0 auto 24px auto; position: relative; overflow: hidden; box-shadow: 0 4px 16px rgba(0,0,0,0.25); border-radius: 2px; }\n";
    ss << "    .sheet-header { position: absolute; top: 12mm; left: 16mm; right: 16mm; border-bottom: 1px solid #e5e7eb; padding-bottom: 8px; pointer-events: none; z-index: 10; }\n";
    ss << "    .sheet-breadcrumb { font-size: 9pt; font-weight: 600; color: #6b7280; text-transform: uppercase; letter-spacing: 0.5px; }\n";
    ss << "    .sheet-title { font-size: 16pt; font-weight: 700; color: #111827; margin-top: 2px; }\n";
    ss << "    .sheet-meta { font-size: 8pt; color: #9ca3af; margin-top: 2px; }\n";
    ss << "    .sheet-svg-layer { position: absolute; top: 0; left: 0; width: 100%; height: 100%; pointer-events: none; }\n";
    ss << "    .sheet-text-layer { position: absolute; top: 0; left: 0; width: 100%; height: 100%; }\n";
    ss << "    .canvas-textbox { position: absolute; pointer-events: auto; white-space: pre-wrap; word-wrap: break-word; }\n";
    ss << "    .canvas-textbox a { color: #1a73e8; text-decoration: underline; }\n";
    ss << "    .canvas-textbox a.folio-app-link { color: #7c3aed; font-weight: 600; }\n";
    ss << "    @media print {\n";
    ss << "      body { background: #ffffff; padding: 0; }\n";
    ss << "      .folio-sheet { box-shadow: none; border-radius: 0; margin: 0; page-break-after: always; break-after: page; width: 100%; height: 100%; }\n";
    ss << "      @page { size: " << sheetWidthMm << "mm " << sheetHeightMm << "mm; margin: 0; }\n";
    ss << "    }\n";
    ss << "  </style>\n";
    ss << "</head>\n<body>\n";
    return ss.str();
}

} // anonymous namespace

std::string HtmlSvgExporter::FormatHtmlText(const std::string& rawText, const ExportOptions& options) {
    if (!options.enableLinks) {
        return EscapeHtml(rawText);
    }

    std::string escaped = EscapeHtml(rawText);

    // Hyperlink regex for https://, http://, and folionote://
    static const std::regex urlRegex(R"((https?://[^\s<>"]+|folionote://[^\s<>"]+))");
    std::string result;
    auto wordsBegin = std::sregex_iterator(escaped.begin(), escaped.end(), urlRegex);
    auto wordsEnd = std::sregex_iterator();

    size_t lastPos = 0;
    for (auto it = wordsBegin; it != wordsEnd; ++it) {
        std::smatch match = *it;
        result += escaped.substr(lastPos, match.position() - lastPos);

        std::string url = match.str();
        if (url.rfind("folionote://", 0) == 0) {
            if (options.enableAppDeepLinks) {
                result += "<a href=\"" + url + "\" class=\"folio-app-link\" title=\"Open in FolioNote\">" + url + "</a>";
            } else {
                result += url;
            }
        } else if (options.enableWebLinks) {
            result += "<a href=\"" + url + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + url + "</a>";
        } else {
            result += url;
        }

        lastPos = match.position() + match.length();
    }
    result += escaped.substr(lastPos);
    return result;
}

std::string HtmlSvgExporter::RenderSheetBlock(
    const SheetTile& tile,
    const std::shared_ptr<CanvasPage>& page,
    const ExportOptions& options
) {
    if (!page) return "";
    std::ostringstream ss;

    ss << "<div class=\"folio-sheet\" id=\"" << tile.sheetAnchorId << "\">\n";

    // Header on the first sheet tile of a page
    if (tile.sequenceIndex == 0) {
        ss << "  <header class=\"sheet-header\">\n";
        ss << "    <div class=\"sheet-title\">" << EscapeHtml(page->title) << "</div>\n";
        if (!page->createdDateStr.empty()) {
            ss << "    <div class=\"sheet-meta\">" << EscapeHtml(page->createdDateStr + " | " + page->createdTimeStr) << "</div>\n";
        }
        ss << "  </header>\n";
    }

    double virtualW = tile.widthMm / tile.uniformScale;
    double virtualH = tile.heightMm / tile.uniformScale;

    // SVG Vector Ink Layer
    std::ostringstream svgStrokes;
    int strokeCount = 0;

    for (const auto& obj : tile.intersectingObjects) {
        if (auto ink = std::dynamic_pointer_cast<InkContainer>(obj)) {
            for (const auto& stroke : ink->strokes) {
                if (stroke.segments.empty()) continue;
                strokeCount++;

                uint32_t val = stroke.color.value;
                uint8_t a = (val >> 24) & 0xFF;
                uint8_t r = (val >> 16) & 0xFF;
                uint8_t g = (val >> 8) & 0xFF;
                uint8_t b = val & 0xFF;
                float alpha = a / 255.0f;
                float w = std::max(0.5f, static_cast<float>(stroke.baseWidth * tile.uniformScale));

                svgStrokes << "    <polyline fill=\"none\" stroke=\"rgba(" << (int)r << "," << (int)g << "," << (int)b << "," << alpha << ")\" "
                           << "stroke-width=\"" << w << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
                svgStrokes << stroke.segments[0].p0.x << "," << stroke.segments[0].p0.y << " ";
                for (const auto& seg : stroke.segments) {
                    svgStrokes << seg.p1.x << "," << seg.p1.y << " ";
                }
                svgStrokes << "\" />\n";
            }
        }
    }

    if (strokeCount > 0) {
        ss << "  <svg class=\"sheet-svg-layer\" viewBox=\""
           << tile.worldX << " " << tile.worldY << " " << virtualW << " " << virtualH
           << "\" preserveAspectRatio=\"none\">\n";
        ss << svgStrokes.str();
        ss << "  </svg>\n";
    }

    // Searchable HTML Text Layer
    ss << "  <div class=\"sheet-text-layer\">\n";
    for (const auto& obj : tile.intersectingObjects) {
        if (auto tb = std::dynamic_pointer_cast<Folio::TextBoxObject>(obj)) {
            // Transform world coordinates relative to this sheet tile
            double relX = (tb->worldX - tile.worldX) * tile.uniformScale;
            double relY = (tb->worldY - tile.worldY) * tile.uniformScale;
            double widthMm = (tb->worldWidth > 0.0 ? tb->worldWidth : 100.0) * tile.uniformScale;
            double fontSizePt = (tb->fontSize > 0.0f ? tb->fontSize : 12.0f) * static_cast<float>(tile.uniformScale);

            ss << "    <div class=\"canvas-textbox\" style=\"left: " << relX << "mm; top: " << relY
               << "mm; width: " << widthMm << "mm; font-size: " << fontSizePt << "pt; font-family: '"
               << EscapeHtml(tb->fontFamily.empty() ? "Segoe UI" : tb->fontFamily) << "'; color: #1f2937;\">\n";
            ss << "      <p>" << FormatHtmlText(tb->text, options) << "</p>\n";
            ss << "    </div>\n";
        }
    }
    ss << "  </div>\n";

    ss << "</div>\n";
    return ss.str();
}

std::string HtmlSvgExporter::ExportPage(
    const std::shared_ptr<CanvasPage>& page,
    const std::string& sectionName,
    const std::string& notebookName,
    const ExportOptions& options
) {
    if (!page) return "";
    auto tiles = SheetTiler::TilePage(
        *page, options.traversalOrder, options.scalingMode, options.skipEmptySheets
    );

    double sheetW = tiles.empty() ? 210.0 : tiles.front().widthMm;
    double sheetH = tiles.empty() ? 297.0 : tiles.front().heightMm;

    std::ostringstream ss;
    ss << GetHtmlHeader(page->title, sheetW, sheetH);
    for (const auto& tile : tiles) {
        ss << RenderSheetBlock(tile, page, options);
    }
    ss << "</body>\n</html>\n";
    return ss.str();
}

std::string HtmlSvgExporter::ExportSection(
    const std::shared_ptr<Section>& section,
    const std::string& notebookName,
    const ExportOptions& options
) {
    if (!section) return "";
    double sheetW = 210.0, sheetH = 297.0;

    std::ostringstream body;
    for (const auto& pg : section->pages) {
        if (!pg) continue;
        auto tiles = SheetTiler::TilePage(
            *pg, options.traversalOrder, options.scalingMode, options.skipEmptySheets
        );
        if (!tiles.empty()) {
            sheetW = tiles.front().widthMm;
            sheetH = tiles.front().heightMm;
        }
        for (const auto& tile : tiles) {
            body << RenderSheetBlock(tile, pg, options);
        }
    }

    std::ostringstream ss;
    ss << GetHtmlHeader(section->name, sheetW, sheetH);
    ss << body.str();
    ss << "</body>\n</html>\n";
    return ss.str();
}

std::string HtmlSvgExporter::ExportNotebook(
    const std::shared_ptr<Notebook>& notebook,
    const ExportOptions& options
) {
    if (!notebook) return "";
    double sheetW = 210.0, sheetH = 297.0;

    std::ostringstream body;
    for (const auto& sec : notebook->sections) {
        if (!sec) continue;
        for (const auto& pg : sec->pages) {
            if (!pg) continue;
            auto tiles = SheetTiler::TilePage(
                *pg, options.traversalOrder, options.scalingMode, options.skipEmptySheets
            );
            if (!tiles.empty()) {
                sheetW = tiles.front().widthMm;
                sheetH = tiles.front().heightMm;
            }
            for (const auto& tile : tiles) {
                body << RenderSheetBlock(tile, pg, options);
            }
        }
    }

    std::ostringstream ss;
    ss << GetHtmlHeader(notebook->name, sheetW, sheetH);
    ss << body.str();
    ss << "</body>\n</html>\n";
    return ss.str();
}

} // namespace Folio
