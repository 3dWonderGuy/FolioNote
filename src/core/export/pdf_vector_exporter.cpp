/**
 * =========================================================================================
 * @file pdf_vector_exporter.cpp
 * @brief Implementation of Standalone High-Fidelity Vector PDF Exporter
 * =========================================================================================
 *
 * ARCHITECTURAL IMPLEMENTATION DETAILS:
 * 1. Native Vector Graphics Pipeline:
 *    Writes PDF drawing operators directly with zero third-party dependencies:
 *    - Coordinate translation: Converts canvas millimeters to PDF typography points ($1\text{ mm} \approx 2.83465\text{ pt}$).
 *    - Inverts the Y-axis to match the standard Cartesian bottom-left origin of PDF.
 *    - Sets round line caps (`1 J`) and round line joins (`1 j`) for pen stroke fidelity.
 * 2. Searchable Text Operators:
 *    Encodes text boxes using PDF text blocks (`BT ... /F1 ... Tf (...) Tj ET`) with Helvetica
 *    standard Type 1 font metrics, ensuring all words are searchable (`Ctrl+F`) and copyable.
 * 3. Interactive URI Link Annotations:
 *    Extracts hyperlinks and generates interactive `/Subtype /Link` annotations with `/S /URI`,
 *    preserving web references and desktop deep links (`folionote://page/...`).
 */

#include "core/export/pdf_vector_exporter.hpp"
#include "core/export/sheet_tiler.hpp"
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/ink_container.hpp"
#include "core/objects/text_box.hpp"
#include "utils/file_manager.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <cmath>

namespace Folio {

namespace {

constexpr double MM_TO_PT = 72.0 / 25.4; // 2.83464567293

std::string EscapePdfString(const std::string& input) {
    std::string out;
    for (char c : input) {
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

struct PdfPageContent {
    std::string streamData;
    std::vector<std::string> linkAnnotDicts;
    double widthPt = 595.28;  // A4 default
    double heightPt = 841.89; // A4 default
};

PdfPageContent GenerateSheetPdfContent(
    const SheetTile& tile,
    const std::shared_ptr<CanvasPage>& page,
    const ExportOptions& options
) {
    PdfPageContent result;
    result.widthPt = tile.widthMm * MM_TO_PT;
    result.heightPt = tile.heightMm * MM_TO_PT;

    std::ostringstream ss;
    double invScale = tile.uniformScale * MM_TO_PT;

    // 1. Vector Ink Strokes
    for (const auto& obj : tile.intersectingObjects) {
        if (auto ink = std::dynamic_pointer_cast<InkContainer>(obj)) {
            for (const auto& stroke : ink->strokes) {
                if (stroke.segments.empty()) continue;

                uint32_t val = stroke.color.value;
                float r = ((val >> 16) & 0xFF) / 255.0f;
                float g = ((val >> 8) & 0xFF) / 255.0f;
                float b = (val & 0xFF) / 255.0f;
                double widthPt = std::max(0.5, stroke.baseWidth * invScale);

                // Set stroke color, line width, round caps (1 J), and round joins (1 j)
                ss << std::fixed << std::setprecision(2);
                ss << r << " " << g << " " << b << " RG\n";
                ss << widthPt << " w 1 J 1 j\n";

                // First point
                double p0x = (stroke.segments[0].p0.x - tile.worldX) * invScale;
                double p0y = result.heightPt - ((stroke.segments[0].p0.y - tile.worldY) * invScale);
                ss << p0x << " " << p0y << " m\n";

                for (const auto& seg : stroke.segments) {
                    double p1x = (seg.p1.x - tile.worldX) * invScale;
                    double p1y = result.heightPt - ((seg.p1.y - tile.worldY) * invScale);
                    ss << p1x << " " << p1y << " l\n";
                }
                ss << "S\n";
            }
        }
    }

    // 2. Searchable Text Objects & Interactive Link Annotations
    for (const auto& obj : tile.intersectingObjects) {
        if (auto tb = std::dynamic_pointer_cast<Folio::TextBoxObject>(obj)) {
            double txPt = (tb->worldX - tile.worldX) * invScale;
            double fontSizePt = (tb->fontSize > 0.0f ? tb->fontSize : 12.0f) * static_cast<float>(tile.uniformScale);
            double tyPt = result.heightPt - ((tb->worldY - tile.worldY) * invScale) - fontSizePt;

            // Split text by lines
            std::istringstream textStream(tb->text);
            std::string line;
            double currentY = tyPt;

            ss << "BT\n/F1 " << fontSizePt << " Tf\n0.12 0.13 0.16 rg\n";

            while (std::getline(textStream, line)) {
                if (!line.empty()) {
                    ss << txPt << " " << currentY << " Td\n";
                    ss << "(" << EscapePdfString(line) << ") Tj\n";
                    ss << (-txPt) << " " << 0 << " Td\n"; // Return X to 0

                    // Check for hyperlinks in line
                    if (options.enableLinks) {
                        static const std::regex urlRegex(R"((https?://[^\s<>"]+|folionote://[^\s<>"]+))");
                        auto wordsBegin = std::sregex_iterator(line.begin(), line.end(), urlRegex);
                        auto wordsEnd = std::sregex_iterator();

                        for (auto it = wordsBegin; it != wordsEnd; ++it) {
                            std::smatch match = *it;
                            std::string url = match.str();

                            bool allowed = (url.rfind("folionote://", 0) == 0) ? options.enableAppDeepLinks : options.enableWebLinks;
                            if (allowed) {
                                double approxCharW = fontSizePt * 0.55;
                                double linkX1 = txPt + (match.position() * approxCharW);
                                double linkX2 = linkX1 + (match.length() * approxCharW);
                                double linkY1 = currentY - 2.0;
                                double linkY2 = currentY + fontSizePt + 2.0;

                                std::ostringstream annot;
                                annot << "<< /Type /Annot /Subtype /Link /Rect ["
                                      << linkX1 << " " << linkY1 << " " << linkX2 << " " << linkY2
                                      << "] /Border [0 0 0] /A << /S /URI /URI (" << EscapePdfString(url) << ") >> >>";
                                result.linkAnnotDicts.push_back(annot.str());
                            }
                        }
                    }
                }
                currentY -= (fontSizePt * 1.35); // Advance line height
            }
            ss << "ET\n";
        }
    }

    result.streamData = ss.str();
    return result;
}

bool WriteCompletePdfDocument(const std::vector<PdfPageContent>& pages, const std::string& destinationPdfPath) {
    if (pages.empty()) return false;

    std::string destDir = FileManager::GetParentPath(destinationPdfPath);
    if (!destDir.empty()) {
        FileManager::CreateDirectories(destDir);
    }

    std::ofstream pdf(destinationPdfPath, std::ios::binary | std::ios::trunc);
    if (!pdf.is_open()) return false;

    std::vector<size_t> xrefOffsets;
    xrefOffsets.push_back(0); // Object 0 is special

    auto RecordObject = [&](std::string_view) {
        xrefOffsets.push_back(static_cast<size_t>(pdf.tellp()));
    };

    // PDF Header
    pdf << "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";

    // 1. Catalog Object (1 0 obj)
    RecordObject("Catalog");
    pdf << "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    // 2. Pages Root Object (2 0 obj)
    // Page objects start at object ID 4
    size_t pageCount = pages.size();
    RecordObject("Pages");
    pdf << "2 0 obj\n<< /Type /Pages /Count " << pageCount << " /Kids [";
    for (size_t i = 0; i < pageCount; ++i) {
        pdf << (4 + i * 2) << " 0 R ";
    }
    pdf << "] >>\nendobj\n";

    // 3. Shared Font Object (3 0 obj) - Standard Helvetica Type 1
    RecordObject("Font");
    pdf << "3 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\nendobj\n";

    // Write individual Page and Content stream objects
    for (size_t i = 0; i < pageCount; ++i) {
        const auto& pg = pages[i];
        size_t pageObjId = 4 + i * 2;
        size_t contentObjId = pageObjId + 1;

        // Page Object
        RecordObject("Page");
        pdf << pageObjId << " 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
            << pg.widthPt << " " << pg.heightPt << "] /Contents " << contentObjId
            << " 0 R /Resources << /Font << /F1 3 0 R >> >>";

        if (!pg.linkAnnotDicts.empty()) {
            pdf << " /Annots [";
            for (const auto& a : pg.linkAnnotDicts) {
                pdf << a << " ";
            }
            pdf << "]";
        }
        pdf << " >>\nendobj\n";

        // Content Stream Object
        RecordObject("Content");
        pdf << contentObjId << " 0 obj\n<< /Length " << pg.streamData.size() << " >>\nstream\n"
            << pg.streamData << "endstream\nendobj\n";
    }

    // Cross-Reference Table
    size_t xrefPos = static_cast<size_t>(pdf.tellp());
    size_t totalObjects = xrefOffsets.size();
    pdf << "xref\n0 " << totalObjects << "\n";
    pdf << "0000000000 65535 f \n";

    for (size_t i = 1; i < totalObjects; ++i) {
        std::ostringstream offsetStr;
        offsetStr << std::setw(10) << std::setfill('0') << xrefOffsets[i];
        pdf << offsetStr.str() << " 00000 n \n";
    }

    // Trailer
    pdf << "trailer\n<< /Size " << totalObjects << " /Root 1 0 R >>\n";
    pdf << "startxref\n" << xrefPos << "\n%%EOF\n";

    pdf.close();
    LOG_INFO(FileManager, "PdfVectorExporter: Successfully wrote vector PDF (" +
             std::to_string(pageCount) + " pages) to: " + destinationPdfPath);
    return true;
}

} // anonymous namespace

bool PdfVectorExporter::ExportPage(
    const std::shared_ptr<CanvasPage>& page,
    const std::string& destinationPdfPath,
    const ExportOptions& options
) {
    if (!page) return false;
    auto tiles = SheetTiler::TilePage(
        *page, options.traversalOrder, options.scalingMode, options.skipEmptySheets
    );

    std::vector<PdfPageContent> pdfPages;
    for (const auto& tile : tiles) {
        pdfPages.push_back(GenerateSheetPdfContent(tile, page, options));
    }

    return WriteCompletePdfDocument(pdfPages, destinationPdfPath);
}

bool PdfVectorExporter::ExportSection(
    const std::shared_ptr<Section>& section,
    const std::string& destinationPdfPath,
    const ExportOptions& options
) {
    if (!section) return false;
    std::vector<PdfPageContent> pdfPages;

    for (const auto& pg : section->pages) {
        if (!pg) continue;
        auto tiles = SheetTiler::TilePage(
            *pg, options.traversalOrder, options.scalingMode, options.skipEmptySheets
        );
        for (const auto& tile : tiles) {
            pdfPages.push_back(GenerateSheetPdfContent(tile, pg, options));
        }
    }

    return WriteCompletePdfDocument(pdfPages, destinationPdfPath);
}

bool PdfVectorExporter::ExportNotebook(
    const std::shared_ptr<Notebook>& notebook,
    const std::string& destinationPdfPath,
    const ExportOptions& options
) {
    if (!notebook) return false;
    std::vector<PdfPageContent> pdfPages;

    for (const auto& sec : notebook->sections) {
        if (!sec) continue;
        for (const auto& pg : sec->pages) {
            if (!pg) continue;
            auto tiles = SheetTiler::TilePage(
                *pg, options.traversalOrder, options.scalingMode, options.skipEmptySheets
            );
            for (const auto& tile : tiles) {
                pdfPages.push_back(GenerateSheetPdfContent(tile, pg, options));
            }
        }
    }

    return WriteCompletePdfDocument(pdfPages, destinationPdfPath);
}

} // namespace Folio
