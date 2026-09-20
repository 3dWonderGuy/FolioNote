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

FontManager::FontManager() {
    // Attempt to preload standard default fallback font
    const char* defaultCandidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "assets/fonts/Roboto-Medium.ttf",
        "../assets/fonts/Roboto-Medium.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf"
    };

    for (const char* path : defaultCandidates) {
        if (fs::exists(path)) {
            if (m_fallbackFace.create_from_file(path) == BL_SUCCESS) {
                m_fallbackLoaded = true;
                break;
            }
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

#if defined(_WIN32)
    const std::string winFontDir = "C:/Windows/Fonts/";
    if (lowerFamily.find("segoe") != std::string::npos) {
        if (bold && italic) return winFontDir + "segoeuez.ttf"; // Bold Italic
        if (bold)           return winFontDir + "segoeuib.ttf"; // Bold
        if (italic)         return winFontDir + "segoeuii.ttf"; // Italic
        return winFontDir + "segoeui.ttf";                     // Regular
    }
    if (lowerFamily.find("arial") != std::string::npos) {
        if (bold && italic) return winFontDir + "arialbi.ttf";
        if (bold)           return winFontDir + "arialbd.ttf";
        if (italic)         return winFontDir + "ariali.ttf";
        return winFontDir + "arial.ttf";
    }
    if (lowerFamily.find("consolas") != std::string::npos) {
        if (bold && italic) return winFontDir + "consolaz.ttf";
        if (bold)           return winFontDir + "consolab.ttf";
        if (italic)         return winFontDir + "consolai.ttf";
        return winFontDir + "consola.ttf";
    }
    if (lowerFamily.find("times") != std::string::npos) {
        if (bold && italic) return winFontDir + "timesbi.ttf";
        if (bold)           return winFontDir + "timesbd.ttf";
        if (italic)         return winFontDir + "timesi.ttf";
        return winFontDir + "times.ttf";
    }
    if (lowerFamily.find("calibri") != std::string::npos) {
        if (bold && italic) return winFontDir + "calibriz.ttf";
        if (bold)           return winFontDir + "calibrib.ttf";
        if (italic)         return winFontDir + "calibrii.ttf";
        return winFontDir + "calibri.ttf";
    }
#else
    // Linux / POSIX candidate resolution
    const char* linuxCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf"
    };
    for (const char* p : linuxCandidates) {
        if (fs::exists(p)) return p;
    }
#endif

    // Fallback to assets directory if available
    const char* assetCandidates[] = {
        "assets/fonts/Roboto-Regular.ttf",
        "assets/fonts/Roboto-Medium.ttf",
        "../assets/fonts/Roboto-Medium.ttf"
    };
    for (const char* p : assetCandidates) {
        if (fs::exists(p)) return p;
    }

    return "";
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
