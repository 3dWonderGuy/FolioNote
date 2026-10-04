#include "core/pdf_engine/pdf_virtualizer.hpp"
#include <algorithm>
#include <cmath>

namespace Folio {

void PdfVirtualizer::ResetViewport() noexcept {
    scrollX = 0.0f;
    scrollY = 0.0f;
    maxScrollX = 0.0f;
    maxScrollY = 0.0f;
    zoomScale = 1.25f;
    activePageIndex = 0;
}

double PdfVirtualizer::GetPageTopMm(int pageIndex, const std::vector<PdfPageDimension>& dims) const noexcept {
    double top = 0.0;
    for (int i = 0; i < pageIndex && i < static_cast<int>(dims.size()); ++i) {
        top += (dims[i].heightMm + PAGE_GAP_MM);
    }
    return top;
}

Point2D PdfVirtualizer::ScreenToPageMm(int pageIndex, float screenX, float screenY, ImVec2 pMin, float pxPerMm, const std::vector<PdfPageDimension>& dims) const noexcept {
    double scale = 1.0 / static_cast<double>(pxPerMm);
    double localX = static_cast<double>(screenX - pMin.x) * scale;
    double localY = static_cast<double>(screenY - pMin.y) * scale;
    return Point2D{ localX, GetPageTopMm(pageIndex, dims) + localY };
}

ImVec2 PdfVirtualizer::PageMmToScreen(int pageIndex, double worldX, double worldY, ImVec2 pMin, float pxPerMm, const std::vector<PdfPageDimension>& dims) const noexcept {
    double localX = worldX;
    double localY = worldY - GetPageTopMm(pageIndex, dims);
    return ImVec2(
        pMin.x + static_cast<float>(localX * static_cast<double>(pxPerMm)),
        pMin.y + static_cast<float>(localY * static_cast<double>(pxPerMm))
    );
}

std::pair<int, int> PdfVirtualizer::GetVisiblePageRange(float viewH, int totalPages, const std::vector<PdfPageDimension>& dims) const noexcept {
    if (totalPages <= 0) return { -1, -1 };

    float pxPerMm = SCALE_DPI * zoomScale;
    float curY = TOP_MARGIN_PX;
    int firstVisible = -1;
    int lastVisible = -1;

    for (int i = 0; i < totalPages; ++i) {
        double mmH = (i < static_cast<int>(dims.size())) ? dims[i].heightMm : 297.0;
        float pageH_px = static_cast<float>(mmH * pxPerMm);

        float pageTopScreen = curY - scrollY;
        float pageBottomScreen = pageTopScreen + pageH_px;

        if (pageBottomScreen >= 0.0f && pageTopScreen <= viewH) {
            if (firstVisible == -1) firstVisible = i;
            lastVisible = i;
        }

        curY += pageH_px + PAGE_GAP_PX;
    }

    if (firstVisible == -1) {
        return { 0, 0 };
    }
    return { firstVisible, lastVisible };
}

void PdfVirtualizer::ZoomAtPoint(float targetZoom, float anchorScreenX, float anchorScreenY,
                                 float contentX, float contentW, float originY, float viewH,
                                 int totalPages, const std::vector<PdfPageDimension>& dims) noexcept {
    float oldZoom = zoomScale;
    float newZoom = std::clamp(targetZoom, 0.4f, 4.0f);
    if (std::abs(newZoom - oldZoom) < 0.0001f) return;

    float oldPxPerMm = SCALE_DPI * oldZoom;
    float newPxPerMm = SCALE_DPI * newZoom;

    // Document millimeter width
    float maxDocW_mm = 0.0f;
    for (int i = 0; i < totalPages; ++i) {
        double mmW = (i < static_cast<int>(dims.size())) ? dims[i].widthMm : 210.0;
        if (static_cast<float>(mmW) > maxDocW_mm) maxDocW_mm = static_cast<float>(mmW);
    }
    if (maxDocW_mm < 10.0f) maxDocW_mm = 210.0f;

    float oldMaxDocW_px = maxDocW_mm * oldPxPerMm;
    float newMaxDocW_px = maxDocW_mm * newPxPerMm;

    // 1. VERTICAL ANCHORING
    float anchorRelY = anchorScreenY - originY;
    float oldDocY = scrollY + anchorRelY;

    int anchorPageIdx = 0;
    double anchorLocalMmY = 0.0;
    double cumMmH = 0.0;
    float curPageTopDocY = TOP_MARGIN_PX;

    for (int i = 0; i < totalPages; ++i) {
        double mmH = (i < static_cast<int>(dims.size())) ? dims[i].heightMm : 297.0;
        float pageH_px = static_cast<float>(mmH * oldPxPerMm);
        if (oldDocY <= curPageTopDocY + pageH_px + (PAGE_GAP_PX * 0.5f) || i == totalPages - 1) {
            anchorPageIdx = i;
            anchorLocalMmY = std::clamp(static_cast<double>(oldDocY - curPageTopDocY) / oldPxPerMm, 0.0, mmH);
            break;
        }
        cumMmH += mmH;
        curPageTopDocY += pageH_px + PAGE_GAP_PX;
    }

    double cumMmH_anchor = 0.0;
    for (int i = 0; i < anchorPageIdx && i < static_cast<int>(dims.size()); ++i) {
        cumMmH_anchor += dims[i].heightMm;
    }

    float newPageTopDocY = TOP_MARGIN_PX + (anchorPageIdx * PAGE_GAP_PX) + static_cast<float>(cumMmH_anchor * newPxPerMm);
    float newDocY = newPageTopDocY + static_cast<float>(anchorLocalMmY * newPxPerMm);
    float newScrollY = newDocY - anchorRelY;

    float newTotalDocH_px = 40.0f;
    for (int i = 0; i < totalPages; ++i) {
        double mmH = (i < static_cast<int>(dims.size())) ? dims[i].heightMm : 297.0;
        newTotalDocH_px += static_cast<float>(mmH * newPxPerMm) + PAGE_GAP_PX;
    }
    float newMaxScrollY = std::max(0.0f, newTotalDocH_px - viewH);
    scrollY = std::clamp(newScrollY, 0.0f, newMaxScrollY);

    // 2. HORIZONTAL ANCHORING
    float newMaxScrollX = std::max(0.0f, newMaxDocW_px + 30.0f - contentW);
    float oldMaxScrollX = std::max(0.0f, oldMaxDocW_px + 30.0f - contentW);

    if (newMaxScrollX <= 0.0f) {
        scrollX = 0.0f;
    } else {
        float anchorRelX = anchorScreenX - contentX;
        float newScrollX = 0.0f;

        if (oldMaxScrollX > 0.0f) {
            float oldDocX = scrollX + anchorRelX;
            float scalingX = oldDocX - 15.0f;
            float newDocX = 15.0f + scalingX * (newZoom / oldZoom);
            newScrollX = newDocX - anchorRelX;
        } else {
            float oldPageScreenLeft = contentX + (contentW - oldMaxDocW_px) * 0.5f;
            float offsetFromPageLeft = anchorScreenX - oldPageScreenLeft;
            float newOffsetFromPageLeft = offsetFromPageLeft * (newZoom / oldZoom);
            newScrollX = 15.0f + newOffsetFromPageLeft - anchorRelX;
        }

        scrollX = std::clamp(newScrollX, 0.0f, newMaxScrollX);
    }

    zoomScale = newZoom;
    maxScrollX = newMaxScrollX;
    maxScrollY = newMaxScrollY;
}

void PdfVirtualizer::SetZoomScale(float z) noexcept {
    zoomScale = std::clamp(z, 0.4f, 4.0f);
}

void PdfVirtualizer::ScrollToPage(int pageIdx, int totalPages, const std::vector<PdfPageDimension>& dims) noexcept {
    if (pageIdx < 0) pageIdx = 0;
    if (pageIdx >= totalPages) pageIdx = totalPages - 1;
    activePageIndex = pageIdx;

    float targetY = 0.0f;
    float pxPerMm = SCALE_DPI * zoomScale;

    for (int p = 0; p < pageIdx; ++p) {
        double hMm = (p < static_cast<int>(dims.size())) ? dims[p].heightMm : 297.0;
        targetY += static_cast<float>(hMm * pxPerMm) + PAGE_GAP_PX;
    }

    scrollY = std::clamp(targetY, 0.0f, maxScrollY);
}

} // namespace Folio
