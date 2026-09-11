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

struct PdfTextLine {
    int startChar = 0;
    int endChar = 0;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

class PdfTextLayer {
public:
    std::vector<PdfCharInfo> chars;
    std::vector<PdfTextLine> lines;
    double pageWidthMm = 210.0;
    double pageHeightMm = 297.0;
    double medianFontSize = 12.0;

    void Clear() {
        chars.clear();
        lines.clear();
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
        double ptW = FPDF_GetPageWidthF(page);
        double ptH = FPDF_GetPageHeightF(page);
        pageWidthMm = ptW * PT_TO_MM;
        pageHeightMm = ptH * PT_TO_MM;

        chars.reserve(totalChars);
        std::vector<double> fontSizes;
        fontSizes.reserve(totalChars);

        constexpr int DEV_RES = 10000;

        for (int i = 0; i < totalChars; ++i) {
            double l = 0.0, r = 0.0, b = 0.0, t = 0.0;
            FPDFText_GetCharBox(textPage, i, &l, &r, &b, &t);
            uint32_t ch = FPDFText_GetUnicode(textPage, i);
            double fs = FPDFText_GetFontSize(textPage, i);

            // Use FPDF_PageToDevice to map all 4 corners through page transformation matrix.
            // This guarantees 100% pixel-perfect alignment regardless of page rotation (0/90/180/270)
            // or non-zero MediaBox/CropBox offsets.
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, x4 = 0, y4 = 0;
            FPDF_PageToDevice(page, 0, 0, DEV_RES, DEV_RES, 0, l, b, &x1, &y1);
            FPDF_PageToDevice(page, 0, 0, DEV_RES, DEV_RES, 0, r, b, &x2, &y2);
            FPDF_PageToDevice(page, 0, 0, DEV_RES, DEV_RES, 0, r, t, &x3, &y3);
            FPDF_PageToDevice(page, 0, 0, DEV_RES, DEV_RES, 0, l, t, &x4, &y4);

            double minX = std::min({x1, x2, x3, x4}) / static_cast<double>(DEV_RES) * pageWidthMm;
            double maxX = std::max({x1, x2, x3, x4}) / static_cast<double>(DEV_RES) * pageWidthMm;
            double minY = std::min({y1, y2, y3, y4}) / static_cast<double>(DEV_RES) * pageHeightMm;
            double maxY = std::max({y1, y2, y3, y4}) / static_cast<double>(DEV_RES) * pageHeightMm;

            // Handle spaces and zero-dimension characters gracefully
            bool isWhitespace = (ch <= 0x20 || ch == 0x00A0 || ch == 0x3000);
            if ((maxX <= minX || maxY <= minY) && !chars.empty()) {
                const auto& prev = chars.back();
                minY = prev.top;
                maxY = prev.bottom;
                minX = prev.right;
                double charFs = (fs > 1.0) ? fs : (medianFontSize > 1.0 ? medianFontSize : 12.0);
                maxX = minX + (charFs * PT_TO_MM * 0.30);
            }

            PdfCharInfo info;
            info.charIndex = i;
            info.unicode = ch;
            info.left = minX;
            info.right = maxX;
            info.top = minY;
            info.bottom = maxY;
            info.fontSize = fs;

            chars.push_back(info);
            if (fs > 1.0 && !isWhitespace) fontSizes.push_back(fs);
        }

        if (!fontSizes.empty()) {
            std::sort(fontSizes.begin(), fontSizes.end());
            medianFontSize = fontSizes[fontSizes.size() / 2];
        }

        // Build structured lines for robust, line-aware text selection tracking
        lines.clear();
        if (!chars.empty()) {
            PdfTextLine curLine;
            curLine.startChar = 0;
            curLine.endChar = 0;
            curLine.left = chars[0].left;
            curLine.right = chars[0].right;
            curLine.top = chars[0].top;
            curLine.bottom = chars[0].bottom;

            for (size_t i = 1; i < chars.size(); ++i) {
                const auto& c = chars[i];

                if (chars[i - 1].unicode == '\n' || chars[i - 1].unicode == '\r') {
                    lines.push_back(curLine);
                    curLine.startChar = static_cast<int>(i);
                    curLine.endChar = static_cast<int>(i);
                    curLine.left = c.left;
                    curLine.right = c.right;
                    curLine.top = c.top;
                    curLine.bottom = c.bottom;
                    continue;
                }

                double overlapMin = std::max(c.top, curLine.top);
                double overlapMax = std::min(c.bottom, curLine.bottom);
                double charH = c.bottom - c.top;
                double lineH = curLine.bottom - curLine.top;
                bool vertOverlap = (overlapMax > overlapMin) &&
                                   ((overlapMax - overlapMin) >= std::min(charH, lineH) * 0.35);

                // Disallow extreme backward wrap on the same vertical span (e.g. multi-column layouts)
                bool horizWrap = (c.left < curLine.left - 5.0);

                if (vertOverlap && !horizWrap) {
                    curLine.endChar = static_cast<int>(i);
                    curLine.left = std::min(curLine.left, c.left);
                    curLine.right = std::max(curLine.right, c.right);
                    curLine.top = std::min(curLine.top, c.top);
                    curLine.bottom = std::max(curLine.bottom, c.bottom);
                } else {
                    lines.push_back(curLine);
                    curLine.startChar = static_cast<int>(i);
                    curLine.endChar = static_cast<int>(i);
                    curLine.left = c.left;
                    curLine.right = c.right;
                    curLine.top = c.top;
                    curLine.bottom = c.bottom;
                }
            }
            lines.push_back(curLine);
        }

        FPDFText_ClosePage(textPage);
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Tests if a point (localX, localY in mm) hits a character glyph box.
     */
    [[nodiscard]] int HitTestChar(double localX, double localY, double toleranceMm = 3.0) const {
        if (chars.empty()) return -1;

        // 1. Direct bounding box check
        for (const auto& c : chars) {
            if (localX >= c.left && localX <= c.right && localY >= c.top && localY <= c.bottom) {
                return c.charIndex;
            }
        }

        // 2. Tolerance-padded proximity check
        for (const auto& c : chars) {
            if (localX >= (c.left - toleranceMm) && localX <= (c.right + toleranceMm) &&
                localY >= (c.top - toleranceMm) && localY <= (c.bottom + toleranceMm)) {
                return c.charIndex;
            }
        }
        return -1;
    }

    /**
     * @brief Robust line-aware character finder for drag selection.
     * Snaps to the closest reading line and clamps to line bounds in margins without erratic jumping.
     */
    [[nodiscard]] int FindNearestChar(double localX, double localY) const {
        if (chars.empty()) return -1;
        if (lines.empty()) return 0;

        // 1. Find matching line vertically
        int bestLineIdx = 0;
        double bestLineDist = 1e18;

        for (int l = 0; l < static_cast<int>(lines.size()); ++l) {
            const auto& line = lines[l];
            if (localY >= line.top && localY <= line.bottom) {
                bestLineIdx = l;
                bestLineDist = 0.0;
                break;
            }
            double lineMidY = (line.top + line.bottom) * 0.5;
            double distY = std::abs(localY - lineMidY);
            if (distY < bestLineDist) {
                bestLineDist = distY;
                bestLineIdx = l;
            }
        }

        const auto& targetLine = lines[bestLineIdx];

        // 2. If mouse is to the left of the line, smoothly clamp to line start
        if (localX <= targetLine.left) {
            return targetLine.startChar;
        }

        // 3. If mouse is to the right of the line, smoothly clamp to line end
        if (localX >= targetLine.right) {
            return targetLine.endChar;
        }

        // 4. Find exact or closest character on this line
        int bestChar = targetLine.startChar;
        double bestCharDist = 1e18;
        for (int c = targetLine.startChar; c <= targetLine.endChar; ++c) {
            const auto& ch = chars[c];
            if (localX >= ch.left && localX <= ch.right) {
                return c;
            }
            double midX = (ch.left + ch.right) * 0.5;
            double d = std::abs(localX - midX);
            if (d < bestCharDist) {
                bestCharDist = d;
                bestChar = c;
            }
        }
        return bestChar;
    }

    /**
     * @brief Extracts bounding box rectangles for all selected characters.
     */
    [[nodiscard]] std::vector<AABB> GetSelectionBoxes(const PdfTextSelection& sel) const {
        std::vector<AABB> boxes;
        if (!sel.hasSelection || chars.empty()) return boxes;

        int minI = std::clamp(sel.MinIdx(), 0, static_cast<int>(chars.size()) - 1);
        int maxI = std::clamp(sel.MaxIdx(), 0, static_cast<int>(chars.size()) - 1);

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

            if (c.right <= c.left || c.bottom <= c.top) {
                continue; // Skip zero-dimension glyphs
            }

            AABB charBox(c.left, c.top, c.right, c.bottom);
            if (!hasLine) {
                currentLineBox = charBox;
                hasLine = true;
            } else {
                // Check vertical overlap with current line
                double overlapMin = std::max(c.top, currentLineBox.minY);
                double overlapMax = std::min(c.bottom, currentLineBox.maxY);
                double charH = c.bottom - c.top;
                double lineH = currentLineBox.maxY - currentLineBox.minY;
                bool sameLine = (overlapMax > overlapMin) &&
                                ((overlapMax - overlapMin) >= std::min(charH, lineH) * 0.35);

                if (sameLine) {
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
