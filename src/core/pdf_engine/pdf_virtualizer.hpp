#pragma once

#include <vector>
#include <utility>
#include "imgui.h"
#include "core/ink_engine/stroke_smoother.hpp"
#include "core/pdf_engine/pdf_types.hpp"

namespace Folio {

/**
 * @brief Continuous multi-page PDF layout virtualizer and camera projector.
 * Handles cumulative millimeter coordinate math, visible page windowing,
 * drift-free cursor-anchored zoom, and viewport panning.
 */
class PdfVirtualizer {
public:
    float scrollX = 0.0f;
    float maxScrollX = 0.0f;
    float scrollY = 0.0f;
    float maxScrollY = 0.0f;
    float zoomScale = 1.25f;
    int activePageIndex = 0;

    constexpr static float TOP_MARGIN_PX = 20.0f;
    constexpr static float PAGE_GAP_PX   = 24.0f;
    constexpr static double PAGE_GAP_MM  = 24.0 / (96.0 / 25.4); // 6.35 mm
    constexpr static float SCALE_DPI     = 96.0f / 25.4f;

    PdfVirtualizer() = default;

    void ResetViewport() noexcept;

    /**
     * @brief Computes the cumulative vertical offset in millimeters of page @p pageIndex.
     */
    [[nodiscard]] double GetPageTopMm(int pageIndex, const std::vector<PdfPageDimension>& dims) const noexcept;

    /**
     * @brief Converts screen pixel coordinates on page @p pageIndex into world millimeters.
     */
    [[nodiscard]] Point2D ScreenToPageMm(int pageIndex, float screenX, float screenY, ImVec2 pMin, float pxPerMm, const std::vector<PdfPageDimension>& dims) const noexcept;

    /**
     * @brief Converts world millimeter coordinates to screen pixel coordinates on page @p pageIndex.
     */
    [[nodiscard]] ImVec2 PageMmToScreen(int pageIndex, double worldX, double worldY, ImVec2 pMin, float pxPerMm, const std::vector<PdfPageDimension>& dims) const noexcept;

    /**
     * @brief Determines the range [startPage, endPage] of pages currently intersecting the viewport.
     */
    [[nodiscard]] std::pair<int, int> GetVisiblePageRange(float viewH, int totalPages, const std::vector<PdfPageDimension>& dims) const noexcept;

    /**
     * @brief Performs smooth, drift-free zoom anchored at an arbitrary screen coordinate.
     */
    void ZoomAtPoint(float targetZoom, float anchorScreenX, float anchorScreenY,
                     float contentX, float contentW, float originY, float viewH,
                     int totalPages, const std::vector<PdfPageDimension>& dims) noexcept;

    /**
     * @brief Sets zoom scale clamped to [0.4, 4.0].
     */
    void SetZoomScale(float z) noexcept;

    /**
     * @brief Scrolls the viewport so that the top of page @p pageIdx aligns with viewport top.
     */
    void ScrollToPage(int pageIdx, int totalPages, const std::vector<PdfPageDimension>& dims) noexcept;
};

} // namespace Folio
