#pragma once
/**
 * @file link_object.hpp
 * @brief Standalone canvas hyperlink — external URL or cross-note anchor chip.
 *
 * LinkObject is a non-resizable pill badge on the canvas that represents a
 * hyperlink. It can point to:
 *
 *   1. External URL:  "https://..." — opens in system default browser
 *   2. Cross-note anchor: "folio://notebook-uuid/page-id#selection-id"
 *      — navigates within FolioNote to another page or selection
 *
 * This object is distinct from INLINE links inside a TextBoxObject:
 *   - TextRun.linkRef references a LinkObject by UUID for inline hyperlinks
 *     (the link is anchored to a word/phrase inside a text box)
 *   - LinkObject as a standalone canvas entity is a free-floating chip that
 *     you can drop anywhere on the canvas, independent of text content
 *
 * Interaction:
 *   - Non-resizable: fixed-height pill, width expands with displayText.
 *   - Moveable: body-drag via standard gizmo.
 *   - Double-click (Phase 2): triggers Navigate().
 *
 * Scalability:
 *   Add new URL scheme handlers in Navigate(). No other files change.
 *   When cross-note navigation is implemented, the "folio://" handler
 *   will call into the document session router.
 */

#include <string>
#include <memory>
#include <algorithm>
#include <vector>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace Folio {

/**
 * @brief Standalone hyperlink chip on the canvas.
 */
class LinkObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    std::string url;            ///< "https://..." or "folio://notebook/page#sel"
    std::string displayText;    ///< Label shown in the pill badge

    double worldX = 0.0;        ///< Top-left X (world mm)
    double worldY = 0.0;        ///< Top-left Y (world mm)

    /// Fixed chip height; width adapts to displayText length (world mm)
    static constexpr double chipH    = 8.0;
    static constexpr double chipMinW = 20.0;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    LinkObject() {
        type = ObjectType::Link;
        UpdateBounds();
    }

    LinkObject(const std::string& linkUrl, const std::string& label)
        : url(linkUrl), displayText(label)
    {
        type = ObjectType::Link;
        UpdateBounds();
    }

    // =========================================================================
    // URL TYPE HELPERS
    // =========================================================================

    /**
     * @brief Returns true if this is a cross-note FolioNote anchor.
     * Folio links use the custom scheme "folio://"
     */
    [[nodiscard]] bool IsCrossNote() const noexcept {
        return url.rfind("folio://", 0) == 0;
    }

    /**
     * @brief Returns true if this is an external web URL.
     */
    [[nodiscard]] bool IsExternal() const noexcept {
        return url.rfind("http://", 0) == 0 ||
               url.rfind("https://", 0) == 0;
    }

    // =========================================================================
    // NAVIGATION
    // =========================================================================

    /**
     * @brief Opens or navigates to the linked resource.
     *
     * External URLs: ShellExecuteW on Windows (opens default browser).
     * Cross-note links: TODO — will call into the document session router
     *   to navigate to the referenced notebook/page/selection.
     */
    void Navigate() const {
        if (IsCrossNote()) {
            // TODO: parse "folio://notebook-uuid/page-id#selection-id"
            // and call into SessionRouter::NavigateTo(...)
            return;
        }
        if (IsExternal()) {
#if defined(_WIN32) || defined(_WIN64)
            int wLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
            if (wLen > 0) {
                std::wstring wUrl(wLen, 0);
                MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wUrl[0], wLen);
                ShellExecuteW(nullptr, L"open", wUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
#else
            // TODO: macOS / Linux: system("xdg-open \"" + url + "\"") etc.
#endif
        }
    }

    // =========================================================================
    // BOUNDS — chip width proportional to displayText length (approximated)
    // =========================================================================

    /**
     * @brief Computes chip width from displayText length (character approximation).
     *
     * Without a font renderer at bounds-update time, we estimate:
     *   chipW = max(chipMinW, displayText.length() * 2.2 + 8.0) world mm
     * Phase 2: replace with actual BLFont text measurement.
     */
    [[nodiscard]] double GetChipWidth() const noexcept {
        double estimated = static_cast<double>(displayText.size()) * 2.2 + 8.0;
        return (std::max)(chipMinW, estimated);
    }

    void UpdateBounds() override {
        double cw = GetChipWidth();
        BLPoint p[4] = {
            transform.map_point(worldX,      worldY),
            transform.map_point(worldX + cw, worldY),
            transform.map_point(worldX + cw, worldY + chipH),
            transform.map_point(worldX,      worldY + chipH)
        };
        double minX = p[0].x, maxX = p[0].x;
        double minY = p[0].y, maxY = p[0].y;
        for (int i = 1; i < 4; ++i) {
            minX = (std::min)(minX, p[i].x);
            maxX = (std::max)(maxX, p[i].x);
            minY = (std::min)(minY, p[i].y);
            maxY = (std::max)(maxY, p[i].y);
        }
        bounds = AABB(minX, minY, maxX, maxY);
    }

    bool HitTest(double wx, double wy) const override {
        return bounds.Contains(wx, wy);
    }

    bool Intersects(const AABB& sel) const override {
        return bounds.Intersects(sel);
    }

    // =========================================================================
    // TRANSFORM — translation only
    // =========================================================================

    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    void BakeTransform() override {
        worldX += transform.m20;
        worldY += transform.m21;
        transform = BLMatrix2D::make_identity();
        UpdateBounds();
    }

    bool GetCustomGizmoHandles(std::vector<GizmoHandle>& /*outHandles*/,
                                const CanvasTransform& /*transform*/) const override {
        return false; // body-move only
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders a pill-shaped hyperlink badge.
     *
     * External links: blue pill with chain-link icon placeholder
     * Cross-note links: teal/green pill with internal-arrow icon placeholder
     *
     * Phase 2: replace placeholder icon with actual vector icon glyphs and
     *           BLFont displayText rendering.
     */
    void Render(BLContext& ctx, const Viewport& /*viewport*/) const override {
        if (!isVisible) return;

        ctx.save();
        ctx.apply_transform(transform);

        const double x  = worldX;
        const double y  = worldY;
        const double cw = GetChipWidth();
        const double ch = chipH;
        const double r  = ch * 0.5; // Fully rounded pill

        // Pill color: blue = external, teal = cross-note
        BLRgba32 pillCol = IsCrossNote()
            ? BLRgba32(0x00, 0x82, 0x72, static_cast<uint8_t>(opacity * 200))
            : BLRgba32(0x00, 0x78, 0xD4, static_cast<uint8_t>(opacity * 200));

        ctx.set_fill_style(pillCol);
        ctx.fill_round_rect(BLRoundRect(x, y, cw, ch, r, r));

        // Link icon placeholder (small circle on the left side of pill)
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 180));
        ctx.fill_circle(x + r, y + ch * 0.5, 1.2);

        // Subtle border
        ctx.set_stroke_style(BLRgba32(0xFF, 0xFF, 0xFF, 40));
        ctx.set_stroke_width(0.3);
        ctx.stroke_round_rect(BLRoundRect(x, y, cw, ch, r, r));

        // TODO Phase 2: render displayText using BLFont

        ctx.restore();
    }

    // =========================================================================
    // DUPLICATION & PERSISTENCE
    // =========================================================================

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<LinkObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

} // namespace Folio
