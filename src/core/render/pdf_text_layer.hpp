#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include "core/spatial/aabb.hpp"
#include "utils/logger.hpp"

#if defined(FOLIO_HAS_PDFIUM)
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_doc.h>
#endif

namespace Folio {

struct PdfCharInfo {
    int charIndex = 0;
    uint32_t unicode = 0;
    double left = 0.0;    // in mm
    double top = 0.0;     // in mm
    double right = 0.0;   // in mm
    double bottom = 0.0;  // in mm
    double fontSize = 12.0;
};

struct PdfTextSelection {
    int startChar = -1;
    int endChar = -1;
    bool hasSelection = false;

    void Clear() {
        startChar = -1;
        endChar = -1;
        hasSelection = false;
    }

    [[nodiscard]] int MinIdx() const { return std::min(startChar, endChar); }
    [[nodiscard]] int MaxIdx() const { return std::max(startChar, endChar); }
};

class PdfTextLayer {
public:
    std::vector<PdfCharInfo> chars;
    double pageWidthMm = 210.0;
    double pageHeightMm = 297.0;
    double medianFontSize = 12.0;

    void Clear() {
        chars.clear();
        medianFontSize = 12.0;
    }

    bool LoadFromPage(void* fpdfPagePtr) {
        Clear();
#if defined(FOLIO_HAS_PDFIUM)
        if (!fpdfPagePtr) return false;
        FPDF_PAGE page = static_cast<FPDF_PAGE>(fpdfPagePtr);

        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        if (!textPage) return false;

        int totalChars = FPDFText_CountChars(textPage);
        if (totalChars <= 0) {
            FPDFText_ClosePage(textPage);
            return false;
        }

        constexpr double PT_TO_MM = 25.4 / 72.0;
        double ptH = FPDF_GetPageHeightF(page);
        pageWidthMm = FPDF_GetPageWidthF(page) * PT_TO_MM;
        pageHeightMm = ptH * PT_TO_MM;

        chars.reserve(totalChars);
        std::vector<double> fontSizes;
        fontSizes.reserve(totalChars);

        for (int i = 0; i < totalChars; ++i) {
            double l = 0.0, r = 0.0, b = 0.0, t = 0.0;
            FPDFText_GetCharBox(textPage, i, &l, &r, &b, &t);
            uint32_t ch = FPDFText_GetUnicode(textPage, i);
            double fs = FPDFText_GetFontSize(textPage, i);

            // PDF coordinates have origin (0, 0) at bottom-left; convert to top-left origin in mm
            PdfCharInfo info;
            info.charIndex = i;
            info.unicode = ch;
            info.left = l * PT_TO_MM;
            info.right = r * PT_TO_MM;
            info.top = (ptH - t) * PT_TO_MM;
            info.bottom = (ptH - b) * PT_TO_MM;
            info.fontSize = fs;

            chars.push_back(info);
            if (fs > 1.0) fontSizes.push_back(fs);
        }

        if (!fontSizes.empty()) {
            std::sort(fontSizes.begin(), fontSizes.end());
            medianFontSize = fontSizes[fontSizes.size() / 2];
        }

        FPDFText_ClosePage(textPage);
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Finds the character index closest to a point (x, y in local page mm).
     */
    [[nodiscard]] int HitTestChar(double localX, double localY, double toleranceMm = 3.0) const {
        int bestIdx = -1;
        double bestDistSq = toleranceMm * toleranceMm;

        for (const auto& c : chars) {
            // Check if directly inside char box
            if (localX >= c.left && localX <= c.right && localY >= c.top && localY <= c.bottom) {
                return c.charIndex;
            }

            // Otherwise proximity check to center
            double cx = (c.left + c.right) * 0.5;
            double cy = (c.top + c.bottom) * 0.5;
            double dx = localX - cx;
            double dy = localY - cy;
            double distSq = dx * dx + dy * dy;

            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestIdx = c.charIndex;
            }
        }
        return bestIdx;
    }

    /**
     * @brief Extracts bounding box rectangles for all selected characters.
     */
    [[nodiscard]] std::vector<AABB> GetSelectionBoxes(const PdfTextSelection& sel) const {
        std::vector<AABB> boxes;
        if (!sel.hasSelection || chars.empty()) return boxes;

        int minI = std::clamp(sel.MinIdx(), 0, static_cast<int>(chars.size()) - 1);
        int maxI = std::clamp(sel.MaxIdx(), 0, static_cast<int>(chars.size()) - 1);

        // Group adjacent characters on the same line into unified highlight rectangles
        AABB currentLineBox;
        bool hasLine = false;

        for (int i = minI; i <= maxI; ++i) {
            const auto& c = chars[i];
            if (c.unicode == '\r' || c.unicode == '\n') {
                if (hasLine) {
                    boxes.push_back(currentLineBox);
                    hasLine = false;
                }
                continue;
            }

            AABB charBox(c.left, c.top, c.right, c.bottom);
            if (!hasLine) {
                currentLineBox = charBox;
                hasLine = true;
            } else {
                // Check if character belongs to the same horizontal line
                if (std::abs(c.top - currentLineBox.minY) < 3.0) {
                    currentLineBox.minX = std::min(currentLineBox.minX, charBox.minX);
                    currentLineBox.maxX = std::max(currentLineBox.maxX, charBox.maxX);
                    currentLineBox.minY = std::min(currentLineBox.minY, charBox.minY);
                    currentLineBox.maxY = std::max(currentLineBox.maxY, charBox.maxY);
                } else {
                    boxes.push_back(currentLineBox);
                    currentLineBox = charBox;
                }
            }
        }

        if (hasLine) {
            boxes.push_back(currentLineBox);
        }

        return boxes;
    }

    /**
     * @brief Returns selected text as clean UTF-8 string.
     */
    [[nodiscard]] std::string GetSelectedText(const PdfTextSelection& sel) const {
        if (!sel.hasSelection || chars.empty()) return "";

        int minI = std::clamp(sel.MinIdx(), 0, static_cast<int>(chars.size()) - 1);
        int maxI = std::clamp(sel.MaxIdx(), 0, static_cast<int>(chars.size()) - 1);

        std::string result;
        for (int i = minI; i <= maxI; ++i) {
            uint32_t u = chars[i].unicode;
            if (u < 0x80) {
                result += static_cast<char>(u);
            } else if (u < 0x800) {
                result += static_cast<char>(0xC0 | ((u >> 6) & 0x1F));
                result += static_cast<char>(0x80 | (u & 0x3F));
            } else if (u < 0x10000) {
                result += static_cast<char>(0xE0 | ((u >> 12) & 0x0F));
                result += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (u & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | ((u >> 18) & 0x07));
                result += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (u & 0x3F));
            }
        }
        return result;
    }

    /**
     * @brief Reconstructs structured Markdown formatting from selected text,
     * detecting headings from font scale, lists from bullet characters, and paragraphs.
     */
    [[nodiscard]] std::string GetSelectedMarkdown(const PdfTextSelection& sel) const {
        if (!sel.hasSelection || chars.empty()) return "";

        int minI = std::clamp(sel.MinIdx(), 0, static_cast<int>(chars.size()) - 1);
        int maxI = std::clamp(sel.MaxIdx(), 0, static_cast<int>(chars.size()) - 1);

        std::string markdown;
        std::string currentLine;
        double currentLineFontSize = medianFontSize;
        bool isNewLine = true;

        for (int i = minI; i <= maxI; ++i) {
            const auto& c = chars[i];

            if (c.unicode == '\n' || c.unicode == '\r') {
                if (!currentLine.empty()) {
                    // Detect headings based on font size ratio relative to median document font size
                    double scale = currentLineFontSize / (medianFontSize > 1.0 ? medianFontSize : 12.0);
                    if (scale >= 1.6) {
                        markdown += "# " + currentLine + "\n\n";
                    } else if (scale >= 1.3) {
                        markdown += "## " + currentLine + "\n\n";
                    } else if (scale >= 1.15) {
                        markdown += "### " + currentLine + "\n\n";
                    } else {
                        // Check for bullet list indicators
                        if (currentLine.rfind("•", 0) == 0 || currentLine.rfind("-", 0) == 0 || currentLine.rfind("*", 0) == 0) {
                            markdown += "- " + currentLine.substr(currentLine.find_first_not_of(" •-*")) + "\n";
                        } else {
                            markdown += currentLine + "\n\n";
                        }
                    }
                    currentLine.clear();
                    isNewLine = true;
                }
                continue;
            }

            if (isNewLine) {
                currentLineFontSize = c.fontSize;
                isNewLine = false;
            }

            uint32_t u = c.unicode;
            if (u < 0x80) {
                currentLine += static_cast<char>(u);
            } else if (u < 0x800) {
                currentLine += static_cast<char>(0xC0 | ((u >> 6) & 0x1F));
                currentLine += static_cast<char>(0x80 | (u & 0x3F));
            } else if (u < 0x10000) {
                currentLine += static_cast<char>(0xE0 | ((u >> 12) & 0x0F));
                currentLine += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                currentLine += static_cast<char>(0x80 | (u & 0x3F));
            } else {
                currentLine += static_cast<char>(0xF0 | ((u >> 18) & 0x07));
                currentLine += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
                currentLine += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                currentLine += static_cast<char>(0x80 | (u & 0x3F));
            }
        }

        if (!currentLine.empty()) {
            double scale = currentLineFontSize / (medianFontSize > 1.0 ? medianFontSize : 12.0);
            if (scale >= 1.6) {
                markdown += "# " + currentLine + "\n";
            } else if (scale >= 1.3) {
                markdown += "## " + currentLine + "\n";
            } else if (scale >= 1.15) {
                markdown += "### " + currentLine + "\n";
            } else {
                markdown += currentLine + "\n";
            }
        }

        return markdown;
    }
};

} // namespace Folio
