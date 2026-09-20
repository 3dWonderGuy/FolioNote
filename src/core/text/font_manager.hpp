#pragma once
/**
 * @file font_manager.hpp
 * @brief Thread-safe Blend2D vector font resolution, caching, and measurement service.
 *
 * Provides cross-platform font resolution for Segoe UI, Roboto, Arial, Consolas, and
 * fallbacks. Caches BLFontFace and BLFont instances by family, size, and styling
 * attributes (bold/italic) to ensure high-performance text layout and rendering
 * without redundant disk I/O or font parsing.
 *
 * Scalability:
 *  - Supports custom font directories (assets/fonts) and system fonts (Windows/Linux/Android).
 *  - Decoupled from UI toolkits (pure Blend2D).
 */

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <blend2d/blend2d.h>

namespace Folio {

/**
 * @struct FontKey
 * @brief Hashable cache key identifying a unique font configuration.
 */
struct FontKey {
    std::string family;
    float sizePt = 16.0f;
    bool bold = false;
    bool italic = false;

    bool operator==(const FontKey& o) const noexcept {
        return sizePt == o.sizePt && bold == o.bold && italic == o.italic && family == o.family;
    }
};

struct FontKeyHash {
    size_t operator()(const FontKey& k) const noexcept {
        size_t h1 = std::hash<std::string>{}(k.family);
        size_t h2 = std::hash<float>{}(k.sizePt);
        size_t h3 = (k.bold ? 1 : 0) | (k.italic ? 2 : 0);
        return h1 ^ (h2 << 1) ^ (h3 << 7);
    }
};

/**
 * @class FontManager
 * @brief Singleton/service managing Blend2D font faces and font instantiation.
 */
class FontManager {
public:
    static FontManager& Instance();

    /**
     * @brief Resolves and instantiates a Blend2D font matching the specified criteria.
     *
     * @param family  Font family name (e.g., "Segoe UI", "Arial", "Roboto", "Consolas").
     * @param sizePt  Font size in typographical points (1 pt = 1/72 inch).
     * @param bold    True to request bold face weight.
     * @param italic  True to request italic slant.
     * @return BLFont Configured Blend2D font instance (falls back to default system font if unavailable).
     */
    BLFont GetFont(const std::string& family, float sizePt, bool bold = false, bool italic = false);

    /**
     * @brief Computes typographical font metrics (ascent, descent, lineGap) in world points.
     *
     * @param font Blend2D font to measure.
     * @return BLFontMetrics Font metrics structure.
     */
    BLFontMetrics GetMetrics(const BLFont& font);

    /**
     * @brief Measures the horizontal advance width of a UTF-8 string using the specified font.
     *
     * @param font Blend2D font.
     * @param text UTF-8 encoded text string.
     * @return double Total advance width in typographic points.
     */
    double MeasureTextWidth(const BLFont& font, const std::string& text);

    /**
     * @brief Clears all cached font faces and instantiated fonts.
     */
    void ClearCache();

private:
    FontManager();
    ~FontManager() = default;

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    std::string ResolveFontPath(const std::string& family, bool bold, bool italic);
    BLFontFace LoadFontFace(const std::string& filePath);

    std::mutex m_mutex;
    std::unordered_map<std::string, BLFontFace> m_faceCache;
    std::unordered_map<FontKey, BLFont, FontKeyHash> m_fontCache;
    BLFontFace m_fallbackFace;
    bool m_fallbackLoaded = false;
};

} // namespace Folio
