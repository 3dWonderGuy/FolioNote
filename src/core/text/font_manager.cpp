/**
 * @file font_manager.cpp
 * @brief Implementation of the Blend2D FontManager for vector typography resolution and metrics.
 *
 * GENERAL WORKING PROCESS:
 * ------------------------
 * 1. Request font via GetFont(family, sizePt, bold, italic).
 * 2. Check if the (family, sizePt, bold, italic) combination is already cached in m_fontCache.
 *    If found, return immediately without locking or parsing overhead.
 * 3. If uncached, look up the file path on disk matching family + style:
 *    - Windows: C:/Windows/Fonts/ (segoeui.ttf, segoeuib.ttf, arial.ttf, etc.)
 *    - Linux: /usr/share/fonts/
 *    - Local fallback: assets/fonts/
 * 4. Load the font face via BLFontFace::create_from_file. Cache the BLFontFace by path in m_faceCache
 *    so multiple font sizes reuse the same underlying face descriptor.
 * 5. Instantiate BLFont::create_from_face(face, sizePt), cache, and return.
 *
 * MATHEMATICAL FOUNDATIONS:
 * -------------------------
 * - Typographical Point to Metric conversion:
 *     1 pt = 1/72 inch.
 *     Blend2D accepts font size in user-space points / coordinates directly.
 * - Advance Width:
 *     advanceX = sum_{i} glyph_advance_x(g_i)
 * - Line Spacing:
 *     LineHeight = ascent + descent + lineGap
 */

#include "core/text/font_manager.hpp"
#include <filesystem>
#include <algorithm>

namespace Folio {

namespace fs = std::filesystem;

FontManager& FontManager::Instance() {
    static FontManager s_instance;
    return s_instance;
}

namespace {
    std::string FindFirstExisting(const std::vector<std::string>& candidates) {
        std::error_code ec;
        const char* home = std::getenv("HOME");
        for (const auto& path : candidates) {
            std::string resolved = path;
            if (!resolved.empty() && resolved[0] == '~' && home) {
                resolved = std::string(home) + resolved.substr(1);
            }
            if (fs::exists(resolved, ec) && !ec) {
                return resolved;
            }
        }
        return "";
    }
}

FontManager::FontManager() {
    const std::vector<std::string> defaultCandidates = {
        "assets/fonts/Roboto-Medium.ttf",
        "../assets/fonts/Roboto-Medium.ttf",
        "../../assets/fonts/Roboto-Medium.ttf",
        "bin/assets/fonts/Roboto-Medium.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/inter/Inter-Regular.ttf",
        "/usr/share/fonts/inter/Inter-Regular.otf",
        "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/system/fonts/Roboto-Regular.ttf"
    };

    std::string best = FindFirstExisting(defaultCandidates);
    if (!best.empty()) {
        if (m_fallbackFace.create_from_file(best.c_str()) == BL_SUCCESS) {
            m_fallbackLoaded = true;
        }
    }
}

/**
 * @brief Resolves physical font file path from family name and style flags.
 *
 * @param family Family name (e.g., "Segoe UI", "Arial", "Roboto", "Consolas")
 * @param bold Whether bold weight is requested
 * @param italic Whether italic slant is requested
 * @return std::string Path to file on disk or empty string if not found
 */
std::string FontManager::ResolveFontPath(const std::string& family, bool bold, bool italic) {
    std::string lowerFamily = family;
    std::transform(lowerFamily.begin(), lowerFamily.end(), lowerFamily.begin(), ::tolower);

    // 1. Segoe UI (Windows standard, or bundled in assets/user fonts on Linux)
    if (lowerFamily.find("segoe") != std::string::npos || lowerFamily.empty() || lowerFamily == "default") {
        if (bold && italic) {
            std::string p = FindFirstExisting({
                "assets/fonts/segoeuiz.ttf", "../assets/fonts/segoeuiz.ttf", "../../assets/fonts/segoeuiz.ttf",
                "bin/assets/fonts/segoeuiz.ttf", "~/.local/share/fonts/segoeuiz.ttf", "~/.fonts/segoeuiz.ttf",
                "assets/fonts/seguisbi.ttf", "~/.local/share/fonts/seguisbi.ttf",
                "C:/Windows/Fonts/segoeuez.ttf", "C:/Windows/Fonts/segoeuiz.ttf"
            });
            if (!p.empty()) return p;
        }
        if (bold) {
            std::string p = FindFirstExisting({
                "assets/fonts/segoeuib.ttf", "../assets/fonts/segoeuib.ttf", "../../assets/fonts/segoeuib.ttf",
                "bin/assets/fonts/segoeuib.ttf", "~/.local/share/fonts/segoeuib.ttf", "~/.fonts/segoeuib.ttf",
                "assets/fonts/seguisb.ttf", "~/.local/share/fonts/seguisb.ttf",
                "C:/Windows/Fonts/segoeuib.ttf"
            });
            if (!p.empty()) return p;
        }
        if (italic) {
            std::string p = FindFirstExisting({
                "assets/fonts/segoeuii.ttf", "../assets/fonts/segoeuii.ttf", "../../assets/fonts/segoeuii.ttf",
                "bin/assets/fonts/segoeuii.ttf", "~/.local/share/fonts/segoeuii.ttf", "~/.fonts/segoeuii.ttf",
                "C:/Windows/Fonts/segoeuii.ttf"
            });
            if (!p.empty()) return p;
        }
        std::string p = FindFirstExisting({
            "assets/fonts/segoeui.ttf", "../assets/fonts/segoeui.ttf", "../../assets/fonts/segoeui.ttf",
            "bin/assets/fonts/segoeui.ttf", "~/.local/share/fonts/segoeui.ttf", "~/.fonts/segoeui.ttf",
            "C:/Windows/Fonts/segoeui.ttf"
        });
        if (!p.empty()) return p;
    }

    // 2. Arial / Sans-Serif / Helvetica
    if (lowerFamily.find("arial") != std::string::npos || lowerFamily.find("sans") != std::string::npos || lowerFamily.find("helvetica") != std::string::npos) {
        if (bold && italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/arialbi.ttf",
                "/usr/share/fonts/liberation-sans-fonts/LiberationSans-BoldItalic.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf",
                "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-BoldOblique.ttf"
            });
            if (!p.empty()) return p;
        }
        if (bold) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/arialbd.ttf",
                "/usr/share/fonts/inter/Inter-Bold.ttf",
                "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
                "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"
            });
            if (!p.empty()) return p;
        }
        if (italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/ariali.ttf",
                "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Italic.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf",
                "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Oblique.ttf"
            });
            if (!p.empty()) return p;
        }
        std::string p = FindFirstExisting({
            "C:/Windows/Fonts/arial.ttf",
            "/usr/share/fonts/inter/Inter-Regular.ttf",
            "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"
        });
        if (!p.empty()) return p;
    }

    // 3. Consolas / Monospace / Courier
    if (lowerFamily.find("consola") != std::string::npos || lowerFamily.find("mono") != std::string::npos || lowerFamily.find("courier") != std::string::npos) {
        if (bold && italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/consolaz.ttf",
                "/usr/share/fonts/liberation-mono-fonts/LiberationMono-BoldItalic.ttf",
                "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-BoldOblique.ttf"
            });
            if (!p.empty()) return p;
        }
        if (bold) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/consolab.ttf",
                "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Bold.ttf",
                "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Bold.ttf"
            });
            if (!p.empty()) return p;
        }
        if (italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/consolai.ttf",
                "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Italic.ttf",
                "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Oblique.ttf"
            });
            if (!p.empty()) return p;
        }
        std::string p = FindFirstExisting({
            "C:/Windows/Fonts/consola.ttf",
            "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
            "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf"
        });
        if (!p.empty()) return p;
    }

    // 4. Times / Serif
    if (lowerFamily.find("times") != std::string::npos || lowerFamily.find("serif") != std::string::npos) {
        if (bold && italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/timesbi.ttf",
                "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-BoldItalic.ttf",
                "/usr/share/fonts/dejavu-serif-fonts/DejaVuSerif-BoldItalic.ttf"
            });
            if (!p.empty()) return p;
        }
        if (bold) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/timesbd.ttf",
                "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Bold.ttf",
                "/usr/share/fonts/dejavu-serif-fonts/DejaVuSerif-Bold.ttf"
            });
            if (!p.empty()) return p;
        }
        if (italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/timesi.ttf",
                "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Italic.ttf",
                "/usr/share/fonts/dejavu-serif-fonts/DejaVuSerif-Italic.ttf"
            });
            if (!p.empty()) return p;
        }
        std::string p = FindFirstExisting({
            "C:/Windows/Fonts/times.ttf",
            "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Regular.ttf",
            "/usr/share/fonts/dejavu-serif-fonts/DejaVuSerif.ttf"
        });
        if (!p.empty()) return p;
    }

    // 5. Calibri
    if (lowerFamily.find("calibri") != std::string::npos) {
        if (bold && italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/calibriz.ttf",
                "/usr/share/fonts/google-carlito-fonts/Carlito-BoldItalic.ttf"
            });
            if (!p.empty()) return p;
        }
        if (bold) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/calibrib.ttf",
                "/usr/share/fonts/google-carlito-fonts/Carlito-Bold.ttf"
            });
            if (!p.empty()) return p;
        }
        if (italic) {
            std::string p = FindFirstExisting({
                "C:/Windows/Fonts/calibrii.ttf",
                "/usr/share/fonts/google-carlito-fonts/Carlito-Italic.ttf"
            });
            if (!p.empty()) return p;
        }
        std::string p = FindFirstExisting({
            "C:/Windows/Fonts/calibri.ttf",
            "/usr/share/fonts/google-carlito-fonts/Carlito-Regular.ttf"
        });
        if (!p.empty()) return p;
    }

    // 6. Generic system fallback
    std::string fallback = FindFirstExisting({
        "assets/fonts/segoeui.ttf",
        "../assets/fonts/segoeui.ttf",
        "bin/assets/fonts/segoeui.ttf",
        "~/.local/share/fonts/segoeui.ttf",
        "assets/fonts/Roboto-Medium.ttf",
        "assets/fonts/Roboto-Regular.ttf",
        "../assets/fonts/Roboto-Medium.ttf",
        "/usr/share/fonts/inter/Inter-Regular.ttf",
        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf"
    });

    return fallback;
}

BLFontFace FontManager::LoadFontFace(const std::string& filePath) {
    auto it = m_faceCache.find(filePath);
    if (it != m_faceCache.end()) {
        return it->second;
    }

    BLFontFace face;
    if (face.create_from_file(filePath.c_str()) == BL_SUCCESS) {
        m_faceCache[filePath] = face;
        return face;
    }

    return m_fallbackFace;
}

BLFont FontManager::GetFont(const std::string& family, float sizePt, bool bold, bool italic) {
    std::lock_guard<std::mutex> lock(m_mutex);

    FontKey key{family, sizePt, bold, italic};
    auto it = m_fontCache.find(key);
    if (it != m_fontCache.end()) {
        return it->second;
    }

    std::string path = ResolveFontPath(family, bold, italic);
    BLFontFace face;
    if (!path.empty() && fs::exists(path)) {
        face = LoadFontFace(path);
    } else if (m_fallbackLoaded) {
        face = m_fallbackFace;
    }

    BLFont font;
    if (face.is_valid()) {
        font.create_from_face(face, sizePt);
    }

    m_fontCache[key] = font;
    return font;
}

BLFontMetrics FontManager::GetMetrics(const BLFont& font) {
    if (font.is_valid()) {
        return font.metrics();
    }
    return BLFontMetrics{};
}

double FontManager::MeasureTextWidth(const BLFont& font, const std::string& text) {
    if (!font.is_valid() || text.empty()) {
        return 0.0;
    }

    BLGlyphBuffer gb;
    gb.set_utf8_text(text.data(), text.size());
    
    BLTextMetrics tm;
    if (font.get_text_metrics(gb, tm) == BL_SUCCESS) {
        return tm.advance.x;
    }

    // Fallback heuristic: 0.55 * sizePt per character if shaping fails
    const auto& fm = font.metrics();
    return text.size() * (fm.ascent * 0.55);
}

void FontManager::ClearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fontCache.clear();
    m_faceCache.clear();
}

} // namespace Folio
