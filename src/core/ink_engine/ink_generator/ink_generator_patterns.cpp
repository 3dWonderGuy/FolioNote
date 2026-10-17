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
// 4. TEXTURED PENCIL (Clean Solid-Ribbon Contour + High-Res BLPattern Grain Fill)
// =============================================================================

namespace {

// Fast deterministic integer hash on a 256x256 periodic lattice (returns [0.0, 1.0])
inline float HashLattice256(int x, int y, uint32_t seed) noexcept {
    uint32_t ux = static_cast<uint32_t>(x & 255);
    uint32_t uy = static_cast<uint32_t>(y & 255);
    uint32_t h = seed ^ (ux * 374761393U) ^ (uy * 668265263U);
    h = (h ^ (h >> 13)) * 1274126177U;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

// Smooth bilinearly interpolated value noise on a seamless 256x256 torus
inline float SmoothNoise256(float x, float y, uint32_t seed) noexcept {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);

    float n00 = HashLattice256(x0,     y0,     seed);
    float n10 = HashLattice256(x0 + 1, y0,     seed);
    float n01 = HashLattice256(x0,     y0 + 1, seed);
    float n11 = HashLattice256(x0 + 1, y0 + 1, seed);

    float nx0 = n00 + u * (n10 - n00);
    float nx1 = n01 + u * (n11 - n01);
    return nx0 + v * (nx1 - nx0);
}

struct PencilTextureSet {
    uint32_t rgbaKey = 0;
    BLImage  tiltShadeImg; ///< Soft grating grain for wide tilted side-shading envelope
    BLImage  baseGrainImg; ///< Dark, crisp 2B graphite micro-grain for initial & normal strokes
    BLImage  darkCoreImg;  ///< Concentrated deep graphite layer that fills micro-pores under firm pressure
};

const PencilTextureSet& GetOrCreatePencilTextures(BLRgba32 baseColor) {
    constexpr size_t kCacheCapacity = 8;
    thread_local PencilTextureSet s_cache[kCacheCapacity];
    thread_local size_t s_nextSlot = 0;

    uint32_t key = baseColor.value;
    for (size_t i = 0; i < kCacheCapacity; ++i) {
        if (s_cache[i].rgbaKey == key && !s_cache[i].baseGrainImg.is_empty()) {
            return s_cache[i];
        }
    }

    PencilTextureSet& slot = s_cache[s_nextSlot];
    s_nextSlot = (s_nextSlot + 1) % kCacheCapacity;
    slot.rgbaKey = key;

    constexpr int kSize = 256;
    slot.tiltShadeImg.create(kSize, kSize, BL_FORMAT_PRGB32);
    slot.baseGrainImg.create(kSize, kSize, BL_FORMAT_PRGB32);
    slot.darkCoreImg.create(kSize, kSize, BL_FORMAT_PRGB32);

    BLImageData shadeData{}, baseData{}, darkData{};
    slot.tiltShadeImg.make_mutable(&shadeData);
    slot.baseGrainImg.make_mutable(&baseData);
    slot.darkCoreImg.make_mutable(&darkData);

    int baseR = baseColor.r();
    int baseG = baseColor.g();
    int baseB = baseColor.b();
    float baseA = static_cast<float>(baseColor.a()) / 255.0f;

    for (int y = 0; y < kSize; ++y) {
        auto* rowShade = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(shadeData.pixel_data) + y * shadeData.stride);
        auto* rowBase  = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(baseData.pixel_data)  + y * baseData.stride);
        auto* rowDark  = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(darkData.pixel_data)  + y * darkData.stride);

        for (int x = 0; x < kSize; ++x) {
            // High-frequency micro-crystalline paper tooth heightfield (256x256 seamless)
            float fine  = HashLattice256(x, y, 101);
            float micro = SmoothNoise256(x * 0.75f, y * 0.75f, 202);
            float tooth = 0.72f * fine + 0.28f * micro;

            // Subtle graphite mineral luminance sheen (-10% to +10%)
            float colorNoise = (HashLattice256(x, y, 404) - 0.5f) * 2.0f;

            // 1. TILT SHADING GRAIN (Grating gradient when stylus is tilted on its side):
            float shadeAlpha = 0.0f;
            if (tooth > 0.26f) {
                float norm = (tooth - 0.26f) / 0.74f;
                shadeAlpha = std::clamp(0.25f + norm * 0.58f, 0.0f, 1.0f) * baseA;
            }
            int shR = std::clamp(static_cast<int>(baseR * (0.85f + 0.12f * colorNoise)), 0, 255);
            int shG = std::clamp(static_cast<int>(baseG * (0.85f + 0.12f * colorNoise)), 0, 255);
            int shB = std::clamp(static_cast<int>(baseB * (0.85f + 0.13f * colorNoise)), 0, 255);
            uint32_t shA = static_cast<uint32_t>(std::round(shadeAlpha * 255.0f));
            rowShade[x] = (shA << 24) | (((shR * shA) / 255) << 16) | (((shG * shA) / 255) << 8) | ((shB * shA) / 255);

            // 2. BASE 2B GRAPHITE GRAIN (Dark, crisp initial lines with fine micro-tooth translucency):
            //    88% of texels form dense dark graphite ridges ([0.78..0.98] alpha);
            //    12% form tiny translucent paper-tooth micro-pores ([0.42..0.78] alpha).
            float baseAlpha = 0.0f;
            if (tooth > 0.18f) {
                float norm = (tooth - 0.18f) / 0.82f;
                baseAlpha = std::clamp(0.78f + norm * 0.20f, 0.0f, 1.0f) * baseA;
            } else {
                float pore = tooth / 0.18f;
                baseAlpha = std::clamp(0.42f + pore * 0.36f, 0.0f, 1.0f) * baseA;
            }
            int bR = std::clamp(static_cast<int>(baseR * (0.72f + 0.10f * colorNoise)), 0, 255);
            int bG = std::clamp(static_cast<int>(baseG * (0.72f + 0.10f * colorNoise)), 0, 255);
            int bB = std::clamp(static_cast<int>(baseB * (0.72f + 0.11f * colorNoise)), 0, 255);
            uint32_t bA = static_cast<uint32_t>(std::round(baseAlpha * 255.0f));
            rowBase[x] = (bA << 24) | (((bR * bA) / 255) << 16) | (((bG * bA) / 255) << 8) | ((bB * bA) / 255);

            // 3. CONCENTRATED DARK GRAPHITE CORE (Fills micro-pores and deepens graphite under firm pressure):
            float denseNoise = HashLattice256(x, y, 707);
            float darkAlpha = std::clamp(0.62f + 0.34f * denseNoise, 0.0f, 1.0f) * baseA;
            int dR = std::clamp(static_cast<int>(baseR * (0.48f + 0.05f * colorNoise)), 0, 255);
            int dG = std::clamp(static_cast<int>(baseG * (0.48f + 0.05f * colorNoise)), 0, 255);
            int dB = std::clamp(static_cast<int>(baseB * (0.48f + 0.06f * colorNoise)), 0, 255);
            uint32_t dA = static_cast<uint32_t>(std::round(darkAlpha * 255.0f));
            rowDark[x] = (dA << 24) | (((dR * dA) / 255) << 16) | (((dG * dA) / 255) << 8) | ((dB * dA) / 255);
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

        float tiltFactor = 1.0f;
        if (tiltMag > 0.22f) {
            float t = std::clamp((tiltMag - 0.22f) / (1.05f - 0.22f), 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t);
            tiltFactor = 1.0f + smoothT * 1.8f;
        }
        float rTip = pt.r / tiltFactor;
        float rTiltExtra = std::max(0.0f, pt.r - rTip);

        outerPts[i] = StrokePoint(pt.x + ux * rTiltExtra, pt.y + uy * rTiltExtra, pt.r, pt.pressure, pt.tiltX, pt.tiltY);
    }

    return BuildSolidRibbon(outerPts, CapType::Round);
}

void InkGenerator::RenderPencilStroke(BLContext& ctx,
                                      const std::vector<StrokePoint>& pts,
                                      float baseWidthMm,
                                      const BLRgba32& color,
                                      const BLPath* prebuiltOutline) {
    if (pts.empty() || color.a() == 0) return;

    const auto& tex = GetOrCreatePencilTextures(color);

    // 1 grain texel = 0.022 mm in world coordinates (~1150 DPI micro-grain resolution)
    constexpr double kGrainScaleMm = 0.022;

    BLPattern patShade(tex.tiltShadeImg, BL_EXTEND_MODE_REPEAT);
    patShade.set_transform(BLMatrix2D::make_scaling(kGrainScaleMm));

    BLPattern patBase(tex.baseGrainImg, BL_EXTEND_MODE_REPEAT);
    patBase.set_transform(BLMatrix2D::make_scaling(kGrainScaleMm));

    BLPattern patDark(tex.darkCoreImg, BL_EXTEND_MODE_REPEAT);
    patDark.set_transform(BLMatrix2D::make_scaling(kGrainScaleMm));

    // Reuse pre-built stroke outline when available (O(1) zero-allocation fast path!)
    BLPath localOutline;
    const BLPath* envelopePath = prebuiltOutline;
    if (!envelopePath || envelopePath->is_empty()) {
        localOutline = BuildTexturedPencilPattern(pts);
        envelopePath = &localOutline;
    }
    if (envelopePath->is_empty()) return;

    // Sample average pressure and check whether stylus tilt shading is active
    const size_t N = pts.size();
    float nominalTipR = (baseWidthMm > 0.01f) ? (baseWidthMm * 0.5f) : pts[0].r;
    float sumPressure = 0.0f;
    float maxTiltExtra = 0.0f;

    // Sample up to 64 evenly-spaced points so stats remain O(1) even on 10,000-point strokes
    const size_t step = std::max<size_t>(1, N / 64);
    size_t count = 0;
    for (size_t i = 0; i < N; i += step) {
        const auto& pt = pts[i];
        float p = std::clamp(pt.pressure, 0.0f, 1.0f);
        sumPressure += p;
        float rTip = std::min(pt.r, nominalTipR * (0.90f + 0.12f * p));
        maxTiltExtra = std::max(maxTiltExtra, pt.r - rTip);
        ++count;
    }
    float avgPressure = (count > 0) ? (sumPressure / static_cast<float>(count)) : 0.65f;

    if (maxTiltExtra > 0.025f) {
        // TILTED SIDE-SHADING MODE:
        // 1. Wide tilted envelope gets the grating shading pattern
        ctx.set_fill_style(patShade);
        ctx.fill_path(*envelopePath);

        // 2. If pressing harder while tilted, darken the shading envelope proportionally
        if (avgPressure > 0.25f) {
            ctx.save();
            ctx.set_fill_alpha(std::clamp((avgPressure - 0.25f) * 0.85f, 0.0f, 0.75f));
            ctx.set_fill_style(patBase);
            ctx.fill_path(*envelopePath);
            ctx.restore();
        }

        // 3. Crisp dark leading tip along the un-shifted stylus contact line
        std::vector<StrokePoint> tipPts(N);
        for (size_t i = 0; i < N; ++i) {
            float p = std::clamp(pts[i].pressure, 0.0f, 1.0f);
            float rTip = std::min(pts[i].r, nominalTipR * (0.90f + 0.12f * p));
            tipPts[i] = StrokePoint(pts[i].x, pts[i].y, rTip);
        }
        BLPath tipPath = BuildSolidRibbon(tipPts, CapType::Round);
        if (!tipPath.is_empty()) {
            ctx.set_fill_style(patBase);
            ctx.fill_path(tipPath);
        }
    } else {
        // STANDARD UPRIGHT PENCIL MODE (Zero concentric bands, zero geometry rebuilds):
        // 1. Rich dark 2B micro-grain across the entire smooth stroke contour
        ctx.set_fill_style(patBase);
        ctx.fill_path(*envelopePath);

        // 2. Pressure concentration pass over the exact same contour (no inner step rings!)
        float darkBoost = std::clamp((avgPressure - 0.15f) / 0.70f, 0.15f, 0.92f);
        ctx.save();
        ctx.set_fill_alpha(darkBoost);
        ctx.set_fill_style(patDark);
        ctx.fill_path(*envelopePath);
        ctx.restore();
    }
}



