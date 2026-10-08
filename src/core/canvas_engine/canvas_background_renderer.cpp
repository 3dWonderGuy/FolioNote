#include "core/canvas_engine/canvas_engine.hpp"
#include "core/objects/ink_container/ink_container.hpp"
#include "core/objects/media/images/image_container.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

void CanvasEngine::GetStandardPageDimensionsMm(double& outW, double& outH) const {
    double w = 215.9, h = 279.4;
    switch (pageSizeFormat) {
        case PageSizeFormat::Letter: w = 215.9; h = 279.4; break;
        case PageSizeFormat::A4:     w = 210.0; h = 297.0; break;
        case PageSizeFormat::A3:     w = 297.0; h = 420.0; break;
        case PageSizeFormat::A5:     w = 148.0; h = 210.0; break;
        case PageSizeFormat::Custom: w = customPageWidthMm; h = customPageHeightMm; break;
    }
    if (pageIsLandscape) std::swap(w, h);
    outW = w;
    outH = h;
}

void CanvasEngine::GetCalculatedPageBoundsMm(double& outW, double& outH) const {
    double stdW, stdH;
    GetStandardPageDimensionsMm(stdW, stdH);
    if (pageBorderType == PageBorderType::Fixed) {
        outW = stdW;
        outH = stdH;
        return;
    }
    double aspect = (stdW > 0.0) ? (stdH / stdW) : 1.2941;
    double usedW = std::max(stdW, contentMaxXMm);
    double calcH = usedW * aspect;
    if (contentMaxYMm > calcH) {
        usedW = contentMaxYMm / aspect;
        calcH = contentMaxYMm;
    }
    outW = usedW;
    outH = calcH;
}

void CanvasEngine::ApplyDefaultTemplate() {
    currentPaperStyle = defaultTemplate.paperStyle;
    gridSpacingMm = defaultTemplate.gridSpacingMm;
    canvasBgColor = inkColorInverted ? defaultTemplate.invertedBgColor : defaultTemplate.normalBgColor;
    gridLineColor = inkColorInverted ? defaultTemplate.invertedLineColor : defaultTemplate.normalLineColor;
    showPageBorder = defaultTemplate.showBorder;
    pageBorderColor = defaultTemplate.borderColor;
    pageBorderWidth = defaultTemplate.borderWidth;
    pageBorderType = defaultTemplate.borderType;
    pageBorderStyle = defaultTemplate.borderStyle;
    pageSizeFormat = defaultTemplate.pageSizeFormat;
    pageIsLandscape = defaultTemplate.pageIsLandscape;
    SetInfinityMode(defaultTemplate.infinityMode);
    transform.SetDPI(static_cast<float>(defaultTemplate.calibrationDpi));
    InvalidateLayer();
}

void CanvasEngine::RenderDevModeAABBs(BLContext& ctx, const std::vector<std::shared_ptr<CanvasObject>>& visibleObjects, const CanvasTransform& tr) {
    ctx.save();

    for (const auto& obj : visibleObjects) {
        if (!obj) continue;
        AABB box = obj->GetAABB();
        Point2D pMin = tr.WorldToScreen(box.minX, box.minY);
        Point2D pMax = tr.WorldToScreen(box.maxX, box.maxY);
        double sx = std::min(pMin.x, pMax.x);
        double sy = std::min(pMin.y, pMax.y);
        double sw = std::abs(pMax.x - pMin.x);
        double sh = std::abs(pMax.y - pMin.y);

        bool isHit = false;
        for (uint32_t hid : debugCollision.hitUids) {
            if (hid == obj->uid) { isHit = true; break; }
        }
        bool isCandidate = false;
        if (!isHit) {
            for (uint32_t cid : debugCollision.candidateUids) {
                if (cid == obj->uid) { isCandidate = true; break; }
            }
        }

        BLRgba32 strokeColor;
        BLRgba32 fillColor;
        const char* typeTag = "Obj";

        if (isHit) {
            strokeColor = BLRgba32(0xFF, 0x17, 0x44, 0xE0);
            fillColor   = BLRgba32(0xFF, 0x17, 0x44, 0x2A);
        } else if (isCandidate) {
            strokeColor = BLRgba32(0xFF, 0xEA, 0x00, 0xD0);
            fillColor   = BLRgba32(0xFF, 0xEA, 0x00, 0x20);
        } else if (obj->type == ObjectType::InkContainer) {
            strokeColor = BLRgba32(0x00, 0xE5, 0xFF, 0x99);
            fillColor   = BLRgba32(0x00, 0xE5, 0xFF, 0x0F);
            typeTag = "Ink";
        } else if (obj->type == ObjectType::Image) {
            strokeColor = BLRgba32(0x00, 0xE6, 0x76, 0x99);
            fillColor   = BLRgba32(0x00, 0xE6, 0x76, 0x0F);
            typeTag = "Img";
        } else {
            strokeColor = BLRgba32(0xFF, 0x91, 0x00, 0x99);
            fillColor   = BLRgba32(0xFF, 0x91, 0x00, 0x0F);
            typeTag = "Box";
        }

        if (debugShowObjectAABB) {
            ctx.set_fill_style(fillColor);
            ctx.fill_rect(sx, sy, sw, sh);
            ctx.set_stroke_style(strokeColor);
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(sx, sy, sw, sh);

            double bracketLen = std::min(6.0, std::min(sw, sh) * 0.3);
            if (bracketLen > 2.0) {
                ctx.set_stroke_width(2.0);
                ctx.stroke_line(sx, sy, sx + bracketLen, sy);
                ctx.stroke_line(sx, sy, sx, sy + bracketLen);
                ctx.stroke_line(sx + sw, sy + sh, sx + sw - bracketLen, sy + sh);
                ctx.stroke_line(sx + sw, sy + sh, sx + sw, sy + sh - bracketLen);
            }

            if (debugShowLabels) {
                char label[64];
                int wMm = static_cast<int>(std::round(box.Width()));
                int hMm = static_cast<int>(std::round(box.Height()));
                std::snprintf(label, sizeof(label), "[%s #%u] %dx%d", typeTag, obj->uid, wMm, hMm);

                double labelW = (std::strlen(label) * 6.5) + 6.0;
                ctx.set_fill_style(BLRgba32(0x10, 0x14, 0x1E, 0xEE));
                ctx.fill_round_rect(sx, sy - 14.0, labelW, 13.0, 2.0);
                ctx.set_stroke_style(strokeColor);
                ctx.set_stroke_width(0.8);
                ctx.stroke_round_rect(sx, sy - 14.0, labelW, 13.0, 2.0);

                SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(sx + 3.0), static_cast<float>(sy - 12.0), label);
            }
        }

        if (debugShowSegmentAABB && obj->type == ObjectType::InkContainer) {
            auto ink = std::static_pointer_cast<InkContainer>(obj);
            if (ink) {
                ctx.set_stroke_style(isHit ? BLRgba32(0xFF, 0x52, 0x52, 0x88) : BLRgba32(0x76, 0xFF, 0x03, 0x55));
                ctx.set_stroke_width(0.75);
                double scale = std::hypot(ink->transform.m00, ink->transform.m01);
                for (const auto& stroke : ink->strokes) {
                    for (const auto& seg : stroke.segments) {
                        BLPoint wp0 = ink->transform.map_point(seg.p0.x, seg.p0.y);
                        BLPoint wp1 = ink->transform.map_point(seg.p1.x, seg.p1.y);
                        double worldR = (static_cast<double>(seg.width) * 0.5) * scale;
                        double sMinX = std::min(wp0.x, wp1.x) - worldR;
                        double sMinY = std::min(wp0.y, wp1.y) - worldR;
                        double sMaxX = std::max(wp0.x, wp1.x) + worldR;
                        double sMaxY = std::max(wp0.y, wp1.y) + worldR;

                        Point2D sc1 = tr.WorldToScreen(sMinX, sMinY);
                        Point2D sc2 = tr.WorldToScreen(sMaxX, sMaxY);
                        double msx = std::min(sc1.x, sc2.x);
                        double msy = std::min(sc1.y, sc2.y);
                        double msw = std::abs(sc2.x - sc1.x);
                        double msh = std::abs(sc2.y - sc1.y);

                        ctx.stroke_rect(msx, msy, msw, msh);
                    }
                }
            }
        }
    }

    if (debugShowQueryAABB && debugCollision.active) {
        Point2D scMin = tr.WorldToScreen(debugCollision.queryBox.minX, debugCollision.queryBox.minY);
        Point2D scMax = tr.WorldToScreen(debugCollision.queryBox.maxX, debugCollision.queryBox.maxY);
        double qx = std::min(scMin.x, scMax.x);
        double qy = std::min(scMin.y, scMax.y);
        double qw = std::abs(scMax.x - scMin.x);
        double qh = std::abs(scMax.y - scMin.y);

        Point2D qCenter = tr.WorldToScreen(debugCollision.queryCenter.x, debugCollision.queryCenter.y);
        double qRadScreen = debugCollision.queryRadius * tr.zoom;

        ctx.set_fill_style(BLRgba32(0xF5, 0x00, 0x57, 0x30));
        ctx.fill_circle(qCenter.x, qCenter.y, qRadScreen);
        ctx.set_stroke_style(BLRgba32(0xF5, 0x00, 0x57, 0xFF));
        ctx.set_stroke_width(2.0);
        ctx.stroke_circle(qCenter.x, qCenter.y, qRadScreen);

        ctx.set_stroke_style(BLRgba32(0xF5, 0x00, 0x57, 0x44));
        ctx.set_stroke_width(0.75);
        ctx.stroke_rect(qx, qy, qw, qh);

        ctx.set_stroke_width(1.0);
        ctx.stroke_line(qCenter.x - 4.0, qCenter.y, qCenter.x + 4.0, qCenter.y);
        ctx.stroke_line(qCenter.x, qCenter.y - 4.0, qCenter.x, qCenter.y + 4.0);

        char qLabel[64];
        std::snprintf(qLabel, sizeof(qLabel), "[QUERY R:%.1f] Cands:%zu Hits:%zu",
                      debugCollision.queryRadius, debugCollision.candidateUids.size(), debugCollision.hitUids.size());
        ctx.set_fill_style(BLRgba32(0x20, 0x00, 0x10, 0xF0));
        double qlW = (std::strlen(qLabel) * 6.5) + 6.0;
        ctx.fill_round_rect(qx, qy - 15.0, qlW, 14.0, 2.0);
        ctx.set_stroke_style(BLRgba32(0xF5, 0x00, 0x57, 0xFF));
        ctx.stroke_round_rect(qx, qy - 15.0, qlW, 14.0, 2.0);
        SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(qx + 3.0), static_cast<float>(qy - 13.0), qLabel);
    }

    Point2D originScreen = tr.WorldToScreen(0.0, 0.0);
    if (originScreen.x >= -150.0 && originScreen.x <= static_cast<double>(viewportW) + 150.0 &&
        originScreen.y >= -150.0 && originScreen.y <= static_cast<double>(viewportH) + 150.0) {
        double ox = originScreen.x;
        double oy = originScreen.y;

        ctx.set_stroke_style(BLRgba32(0xFF, 0xD6, 0x00, 0xCC));
        ctx.set_stroke_width(1.5);
        ctx.stroke_circle(ox, oy, 8.0);
        ctx.stroke_circle(ox, oy, 16.0);
        ctx.set_fill_style(BLRgba32(0xFF, 0xD6, 0x00, 0x33));
        ctx.fill_circle(ox, oy, 4.0);

        ctx.set_stroke_style(BLRgba32(0xFF, 0x17, 0x44, 0xEE));
        ctx.set_stroke_width(2.0);
        ctx.stroke_line(ox, oy, ox + 45.0, oy);

        ctx.set_stroke_style(BLRgba32(0x00, 0xE6, 0x76, 0xEE));
        ctx.set_stroke_width(2.0);
        ctx.stroke_line(ox, oy, ox, oy + 45.0);

        SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(ox + 48.0), static_cast<float>(oy - 4.0), "+X (mm)");
        SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(ox - 6.0), static_cast<float>(oy + 48.0), "+Y (mm)");

        char originLabel[48];
        std::snprintf(originLabel, sizeof(originLabel), "ORIGIN (0,0) mm");
        ctx.set_fill_style(BLRgba32(0x10, 0x14, 0x1E, 0xEE));
        double olW = (std::strlen(originLabel) * 6.5) + 6.0;
        ctx.fill_round_rect(ox + 10.0, oy - 20.0, olW, 14.0, 2.0);
        ctx.set_stroke_style(BLRgba32(0xFF, 0xD6, 0x00, 0xAA));
        ctx.set_stroke_width(0.8);
        ctx.stroke_round_rect(ox + 10.0, oy - 20.0, olW, 14.0, 2.0);
        SelectionGizmo::DrawFallbackText(ctx, static_cast<float>(ox + 13.0), static_cast<float>(oy - 18.0), originLabel);
    }

    float screenMidX = static_cast<float>(viewportW) * 0.5f;
    float screenMidY = static_cast<float>(viewportH) * 0.5f;
    Point2D centerWorld = tr.ScreenToWorld(screenMidX, screenMidY);

    ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0x55));
    ctx.set_stroke_width(1.0);
    ctx.stroke_line(screenMidX - 14.0f, screenMidY, screenMidX + 14.0f, screenMidY);
    ctx.stroke_line(screenMidX, screenMidY - 14.0f, screenMidX, screenMidY + 14.0f);
    ctx.stroke_circle(screenMidX, screenMidY, 3.5);

    char centerLabel[64];
    std::snprintf(centerLabel, sizeof(centerLabel), "Ctr: (%.1f, %.1f) mm", centerWorld.x, centerWorld.y);
    ctx.set_fill_style(BLRgba32(0x10, 0x14, 0x1E, 0x99));
    double clW = (std::strlen(centerLabel) * 6.5) + 6.0;
    ctx.fill_round_rect(screenMidX + 6.0, screenMidY + 6.0, clW, 13.0, 2.0);
    SelectionGizmo::DrawFallbackText(ctx, screenMidX + 9.0f, screenMidY + 7.0f, centerLabel);

    {
        Point2D vpMinWorld = tr.ScreenToWorld(0.0, 0.0);
        Point2D vpMaxWorld = tr.ScreenToWorld(viewportW, viewportH);
        double vpWorldW = vpMaxWorld.x - vpMinWorld.x;
        double vpWorldH = vpMaxWorld.y - vpMinWorld.y;

        const char* modeStr = "Semi-Inf";
        if (tr.infinityMode == CanvasInfinityMode::FullInfinity) modeStr = "Full-Inf";
        else if (tr.infinityMode == CanvasInfinityMode::VerticalScroll) modeStr = "VertScroll";
        else if (tr.infinityMode == CanvasInfinityMode::HorizontalScroll) modeStr = "HorizScroll";

        char line0[96], line1[96], line2[96], line3[96], line4[96], line5[96];
        std::snprintf(line0, sizeof(line0), "DEV[F4] %s | Vis:%zu Hits:%zu",
                      modeStr, visibleObjects.size(), debugCollision.hitUids.size());
        std::snprintf(line1, sizeof(line1), "Zoom: %.1f%% (Scale: %.2f px/mm)",
                      tr.zoom * 100.0, tr.GetEffectiveScale());
        std::snprintf(line2, sizeof(line2), "Pan: (%.1f, %.1f) mm%s",
                      tr.panXMm, tr.panYMm,
                      (tr.infinityMode == CanvasInfinityMode::SemiInfinity && (tr.panXMm == 0.0 || tr.panYMm == 0.0)) ? " [CLAMP]" : "");
        std::snprintf(line3, sizeof(line3), "World Ctr: (%.1f, %.1f) mm",
                      centerWorld.x, centerWorld.y);
        std::snprintf(line4, sizeof(line4), "View Bounds: [%.1f, %.1f] -> [%.1f, %.1f]",
                      vpMinWorld.x, vpMinWorld.y, vpMaxWorld.x, vpMaxWorld.y);
        std::snprintf(line5, sizeof(line5), "View Size: %.1fx%.1f mm (%dx%d px)",
                      vpWorldW, vpWorldH, viewportW, viewportH);

        const char* lines[6] = { line0, line1, line2, line3, line4, line5 };
        float cardW = 325.0f;
        float lineH = 15.0f;
        float cardH = 6 * lineH + 12.0f;
        float cardX = static_cast<float>(viewportW) - cardW - 16.0f;
        float cardY = 16.0f;

        ctx.set_fill_style(BLRgba32(0x0E, 0x12, 0x1C, 0xF2));
        ctx.fill_round_rect(cardX, cardY, cardW, cardH, 5.0f);
        ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0xAA));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(cardX, cardY, cardW, cardH, 5.0f);

        BLRgba32 dotColor = debugCollision.hitUids.empty() ? BLRgba32(0x00, 0xE6, 0x76, 0xFF) : BLRgba32(0xFF, 0x17, 0x44, 0xFF);
        ctx.fill_circle(cardX + 11.0f, cardY + 11.0f, 3.5, dotColor);

        ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0x44));
        ctx.stroke_line(cardX + 6.0f, cardY + lineH + 5.0f, cardX + cardW - 6.0f, cardY + lineH + 5.0f);

        for (int i = 0; i < 6; ++i) {
            float textX = cardX + (i == 0 ? 20.0f : 10.0f);
            float textY = cardY + 5.0f + (i * lineH);
            SelectionGizmo::DrawFallbackText(ctx, textX, textY, lines[i]);
        }
    }

    ctx.restore();
}
