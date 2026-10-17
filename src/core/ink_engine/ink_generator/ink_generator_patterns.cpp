#include "core/ink_engine/ink_generator.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

// =============================================================================
// DISCONTINUOUS PATTERN DISPATCHER
// =============================================================================

BLPath InkGenerator::ApplyDiscontinuousPattern(const std::vector<StrokePoint>& pts,
                                               StrokePattern pattern) {
    switch (pattern) {
        case StrokePattern::Dotted:
            return BuildDottedPattern(pts);
        case StrokePattern::Dashed:
            return BuildDashedPattern(pts);
        case StrokePattern::DashDot:
            return BuildDashDotPattern(pts);
        case StrokePattern::TexturedPencil:
            return BuildTexturedPencilPattern(pts);
        case StrokePattern::Solid:
        default:
            return BuildSolidRibbon(pts);
    }
}

// =============================================================================
// 1. DOTTED PATTERN
// =============================================================================

BLPath InkGenerator::BuildDottedPattern(const std::vector<StrokePoint>& pts) {
    BLPath path;
    double nextDotDist = 0.0;
    double distAccum = 0.0;

    for (size_t i = 0; i < pts.size() - 1; ++i) {
        const auto& p0 = pts[i];
        const auto& p1 = pts[i + 1];
        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double segLen = std::hypot(dx, dy);
        if (segLen < 1e-7) continue;

        while (nextDotDist <= distAccum + segLen) {
            double t = (nextDotDist - distAccum) / segLen;
            t = std::clamp(t, 0.0, 1.0);
            double dotX = p0.x + t * dx;
            double dotY = p0.y + t * dy;
            float dotR = p0.r + static_cast<float>(t) * (p1.r - p0.r);
            double r = dotR;
            path.add_circle(BLCircle{dotX, dotY, r});

            double spacing = std::max(0.6, (double)dotR * 4.4);
            nextDotDist += spacing;
        }
        distAccum += segLen;
    }
    if (path.is_empty() && !pts.empty()) {
        double r = pts[0].r;
        path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
    }
    return path;
}

// =============================================================================
// 2. DASHED PATTERN
// =============================================================================

BLPath InkGenerator::BuildDashedPattern(const std::vector<StrokePoint>& pts) {
    BLPath path;
    const size_t N = pts.size();
    std::vector<double> cumDist(N, 0.0);
    for (size_t i = 1; i < N; ++i) {
        cumDist[i] = cumDist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    double totalLen = cumDist.back();
    if (totalLen < 1e-5) {
        double r = pts[0].r;
        path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
        return path;
    }

    float avgR = 0.0f;
    for (const auto& pt : pts) avgR += pt.r;
    avgR /= static_cast<float>(N);

    double dashLen = std::max(1.8, (double)avgR * 8.0);
    double gapLen  = std::max(1.0, (double)avgR * 5.0);
    double cycle   = dashLen + gapLen;

    auto samplePointAt = [&](double d) -> StrokePoint {
        d = std::clamp(d, 0.0, totalLen);
        auto it = std::lower_bound(cumDist.begin(), cumDist.end(), d);
        if (it == cumDist.begin()) return pts.front();
        if (it == cumDist.end()) return pts.back();
        size_t idx = std::distance(cumDist.begin(), it);
        double d0 = cumDist[idx - 1];
        double d1 = cumDist[idx];
        double segL = d1 - d0;
        double t = (segL > 1e-7) ? (d - d0) / segL : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const auto& p0 = pts[idx - 1];
        const auto& p1 = pts[idx];
        StrokePoint res;
        res.x = p0.x + t * (p1.x - p0.x);
        res.y = p0.y + t * (p1.y - p0.y);
        res.r = p0.r + static_cast<float>(t) * (p1.r - p0.r);
        return res;
    };

    for (double s0 = 0.0; s0 < totalLen; s0 += cycle) {
        double s1 = std::min(totalLen, s0 + dashLen);
        if (s1 <= s0) break;

        std::vector<StrokePoint> dashPts;
        dashPts.push_back(samplePointAt(s0));

        for (size_t i = 0; i < N; ++i) {
            if (cumDist[i] > s0 + 1e-5 && cumDist[i] < s1 - 1e-5) {
                dashPts.push_back(pts[i]);
            }
        }
        dashPts.push_back(samplePointAt(s1));

        if (dashPts.size() <= 1 || std::hypot(dashPts.front().x - dashPts.back().x, dashPts.front().y - dashPts.back().y) < 1e-5) {
            double r = dashPts[0].r;
            path.add_circle(BLCircle{dashPts[0].x, dashPts[0].y, r});
        } else {
            BLPath subRibbon = BuildSolidRibbon(dashPts);
            path.add_path(subRibbon);
        }
    }
    return path;
}

// =============================================================================
// 3. DASH-DOT PATTERN
// =============================================================================

BLPath InkGenerator::BuildDashDotPattern(const std::vector<StrokePoint>& pts) {
    BLPath path;
    const size_t N = pts.size();
    std::vector<double> cumDist(N, 0.0);
    for (size_t i = 1; i < N; ++i) {
        cumDist[i] = cumDist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    double totalLen = cumDist.back();
    if (totalLen < 1e-5) {
        double r = pts[0].r;
        path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
        return path;
    }

    float avgR = 0.0f;
    for (const auto& pt : pts) avgR += pt.r;
    avgR /= static_cast<float>(N);

    double dashLen = std::max(2.0, (double)avgR * 8.4);
    double gap1    = std::max(1.0, (double)avgR * 4.0);
    double dotLen  = std::max(0.5, (double)avgR * 2.0);
    double gap2    = std::max(1.0, (double)avgR * 4.0);
    double cycle   = dashLen + gap1 + dotLen + gap2;

    auto samplePointAt = [&](double d) -> StrokePoint {
        d = std::clamp(d, 0.0, totalLen);
        auto it = std::lower_bound(cumDist.begin(), cumDist.end(), d);
        if (it == cumDist.begin()) return pts.front();
        if (it == cumDist.end()) return pts.back();
        size_t idx = std::distance(cumDist.begin(), it);
        double d0 = cumDist[idx - 1];
        double d1 = cumDist[idx];
        double segL = d1 - d0;
        double t = (segL > 1e-7) ? (d - d0) / segL : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const auto& p0 = pts[idx - 1];
        const auto& p1 = pts[idx];
        StrokePoint res;
        res.x = p0.x + t * (p1.x - p0.x);
        res.y = p0.y + t * (p1.y - p0.y);
        res.r = p0.r + static_cast<float>(t) * (p1.r - p0.r);
        return res;
    };

    for (double s0 = 0.0; s0 < totalLen; s0 += cycle) {
        // 1. Dash segment
        double s1 = std::min(totalLen, s0 + dashLen);
        if (s1 > s0) {
            std::vector<StrokePoint> dashPts;
            dashPts.push_back(samplePointAt(s0));
            for (size_t i = 0; i < N; ++i) {
                if (cumDist[i] > s0 + 1e-5 && cumDist[i] < s1 - 1e-5) {
                    dashPts.push_back(pts[i]);
                }
            }
            dashPts.push_back(samplePointAt(s1));
            if (dashPts.size() <= 1 || std::hypot(dashPts.front().x - dashPts.back().x, dashPts.front().y - dashPts.back().y) < 1e-5) {
                double r = dashPts[0].r;
                path.add_circle(BLCircle{dashPts[0].x, dashPts[0].y, r});
            } else {
                BLPath subRibbon = BuildSolidRibbon(dashPts);
                path.add_path(subRibbon);
            }
        }

        // 2. Dot segment
        double dotCenter = s0 + dashLen + gap1 + dotLen * 0.5;
        if (dotCenter <= totalLen) {
            StrokePoint dotPt = samplePointAt(dotCenter);
            double r = dotPt.r * 1.1;
            path.add_circle(BLCircle{dotPt.x, dotPt.y, r});
        }
    }
    return path;
}

// =============================================================================
// 4. TEXTURED PENCIL (Clean Solid-Ribbon Contour + Cached BLPattern Grain Fill)
// =============================================================================

namespace {

// Fast deterministic integer hash on a 128x128 periodic lattice (returns [0.0, 1.0])
inline float HashLattice128(int x, int y, uint32_t seed) noexcept {
    uint32_t ux = static_cast<uint32_t>(x & 127);
    uint32_t uy = static_cast<uint32_t>(y & 127);
    uint32_t h = seed ^ (ux * 374761393U) ^ (uy * 668265263U);
    h = (h ^ (h >> 13)) * 1274126177U;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

// Smooth bilinearly interpolated value noise on a seamless 128x128 torus
inline float SmoothNoise128(float x, float y, uint32_t seed) noexcept {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);

    float n00 = HashLattice128(x0,     y0,     seed);
    float n10 = HashLattice128(x0 + 1, y0,     seed);
    float n01 = HashLattice128(x0,     y0 + 1, seed);
    float n11 = HashLattice128(x0 + 1, y0 + 1, seed);

    float nx0 = n00 + u * (n10 - n00);
    float nx1 = n01 + u * (n11 - n01);
    return nx0 + v * (nx1 - nx0);
}

struct PencilTextureSet {
    uint32_t rgbaKey = 0;
    BLImage  sparseGrainImg; ///< Prominent paper-tooth valleys & lighter charcoal grain (light pressure / outer tilt)
    BLImage  midGrainImg;    ///< Intermediate pore-filling grain (medium pressure / mid tilt gradient)
    BLImage  denseCoreImg;   ///< Concentrated dark graphite with minimal noise (heavy pressure / sharp tip core)
};

const PencilTextureSet& GetOrCreatePencilTextures(BLRgba32 baseColor) {
    constexpr size_t kCacheCapacity = 8;
    thread_local PencilTextureSet s_cache[kCacheCapacity];
    thread_local size_t s_nextSlot = 0;

    uint32_t key = baseColor.value;
    for (size_t i = 0; i < kCacheCapacity; ++i) {
        if (s_cache[i].rgbaKey == key && !s_cache[i].sparseGrainImg.is_empty()) {
            return s_cache[i];
        }
    }

    PencilTextureSet& slot = s_cache[s_nextSlot];
    s_nextSlot = (s_nextSlot + 1) % kCacheCapacity;
    slot.rgbaKey = key;

    constexpr int kSize = 128;
    slot.sparseGrainImg.create(kSize, kSize, BL_FORMAT_PRGB32);
    slot.midGrainImg.create(kSize, kSize, BL_FORMAT_PRGB32);
    slot.denseCoreImg.create(kSize, kSize, BL_FORMAT_PRGB32);

    BLImageData sparseData{}, midData{}, denseData{};
    slot.sparseGrainImg.make_mutable(&sparseData);
    slot.midGrainImg.make_mutable(&midData);
    slot.denseCoreImg.make_mutable(&denseData);

    int baseR = baseColor.r();
    int baseG = baseColor.g();
    int baseB = baseColor.b();
    float baseA = static_cast<float>(baseColor.a()) / 255.0f;

    for (int y = 0; y < kSize; ++y) {
        auto* rowSparse = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(sparseData.pixel_data) + y * sparseData.stride);
        auto* rowMid    = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(midData.pixel_data)    + y * midData.stride);
        auto* rowDense  = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(denseData.pixel_data)  + y * denseData.stride);

        for (int x = 0; x < kSize; ++x) {
            // Multi-scale paper tooth heightfield (seamless 128x128)
            float fine   = HashLattice128(x, y, 101);
            float med    = SmoothNoise128(x * 0.5f,  y * 0.5f,  202);
            float coarse = SmoothNoise128(x * 0.25f, y * 0.25f, 303);
            float tooth  = 0.45f * fine + 0.35f * med + 0.20f * coarse;

            // Graphite mineral color/luminance noise (-14% to +14%)
            float colorNoise = (HashLattice128(x, y, 404) - 0.5f) * 2.0f;

            // 1. SPARSE GRAIN (Light pressure / Outer Tilt Shading):
            float sparseAlpha = 0.0f;
            if (tooth > 0.34f) {
                float norm = (tooth - 0.34f) / 0.66f;
                sparseAlpha = std::clamp(std::pow(norm, 1.10f) * 0.72f, 0.0f, 1.0f) * baseA;
            }
            int sR = std::clamp(static_cast<int>(baseR * (1.10f + 0.14f * colorNoise)), 0, 255);
            int sG = std::clamp(static_cast<int>(baseG * (1.10f + 0.14f * colorNoise)), 0, 255);
            int sB = std::clamp(static_cast<int>(baseB * (1.10f + 0.15f * colorNoise)), 0, 255);
            uint32_t sA = static_cast<uint32_t>(std::round(sparseAlpha * 255.0f));
            rowSparse[x] = (sA << 24) | (((sR * sA) / 255) << 16) | (((sG * sA) / 255) << 8) | ((sB * sA) / 255);

            // 2. MID GRAIN (Intermediate concentration band — stacks over Sparse):
            float midTooth = 0.45f * HashLattice128(x, y, 505) +
                             0.35f * SmoothNoise128(x * 0.5f + 17.0f, y * 0.5f + 31.0f, 606) +
                             0.20f * coarse;
            float midAlpha = 0.0f;
            if (midTooth > 0.24f) {
                float norm = (midTooth - 0.24f) / 0.76f;
                midAlpha = std::clamp(0.18f + norm * 0.52f, 0.0f, 1.0f) * baseA;
            }
            int mR = std::clamp(static_cast<int>(baseR * (0.90f + 0.10f * colorNoise)), 0, 255);
            int mG = std::clamp(static_cast<int>(baseG * (0.90f + 0.10f * colorNoise)), 0, 255);
            int mB = std::clamp(static_cast<int>(baseB * (0.90f + 0.11f * colorNoise)), 0, 255);
            uint32_t mA = static_cast<uint32_t>(std::round(midAlpha * 255.0f));
            rowMid[x] = (mA << 24) | (((mR * mA) / 255) << 16) | (((mG * mA) / 255) << 8) | ((mB * mA) / 255);

            // 3. DENSE CONCENTRATED CORE (Stacks over Sparse + Mid to suppress noise & darken):
            float denseNoise = HashLattice128(x, y, 707);
            float denseAlpha = std::clamp(0.45f + 0.35f * denseNoise, 0.0f, 1.0f) * baseA;
            int dR = std::clamp(static_cast<int>(baseR * (0.68f + 0.06f * colorNoise)), 0, 255);
            int dG = std::clamp(static_cast<int>(baseG * (0.68f + 0.06f * colorNoise)), 0, 255);
            int dB = std::clamp(static_cast<int>(baseB * (0.68f + 0.07f * colorNoise)), 0, 255);
            uint32_t dA = static_cast<uint32_t>(std::round(denseAlpha * 255.0f));
            rowDense[x] = (dA << 24) | (((dR * dA) / 255) << 16) | (((dG * dA) / 255) << 8) | ((dB * dA) / 255);
        }
    }

    return slot;
}

} // namespace

BLPath InkGenerator::BuildTexturedPencilPattern(const std::vector<StrokePoint>& pts) {
    if (pts.empty()) return BLPath{};

    // Build the clean outer envelope contour (including directional tilt offset if tilted)
    std::vector<StrokePoint> outerPts(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& pt = pts[i];
        float tiltMag = std::hypot(pt.tiltX, pt.tiltY);
        double ux = (tiltMag > 1e-4f) ? (pt.tiltX / tiltMag) : 0.0;
        double uy = (tiltMag > 1e-4f) ? (pt.tiltY / tiltMag) : 0.0;

        // Estimate tilt extra radius when tilted past deadzone (0.20 rad)
        float tiltFactor = 1.0f;
        if (tiltMag > 0.20f) {
            float t = std::clamp((tiltMag - 0.20f) / (1.10f - 0.20f), 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t);
            tiltFactor = 1.0f + smoothT * 1.5f;
        }
        float rTip = pt.r / tiltFactor;
        float rTiltExtra = std::max(0.0f, pt.r - rTip);

        outerPts[i] = StrokePoint(pt.x + ux * rTiltExtra, pt.y + uy * rTiltExtra, pt.r);
    }

    return BuildSolidRibbon(outerPts, CapType::Round);
}

void InkGenerator::RenderPencilStroke(BLContext& ctx,
                                      const std::vector<StrokePoint>& pts,
                                      float baseWidthMm,
                                      const BLRgba32& color) {
    if (pts.empty() || color.a() == 0) return;

    const auto& tex = GetOrCreatePencilTextures(color);

    // 1 grain texel = 0.045 mm in world coordinates (~560 DPI paper tooth resolution)
    constexpr double kGrainScaleMm = 0.045;

    const size_t N = pts.size();
    std::vector<StrokePoint> band0(N), band1(N), band2(N), band3(N);

    float nominalTipR = (baseWidthMm > 0.01f) ? (baseWidthMm * 0.5f) : pts[0].r;

    for (size_t i = 0; i < N; ++i) {
        const auto& pt = pts[i];
        float p = std::clamp(pt.pressure, 0.0f, 1.0f);

        float tiltMag = std::hypot(pt.tiltX, pt.tiltY);
        double ux = (tiltMag > 1e-4f) ? (pt.tiltX / tiltMag) : 0.0;
        double uy = (tiltMag > 1e-4f) ? (pt.tiltY / tiltMag) : 0.0;

        float rTip = std::min(pt.r, nominalTipR * (0.90f + 0.12f * p));
        float rTiltExtra = std::max(0.0f, pt.r - rTip);

        // Band 0: Full contact patch (tip + 100% tilt spread)
        float r0 = rTip + rTiltExtra;
        band0[i] = StrokePoint(pt.x + ux * rTiltExtra, pt.y + uy * rTiltExtra, r0);

        // Band 1: Active for p > 0.08 -> covers 96% of tip + 72% of tilt spread
        float p1 = std::clamp((p - 0.08f) / 0.60f, 0.0f, 1.0f);
        float r1 = (rTip * 0.96f + rTiltExtra * 0.72f) * std::pow(p1, 0.40f);
        double shift1 = std::max(0.0f, r1 - rTip * 0.96f);
        band1[i] = StrokePoint(pt.x + ux * shift1, pt.y + uy * shift1, r1);

        // Band 2: Active for p > 0.22 -> covers 88% of tip + 45% of tilt spread
        float p2 = std::clamp((p - 0.22f) / 0.60f, 0.0f, 1.0f);
        float r2 = (rTip * 0.88f + rTiltExtra * 0.45f) * std::pow(p2, 0.50f);
        double shift2 = std::max(0.0f, r2 - rTip * 0.88f);
        band2[i] = StrokePoint(pt.x + ux * shift2, pt.y + uy * shift2, r2);

        // Band 3: Active for p > 0.38 -> covers 76% of tip + 22% of tilt spread
        float p3 = std::clamp((p - 0.38f) / 0.55f, 0.0f, 1.0f);
        float r3 = (rTip * 0.76f + rTiltExtra * 0.22f) * std::pow(p3, 0.60f);
        double shift3 = std::max(0.0f, r3 - rTip * 0.76f);
        band3[i] = StrokePoint(pt.x + ux * shift3, pt.y + uy * shift3, r3);
    }

    BLPath path0 = BuildSolidRibbon(band0, CapType::Round);
    BLPath path1 = BuildSolidRibbon(band1, CapType::Round);
    BLPath path2 = BuildSolidRibbon(band2, CapType::Round);
    BLPath path3 = BuildSolidRibbon(band3, CapType::Round);

    BLPattern patSparse0(tex.sparseGrainImg, BL_EXTEND_MODE_REPEAT);
    patSparse0.set_transform(BLMatrix2D::make_scaling(kGrainScaleMm));

    BLMatrix2D m1 = BLMatrix2D::make_scaling(kGrainScaleMm);
    m1.translate(37.0, 53.0);
    BLPattern patSparse1(tex.sparseGrainImg, BL_EXTEND_MODE_REPEAT);
    patSparse1.set_transform(m1);

    BLPattern patMid(tex.midGrainImg, BL_EXTEND_MODE_REPEAT);
    patMid.set_transform(BLMatrix2D::make_scaling(kGrainScaleMm));

    BLPattern patDense(tex.denseCoreImg, BL_EXTEND_MODE_REPEAT);
    patDense.set_transform(BLMatrix2D::make_scaling(kGrainScaleMm));

    if (!path0.is_empty()) {
        ctx.set_fill_style(patSparse0);
        ctx.fill_path(path0);
    }
    if (!path1.is_empty()) {
        ctx.set_fill_style(patSparse1);
        ctx.fill_path(path1);
    }
    if (!path2.is_empty()) {
        ctx.set_fill_style(patMid);
        ctx.fill_path(path2);
    }
    if (!path3.is_empty()) {
        ctx.set_fill_style(patDense);
        ctx.fill_path(path3);
    }
}


