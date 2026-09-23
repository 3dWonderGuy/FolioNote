/**
 * =========================================================================================
 * @file markdown_exporter.cpp
 * @brief Implementation of Portable Markdown Exporter with Embedded Images and Vector Drawings
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Hybrid Text & Vector Representation:
 *    Text boxes are converted directly to standard Markdown text blocks, while vector ink strokes
 *    are exported into standalone SVG assets stored inside an `assets/` subfolder.
 * 2. Cross-Linking & Interactive Hyperlinks:
 *    Automatically detects web URLs (`https://...`) and desktop deep links (`folionote://page/...`)
 *    and converts them to clickable Markdown references: `[Text](url)`.
 * 3. Portable Hierarchical Structure:
 *    Maintains clean Heading levels (`#`, `##`, `###`) across Notebooks, Sections, and Pages.
 */

#include "core/export/markdown_exporter.hpp"
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/ink_container.hpp"
#include "core/objects/text_box.hpp"
#include "core/objects/image_container.hpp"
#include "io/file_manager.hpp"
#include <sstream>
#include <fstream>
#include <regex>
#include <filesystem>

namespace Folio {

std::string MarkdownExporter::FormatMarkdownText(const std::string& rawText, const ExportOptions& options) {
    if (!options.enableLinks) {
        return rawText;
    }

    // Convert URLs into Markdown links: [url](url)
    static const std::regex urlRegex(R"((https?://[^\s<>"]+|folionote://[^\s<>"]+))");
    std::string result;
    auto wordsBegin = std::sregex_iterator(rawText.begin(), rawText.end(), urlRegex);
    auto wordsEnd = std::sregex_iterator();

    size_t lastPos = 0;
    for (auto it = wordsBegin; it != wordsEnd; ++it) {
        std::smatch match = *it;
        result += rawText.substr(lastPos, match.position() - lastPos);

        std::string url = match.str();
        if (url.rfind("folionote://", 0) == 0) {
            if (options.enableAppDeepLinks) {
                result += "[Open in FolioNote](" + url + ")";
            } else {
                result += url;
            }
        } else if (options.enableWebLinks) {
            result += "[" + url + "](" + url + ")";
        } else {
            result += url;
        }

        lastPos = match.position() + match.length();
    }
    result += rawText.substr(lastPos);
    return result;
}

std::string MarkdownExporter::ExportInkToSvg(const CanvasPage& page, const std::string& assetsDir) {
    if (page.objects.empty()) return "";

    std::ostringstream svgStrokes;
    int strokeCount = 0;
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;

    for (const auto& obj : page.objects) {
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
                float w = std::max(0.5f, static_cast<float>(stroke.baseWidth));

                svgStrokes << "  <polyline fill=\"none\" stroke=\"rgba(" << (int)r << "," << (int)g << "," << (int)b << "," << alpha << ")\" "
                           << "stroke-width=\"" << w << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
                svgStrokes << stroke.segments[0].p0.x << "," << stroke.segments[0].p0.y << " ";

                minX = std::min(minX, stroke.segments[0].p0.x);
                minY = std::min(minY, stroke.segments[0].p0.y);
                maxX = std::max(maxX, stroke.segments[0].p0.x);
                maxY = std::max(maxY, stroke.segments[0].p0.y);

                for (const auto& seg : stroke.segments) {
                    svgStrokes << seg.p1.x << "," << seg.p1.y << " ";
                    minX = std::min(minX, seg.p1.x);
                    minY = std::min(minY, seg.p1.y);
                    maxX = std::max(maxX, seg.p1.x);
                    maxY = std::max(maxY, seg.p1.y);
                }
                svgStrokes << "\" />\n";
            }
        }
    }

    if (strokeCount == 0) return "";

    double padding = 20.0;
    minX -= padding;
    minY -= padding;
    double width = std::max(100.0, (maxX - minX) + padding * 2.0);
    double height = std::max(100.0, (maxY - minY) + padding * 2.0);

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
        << minX << " " << minY << " " << width << " " << height
        << "\" width=\"100%\" height=\"auto\">\n";
    svg << svgStrokes.str();
    svg << "</svg>\n";

    FileManager::CreateDirectories(assetsDir);
    std::string svgFilename = page.guid + "_drawing.svg";
    std::string svgPath = FileManager::JoinPath(assetsDir, svgFilename);

    FileManager::WriteTextAtomic(svgPath, svg.str());
    return "assets/" + svgFilename;
}

std::string MarkdownExporter::ExportPage(
    const std::shared_ptr<CanvasPage>& page,
    const std::string& sectionName,
    const std::string& notebookName,
    const std::string& outputDirectory,
    const ExportOptions& options
) {
    if (!page) return "";
    std::ostringstream ss;

    ss << "# " << page->title << "\n\n";
    ss << "> **Location:** " << notebookName << " &rsaquo; " << sectionName << "  \n";
    if (!page->createdDateStr.empty()) {
        ss << "> **Created:** " << page->createdDateStr << " " << page->createdTimeStr << "\n\n";
    }

    // Export hand-drawn vector strokes as SVG asset
    std::string assetsDir = FileManager::JoinPath(outputDirectory, "assets");
    std::string svgRelPath = ExportInkToSvg(*page, assetsDir);
    if (!svgRelPath.empty()) {
        ss << "![Handwritten Ink Notes](" << svgRelPath << ")\n\n";
    }

    // Text box contents
    for (const auto& obj : page->objects) {
        if (auto tb = std::dynamic_pointer_cast<Folio::TextBoxObject>(obj)) {
            ss << FormatMarkdownText(tb->text, options) << "\n\n";
        }
    }

    return ss.str();
}

std::string MarkdownExporter::ExportSection(
    const std::shared_ptr<Section>& section,
    const std::string& notebookName,
    const std::string& outputDirectory,
    const ExportOptions& options
) {
    if (!section) return "";
    std::ostringstream ss;

    ss << "# " << section->name << "\n\n";
    ss << "> **Notebook:** " << notebookName << "\n\n";
    ss << "---\n\n";

    for (const auto& pg : section->pages) {
        if (pg) {
            ss << ExportPage(pg, section->name, notebookName, outputDirectory, options);
            ss << "\n---\n\n";
        }
    }

    return ss.str();
}

std::string MarkdownExporter::ExportNotebook(
    const std::shared_ptr<Notebook>& notebook,
    const std::string& outputDirectory,
    const ExportOptions& options
) {
    if (!notebook) return "";
    std::ostringstream ss;

    ss << "# " << notebook->name << "\n\n";

    for (const auto& sec : notebook->sections) {
        if (sec) {
            ss << "## " << sec->name << "\n\n";
            for (const auto& pg : sec->pages) {
                if (pg) {
                    ss << ExportPage(pg, sec->name, notebook->name, outputDirectory, options);
                    ss << "\n---\n\n";
                }
            }
        }
    }

    return ss.str();
}

} // namespace Folio
