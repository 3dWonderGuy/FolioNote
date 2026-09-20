/**
 * =========================================================================================
 * @file export_manager.cpp
 * @brief Implementation of High-Level Export Coordinator
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Scope & Format Dispatching:
 *    Routes Page, Section, Notebook, and Library scopes to domain-specific export engines:
 *    - HtmlSvgExporter (Sheet-Tiled HTML/SVG with Print CSS)
 *    - PdfVectorExporter (Vector PDF with Searchable Text and URI Annotations)
 *    - MarkdownExporter (Portable Markdown with extracted vector and image assets)
 *    - PackageExporter (High-Ratio 7-Zip Compressed Archives)
 * 2. Non-Blocking Background ThreadPool:
 *    Async exports execute off the main thread to ensure the 120 FPS UI never hitches.
 */

#include "core/export/export_manager.hpp"
#include "core/export/html_svg_exporter.hpp"
#include "core/export/markdown_exporter.hpp"
#include "core/export/pdf_vector_exporter.hpp"
#include "core/export/package_exporter.hpp"
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "utils/file_manager.hpp"
#include "utils/thread_pool.hpp"
#include "utils/logger.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace Folio {

std::string ExportManager::SanitizeFilename(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ') {
            result += (c == ' ' ? '_' : c);
        }
    }
    return result.empty() ? "export" : result;
}

std::string ExportManager::GetExportFilename(
    ExportScope scope,
    ExportFormat format,
    const std::shared_ptr<Notebook>& notebook,
    const std::shared_ptr<Section>& section,
    const std::shared_ptr<CanvasPage>& page,
    const std::string& libraryPath
) {
    std::string base = "FolioNote_Export";

    if (scope == ExportScope::CurrentPage && page) {
        base = SanitizeFilename(page->title.empty() ? "Untitled_Page" : page->title);
    } else if (scope == ExportScope::CurrentSection && section) {
        base = SanitizeFilename(section->name.empty() ? "Untitled_Section" : section->name);
    } else if (scope == ExportScope::EntireNotebook && notebook) {
        base = SanitizeFilename(notebook->name.empty() ? "Untitled_Notebook" : notebook->name);
    } else if (scope == ExportScope::EntireLibrary && !libraryPath.empty()) {
        base = SanitizeFilename(FileManager::GetStem(libraryPath));
    }

    switch (format) {
        case ExportFormat::PDF_Print:      return base + "_Print.html";
        case ExportFormat::HTML_Document:  return base + ".html";
        case ExportFormat::PDF_Vector:     return base + ".pdf";
        case ExportFormat::MarkdownText:   return base + ".md";
        case ExportFormat::FolioPackage:   return base + ".folionb.7z";
        case ExportFormat::LibraryPackage: return base + ".foliolib.7z";
        default:                           return base + ".txt";
    }
}

bool ExportManager::Export(
    ExportScope scope,
    ExportFormat format,
    const std::shared_ptr<Notebook>& notebook,
    const std::shared_ptr<Section>& section,
    const std::shared_ptr<CanvasPage>& page,
    const std::string& destinationPath,
    const ExportOptions& options
) {
    std::string outPath = destinationPath;
    if (outPath.empty() || FileManager::IsDirectory(outPath) || FileManager::GetExtension(outPath).empty()) {
        std::string baseDir = outPath.empty() ? FileManager::GetExportsDirectory() : outPath;
        FileManager::CreateDirectories(baseDir);
        std::string defaultFilename = GetExportFilename(scope, format, notebook, section, page, options.libraryPath);
        outPath = FileManager::JoinPath(baseDir, defaultFilename);
    } else {
        std::string parentDir = FileManager::GetParentPath(outPath);
        if (!parentDir.empty()) {
            FileManager::CreateDirectories(parentDir);
        }
    }

    LOG_INFO(FileManager, "ExportManager: Starting export. Scope=" + std::to_string(static_cast<int>(scope)) +
             ", Format=" + std::to_string(static_cast<int>(format)) + " -> " + outPath);

    switch (format) {
        case ExportFormat::HTML_Document:
        case ExportFormat::PDF_Print: {
            std::string html;
            if (scope == ExportScope::CurrentPage && page) {
                std::string secName = section ? section->name : "Section";
                std::string nbName = notebook ? notebook->name : "Notebook";
                html = HtmlSvgExporter::ExportPage(page, secName, nbName, options);
            } else if (scope == ExportScope::CurrentSection && section) {
                std::string nbName = notebook ? notebook->name : "Notebook";
                html = HtmlSvgExporter::ExportSection(section, nbName, options);
            } else if (notebook) {
                html = HtmlSvgExporter::ExportNotebook(notebook, options);
            } else {
                return false;
            }

            if (!FileManager::WriteTextAtomic(outPath, html)) {
                LOG_ERROR(FileManager, "ExportManager: Failed to write HTML to: " + outPath);
                return false;
            }

            if (format == ExportFormat::PDF_Print && options.triggerPrintDialog) {
#if defined(_WIN32)
                ShellExecuteA(NULL, "print", outPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
            }
            return true;
        }

        case ExportFormat::PDF_Vector: {
            if (scope == ExportScope::CurrentPage && page) {
                return PdfVectorExporter::ExportPage(page, outPath, options);
            } else if (scope == ExportScope::CurrentSection && section) {
                return PdfVectorExporter::ExportSection(section, outPath, options);
            } else if (notebook) {
                return PdfVectorExporter::ExportNotebook(notebook, outPath, options);
            }
            return false;
        }

        case ExportFormat::MarkdownText: {
            std::string outDir = FileManager::GetParentPath(outPath);
            std::string md;

            if (scope == ExportScope::CurrentPage && page) {
                std::string secName = section ? section->name : "Section";
                std::string nbName = notebook ? notebook->name : "Notebook";
                md = MarkdownExporter::ExportPage(page, secName, nbName, outDir, options);
            } else if (scope == ExportScope::CurrentSection && section) {
                std::string nbName = notebook ? notebook->name : "Notebook";
                md = MarkdownExporter::ExportSection(section, nbName, outDir, options);
            } else if (notebook) {
                md = MarkdownExporter::ExportNotebook(notebook, outDir, options);
            } else {
                return false;
            }

            return FileManager::WriteTextAtomic(outPath, md);
        }

        case ExportFormat::FolioPackage: {
            if (!notebook || notebook->filePath.empty()) return false;
            return PackageExporter::ExportNotebookPackage(notebook->filePath, outPath, options.compressionLevel);
        }

        case ExportFormat::LibraryPackage: {
            std::string libPath = options.libraryPath;
            if (libPath.empty() && notebook && !notebook->filePath.empty()) {
                libPath = FileManager::GetParentPath(notebook->filePath);
            }
            if (libPath.empty()) return false;
            return PackageExporter::ExportLibraryPackage(libPath, outPath, options.compressionLevel);
        }

        default:
            return false;
    }
}

std::future<bool> ExportManager::ExportAsync(
    ExportScope scope,
    ExportFormat format,
    const std::shared_ptr<Notebook>& notebook,
    const std::shared_ptr<Section>& section,
    const std::shared_ptr<CanvasPage>& page,
    const std::string& destinationPath,
    const ExportOptions& options
) {
    return GetGlobalThreadPool().Enqueue([=]() -> bool {
        return Export(scope, format, notebook, section, page, destinationPath, options);
    });
}

} // namespace Folio
