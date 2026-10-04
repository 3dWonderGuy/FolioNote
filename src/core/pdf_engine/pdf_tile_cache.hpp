#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <blend2d/blend2d.h>

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include "core/pdf_engine/pdf_types.hpp"
#include "core/pdf_engine/pdf_text_layer.hpp"

namespace Folio {

/**
 * @brief Fully rendered raster page entry, retaining Blend2D CPU image and GPU GL texture.
 */
struct CachedPdfViewerPage {
    int pageIndex = 0;
    BLImage image;
    GLuint glTexture = 0;
    int pixelW = 0;
    int pixelH = 0;
    double widthMm = 210.0;
    double heightMm = 297.0;
    PdfTextLayer textLayer;
    std::vector<PdfPageLink> links;
    std::vector<TextHighlightSpan> textHighlights;
    bool isLoaded = false;
    bool isInverted = false;

    void DestroyTexture();
};

/**
 * @brief Fast preview thumbnail entry rendered at 32 DPI for document navigation.
 */
struct CachedThumbnail {
    int pageIndex = 0;
    GLuint glTexture = 0;
    int pixelW = 0;
    int pixelH = 0;
    double widthMm = 210.0;
    double heightMm = 297.0;
    bool isLoaded = false;
    bool isInverted = false;

    void DestroyTexture();
};

/**
 * @brief Thread-safe cache managing rendered PDF page tiles, full pages, and navigation thumbnails.
 */
class PdfTileCache {
public:
    std::unordered_map<int, CachedPdfViewerPage> pageCache;
    std::unordered_map<int, CachedThumbnail> thumbnailCache;

    PdfTileCache() = default;
    ~PdfTileCache();

    // Non-copyable, movable
    PdfTileCache(const PdfTileCache&) = delete;
    PdfTileCache& operator=(const PdfTileCache&) = delete;
    PdfTileCache(PdfTileCache&&) noexcept = default;
    PdfTileCache& operator=(PdfTileCache&&) noexcept = default;

    /**
     * @brief Retrieves an existing cached page or rasterizes it via PdfRenderer.
     */
    CachedPdfViewerPage* GetOrLoadPage(
        const std::string& pdfPath,
        int pageIdx,
        const std::unordered_map<int, std::vector<TextHighlightSpan>>& docHighlights,
        double targetDpi = 150.0,
        bool invert = false
    );

    /**
     * @brief Retrieves an existing thumbnail or rasterizes it at 32 DPI.
     */
    CachedThumbnail* GetOrLoadThumbnail(
        const std::string& pdfPath,
        int pageIdx,
        bool invert = false
    );

    /**
     * @brief Releases all GPU textures and purges in-memory CPU images.
     */
    void EvictAll();

    /**
     * @brief Evicts a specific page's GPU and CPU cache.
     */
    void EvictPage(int pageIdx);

    /**
     * @brief Evicts a specific thumbnail's GPU texture.
     */
    void EvictThumbnail(int pageIdx);
};

} // namespace Folio
