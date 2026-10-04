#include "core/pdf_engine/pdf_document.hpp"
#include "core/document/document_session.hpp"
#include "core/document/canvas_page.hpp"
#include "utils/logger.hpp"

#include <sstream>
#include <algorithm>

namespace Folio {

void PdfDocument::Clear() {
    filePath.clear();
    totalPages = 0;
    pageDimensions.clear();
    docOutline.clear();
    isOutlineLoaded = false;
    userBookmarks.clear();
    lastSyncedBookmarks.clear();
    docHighlights.clear();
    lastSyncedHighlights.clear();
}

void PdfDocument::SetStructure(PdfDocSummary&& summary) {
    totalPages = summary.pageCount;
    pageDimensions.clear();
    pageDimensions.reserve(summary.dimensions.size());
    for (const auto& [w, h] : summary.dimensions) {
        pageDimensions.push_back(PdfPageDimension{ w, h });
    }
    docOutline = std::move(summary.outline);
    isOutlineLoaded = !docOutline.empty();
}

void PdfDocument::LoadBookmarksFromString(const std::string& data) {
    userBookmarks.clear();
    if (data.empty()) return;
    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            try {
                int p = std::stoi(line.substr(0, tabPos));
                std::string t = line.substr(tabPos + 1);
                userBookmarks.push_back({p, t});
            } catch (...) {}
        }
    }
}

std::string PdfDocument::SaveBookmarksToString() const {
    std::string out;
    for (const auto& bm : userBookmarks) {
        out += std::to_string(bm.pageIndex) + "\t" + bm.title + "\n";
    }
    return out;
}

void PdfDocument::SyncBookmarksToPage(DocumentSession& session) {
    auto activePage = session.GetActivePage();
    if (activePage) {
        activePage->dedicatedPdfBookmarks = SaveBookmarksToString();
        lastSyncedBookmarks = activePage->dedicatedPdfBookmarks;
        activePage->isModified = true;
        session.workspace.FlushActiveNotebookAsync();
        LOG_INFO(PdfStorage, "Synced " + std::to_string(userBookmarks.size()) + " bookmarks to page metadata.");
    }
}

void PdfDocument::LoadHighlightsFromString(const std::string& data) {
    docHighlights.clear();
    if (data.empty()) return;

    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) {
            tokens.push_back(token);
        }
        if (tokens.size() >= 8) {
            try {
                TextHighlightSpan span;
                span.pageIndex = std::stoi(tokens[0]);
                span.color = static_cast<ImU32>(std::stoul(tokens[1]));
                span.startChar = std::stoi(tokens[2]);
                span.endChar = std::stoi(tokens[3]);
                span.boundsMm.minX = std::stod(tokens[4]);
                span.boundsMm.minY = std::stod(tokens[5]);
                span.boundsMm.maxX = std::stod(tokens[6]);
                span.boundsMm.maxY = std::stod(tokens[7]);
                if (tokens.size() >= 9) {
                    span.text = tokens[8];
                }
                docHighlights[span.pageIndex].push_back(span);
            } catch (...) {}
        }
    }
}

std::string PdfDocument::SaveHighlightsToString() const {
    std::ostringstream ss;
    for (const auto& [pIdx, hlList] : docHighlights) {
        for (const auto& hl : hlList) {
            ss << hl.pageIndex << '\t'
               << hl.color << '\t'
               << hl.startChar << '\t'
               << hl.endChar << '\t'
               << hl.boundsMm.minX << '\t'
               << hl.boundsMm.minY << '\t'
               << hl.boundsMm.maxX << '\t'
               << hl.boundsMm.maxY << '\t';
            std::string cleanText = hl.text;
            std::replace(cleanText.begin(), cleanText.end(), '\n', ' ');
            std::replace(cleanText.begin(), cleanText.end(), '\r', ' ');
            std::replace(cleanText.begin(), cleanText.end(), '\t', ' ');
            ss << cleanText << '\n';
        }
    }
    return ss.str();
}

void PdfDocument::SyncHighlightsToPage(DocumentSession& session) {
    auto activePage = session.GetActivePage();
    if (activePage) {
        activePage->dedicatedPdfHighlights = SaveHighlightsToString();
        lastSyncedHighlights = activePage->dedicatedPdfHighlights;
        activePage->isModified = true;
        session.workspace.FlushActiveNotebookAsync();
        LOG_INFO(PdfStorage, "Synced text highlights to page metadata.");
    }
}

} // namespace Folio
