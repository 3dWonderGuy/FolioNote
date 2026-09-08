#pragma once
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <ctime>
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/ink_container.hpp"
#include "core/objects/text_box.hpp"
#include "core/objects/image_object.hpp"

using TextBox = Folio::TextBoxObject;

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

enum class ExportScope {
    CurrentPage,
    CurrentSection,
    EntireNotebook
};

enum class ExportFormat {
    PDF_Print,
    HTML_Document,
    FolioPackage,
    MarkdownText
};

class ExportManager {
public:
    // Exports either the page, section, or notebook according to scope and format
    static bool Export(
        ExportScope scope,
        ExportFormat format,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page,
        const std::string& destinationPath,
        bool triggerPrintDialog = false
    ) {
        if (!notebook) return false;

        std::filesystem::path outPath = destinationPath;
        if (outPath.empty()) {
            std::error_code ec;
            std::filesystem::create_directories("exports", ec);
            std::string defaultName = GetExportFilename(scope, format, notebook, section, page);
            outPath = std::filesystem::path("exports") / defaultName;
        }

        switch (format) {
            case ExportFormat::PDF_Print:
            case ExportFormat::HTML_Document: {
                std::string htmlContent = GenerateHTML(scope, notebook, section, page);
                std::ofstream file(outPath.string());
                if (!file.is_open()) return false;
                file << htmlContent;
                file.close();

                if (format == ExportFormat::PDF_Print && triggerPrintDialog) {
#if defined(_WIN32)
                    // Launch system print or browser view for instant PDF saving/printing
                    ShellExecuteA(NULL, "print", outPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
                }
                return true;
            }
            case ExportFormat::MarkdownText: {
                std::string mdContent = GenerateMarkdown(scope, notebook, section, page);
                std::ofstream file(outPath.string());
                if (!file.is_open()) return false;
                file << mdContent;
                file.close();
                return true;
            }
            case ExportFormat::FolioPackage: {
                return ExportFolioPackage(scope, notebook, section, outPath.string());
            }
            default:
                return false;
        }
    }

    static std::string GetExportFilename(
        ExportScope scope,
        ExportFormat format,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page
    ) {
        std::string base = "FolioNote_Export";
        if (scope == ExportScope::CurrentPage && page) {
            base = SanitizeFilename(page->title.empty() ? "Untitled_Page" : page->title);
        } else if (scope == ExportScope::CurrentSection && section) {
            base = SanitizeFilename(section->name.empty() ? "Untitled_Section" : section->name);
        } else if (notebook) {
            base = SanitizeFilename(notebook->name.empty() ? "Untitled_Notebook" : notebook->name);
        }

        switch (format) {
            case ExportFormat::PDF_Print:     return base + "_Print.html";
            case ExportFormat::HTML_Document: return base + ".html";
            case ExportFormat::MarkdownText:  return base + ".md";
            case ExportFormat::FolioPackage:  return base + ".folio";
            default:                          return base + ".txt";
        }
    }

private:
    static std::string SanitizeFilename(const std::string& name) {
        std::string result;
        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ') {
                result += (c == ' ' ? '_' : c);
            }
        }
        return result.empty() ? "export" : result;
    }

    static std::string GeneratePageHTML(const std::shared_ptr<CanvasPage>& page, const std::string& secName, const std::string& nbName) {
        if (!page) return "";
        std::ostringstream ss;
        ss << "<div class=\"folio-page\">\n";
        ss << "  <header class=\"page-header\">\n";
        ss << "    <div class=\"breadcrumb\">" << nbName << " &rsaquo; " << secName << "</div>\n";
        ss << "    <h1 class=\"page-title\">" << (page->title.empty() ? "Untitled Page" : page->title) << "</h1>\n";
        
        // Date & Time
        std::string dt = page->createdDateStr + " | " + page->createdTimeStr;
        if (page->createdDateStr.empty()) dt = "Created with FolioNote";
        ss << "    <div class=\"page-meta\">" << dt << "</div>\n";
        ss << "  </header>\n";

        ss << "  <div class=\"page-canvas-area\">\n";

        // Render TextBoxes
        for (const auto& obj : page->objects) {
            if (auto tb = std::dynamic_pointer_cast<TextBox>(obj)) {
                ss << "    <div class=\"canvas-textbox\" style=\"position: relative; margin: 16px 0; max-width: 800px;\">\n";
                ss << "      <p style=\"font-size: 16px; line-height: 1.6; color: #202124; white-space: pre-wrap;\">" 
                   << tb->text << "</p>\n";
                ss << "    </div>\n";
            }
        }

        // Render vector strokes as high-resolution SVG
        std::ostringstream svgStrokes;
        int strokeCount = 0;
        for (const auto& obj : page->objects) {
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

                    float w = std::max(1.0f, static_cast<float>(stroke.baseWidth));
                    svgStrokes << "      <polyline fill=\"none\" stroke=\"rgba(" << (int)r << "," << (int)g << "," << (int)b << "," << alpha << ")\" "
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
            ss << "    <div class=\"canvas-drawing-container\" style=\"position: relative; width: 100%; min-height: 400px; margin-top: 20px;\">\n";
            ss << "      <svg width=\"100%\" height=\"800\" viewBox=\"0 0 1200 800\" style=\"background: transparent; overflow: visible;\">\n";
            ss << svgStrokes.str();
            ss << "      </svg>\n";
            ss << "    </div>\n";
        }

        ss << "  </div>\n";
        ss << "</div>\n";
        return ss.str();
    }

    static std::string GenerateHTML(
        ExportScope scope,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page
    ) {
        std::ostringstream ss;
        ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
        ss << "  <meta charset=\"UTF-8\">\n";
        ss << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
        ss << "  <title>" << (notebook ? notebook->name : "FolioNote Export") << "</title>\n";
        ss << "  <style>\n";
        ss << "    * { box-sizing: border-box; margin: 0; padding: 0; }\n";
        ss << "    body { font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif; background: #f8f9fa; color: #1a1a1a; padding: 30px; }\n";
        ss << "    .folio-page { background: #ffffff; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.06); max-width: 960px; margin: 0 auto 30px auto; padding: 48px; position: relative; }\n";
        ss << "    .page-header { border-bottom: 1px solid #e5e7eb; padding-bottom: 20px; margin-bottom: 24px; }\n";
        ss << "    .breadcrumb { font-size: 13px; font-weight: 600; color: #6b7280; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; }\n";
        ss << "    .page-title { font-size: 28px; font-weight: 700; color: #111827; margin-bottom: 8px; }\n";
        ss << "    .page-meta { font-size: 13px; color: #9ca3af; }\n";
        ss << "    .page-canvas-area { min-height: 400px; }\n";
        ss << "    @media print {\n";
        ss << "      body { background: #ffffff; padding: 0; }\n";
        ss << "      .folio-page { box-shadow: none; border-radius: 0; padding: 20mm; margin: 0; max-width: 100%; page-break-after: always; break-after: page; }\n";
        ss << "      @page { size: A4 portrait; margin: 0; }\n";
        ss << "    }\n";
        ss << "  </style>\n";
        ss << "</head>\n<body>\n";

        std::string nbName = notebook ? notebook->name : "Notebook";

        if (scope == ExportScope::CurrentPage && page) {
            std::string secName = section ? section->name : "Section";
            ss << GeneratePageHTML(page, secName, nbName);
        } else if (scope == ExportScope::CurrentSection && section) {
            for (const auto& pg : section->pages) {
                if (pg) {
                    ss << GeneratePageHTML(pg, section->name, nbName);
                }
            }
        } else if (notebook) {
            for (const auto& sec : notebook->sections) {
                if (!sec) continue;
                for (const auto& pg : sec->pages) {
                    if (pg) {
                        ss << GeneratePageHTML(pg, sec->name, nbName);
                    }
                }
            }
            for (const auto& grp : notebook->sectionGroups) {
                if (!grp) continue;
                for (const auto& sec : grp->sections) {
                    if (!sec) continue;
                    for (const auto& pg : sec->pages) {
                        if (pg) {
                            ss << GeneratePageHTML(pg, grp->name + " / " + sec->name, nbName);
                        }
                    }
                }
            }
        }

        ss << "</body>\n</html>\n";
        return ss.str();
    }

    static std::string GenerateMarkdown(
        ExportScope scope,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::shared_ptr<CanvasPage>& page
    ) {
        std::ostringstream ss;
        if (scope == ExportScope::CurrentPage && page) {
            ss << "# " << page->title << "\n\n";
            ss << "*" << (page->createdDateStr + " | " + page->createdTimeStr) << "*\n\n";
            for (const auto& obj : page->objects) {
                if (auto tb = std::dynamic_pointer_cast<TextBox>(obj)) {
                    ss << tb->text << "\n\n";
                }
            }
        } else if (scope == ExportScope::CurrentSection && section) {
            ss << "# Section: " << section->name << "\n\n";
            for (const auto& pg : section->pages) {
                if (!pg) continue;
                ss << "## " << pg->title << "\n\n";
                ss << "*" << (pg->createdDateStr + " | " + pg->createdTimeStr) << "*\n\n";
                for (const auto& obj : pg->objects) {
                    if (auto tb = std::dynamic_pointer_cast<TextBox>(obj)) {
                        ss << tb->text << "\n\n";
                    }
                }
                ss << "---\n\n";
            }
        } else if (notebook) {
            ss << "# Notebook: " << notebook->name << "\n\n";
            for (const auto& sec : notebook->sections) {
                if (!sec) continue;
                ss << "## Section: " << sec->name << "\n\n";
                for (const auto& pg : sec->pages) {
                    if (!pg) continue;
                    ss << "### " << pg->title << "\n\n";
                    for (const auto& obj : pg->objects) {
                        if (auto tb = std::dynamic_pointer_cast<TextBox>(obj)) {
                            ss << tb->text << "\n\n";
                        }
                    }
                }
            }
        }
        return ss.str();
    }

    static bool ExportFolioPackage(
        ExportScope scope,
        const std::shared_ptr<Notebook>& notebook,
        const std::shared_ptr<Section>& section,
        const std::string& destinationPath
    ) {
        std::error_code ec;
        std::filesystem::path dst(destinationPath);
        if (!notebook->filePath.empty() && std::filesystem::exists(notebook->filePath, ec)) {
            // Copy directory package
            std::filesystem::copy(notebook->filePath, dst, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            return !ec;
        }
        return false;
    }
};
