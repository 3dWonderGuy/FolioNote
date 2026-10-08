#include "core/pdf_engine/pdf_tile_cache.hpp"
#include "core/pdf_engine/pdf_renderer.hpp"
#include "utils/logger.hpp"

namespace Folio {

void CachedPdfViewerPage::DestroyTexture() {
    if (glTexture != 0) {
        glDeleteTextures(1, &glTexture);
        glTexture = 0;
    }
}

void CachedThumbnail::DestroyTexture() {
    if (glTexture != 0) {
        glDeleteTextures(1, &glTexture);
        glTexture = 0;
    }
}

PdfTileCache::~PdfTileCache() {
    EvictAll();
}

CachedPdfViewerPage* PdfTileCache::GetOrLoadPage(
    const std::string& pdfPath,
    int pageIdx,
    const std::unordered_map<int, std::vector<TextHighlightSpan>>& docHighlights,
    double targetDpi,
    bool invert
) {
    auto it = pageCache.find(pageIdx);
    if (it != pageCache.end() && it->second.isLoaded && it->second.isInverted == invert) {
        return &it->second;
    }

    if (it != pageCache.end()) {
        it->second.DestroyTexture();
        pageCache.erase(it);
    }

    auto res = PdfRenderer::RenderPage(pdfPath, pageIdx, targetDpi, invert);
    if (!res.success || res.image.is_empty()) {
        return nullptr;
    }

    CachedPdfViewerPage entry;
    entry.pageIndex = pageIdx;
    entry.image = res.image;
    entry.pixelW = res.pixelWidth;
    entry.pixelH = res.pixelHeight;
    entry.widthMm = res.widthMm;
    entry.heightMm = res.heightMm;
    entry.links = std::move(res.links);
    entry.isInverted = invert;

    // Populate persistent text highlights for this page index
    auto hlIt = docHighlights.find(pageIdx);
    if (hlIt != docHighlights.end()) {
        entry.textHighlights = hlIt->second;
    }

    // Generate OpenGL texture for fast blitting
    glGenTextures(1, &entry.glTexture);
    glBindTexture(GL_TEXTURE_2D, entry.glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    BLImageData imgData;
    if (entry.image.get_data(&imgData) == BL_SUCCESS) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
#if defined(__ANDROID__)
        // OpenGL ES 3.0 does not expose GL_BGRA or GL_UNSIGNED_INT_8_8_8_8_REV in core headers.
        // Blend2D BL_FORMAT_PRGB32 layout on little-endian is [B, G, R, A] in memory.
        // We upload as GL_RGBA / GL_UNSIGNED_BYTE and configure hardware texture swizzling
        // (mapping texture B to Red and texture R to Blue) to eliminate any CPU-side channel swap overhead.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, entry.pixelW, entry.pixelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, imgData.pixel_data);
#else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, entry.pixelW, entry.pixelH, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, imgData.pixel_data);
#endif
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    // Load text layer for interaction and selection
    PdfRenderer::LoadTextLayer(pdfPath, pageIdx, entry.textLayer);
    entry.isLoaded = true;

    pageCache[pageIdx] = std::move(entry);
    return &pageCache[pageIdx];
}

CachedThumbnail* PdfTileCache::GetOrLoadThumbnail(
    const std::string& pdfPath,
    int pageIdx,
    bool invert
) {
    auto it = thumbnailCache.find(pageIdx);
    if (it != thumbnailCache.end() && it->second.isLoaded && it->second.isInverted == invert) {
        return &it->second;
    }

    if (it != thumbnailCache.end()) {
        it->second.DestroyTexture();
        thumbnailCache.erase(it);
    }

    // Render at 32 DPI for fast, lightweight previews
    auto res = PdfRenderer::RenderPage(pdfPath, pageIdx, 32.0, invert);
    if (!res.success || res.image.is_empty()) {
        return nullptr;
    }

    CachedThumbnail entry;
    entry.pageIndex = pageIdx;
    entry.pixelW = res.pixelWidth;
    entry.pixelH = res.pixelHeight;
    entry.widthMm = res.widthMm;
    entry.heightMm = res.heightMm;
    entry.isInverted = invert;

    glGenTextures(1, &entry.glTexture);
    glBindTexture(GL_TEXTURE_2D, entry.glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    BLImageData imgData;
    if (res.image.get_data(&imgData) == BL_SUCCESS) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
#if defined(__ANDROID__)
        // OpenGL ES 3.0 texture swizzling for Blend2D PRGB32 BGRA data
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, entry.pixelW, entry.pixelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, imgData.pixel_data);
#else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, entry.pixelW, entry.pixelH, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, imgData.pixel_data);
#endif
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    entry.isLoaded = true;

    thumbnailCache[pageIdx] = std::move(entry);
    return &thumbnailCache[pageIdx];
}

void PdfTileCache::EvictAll() {
    for (auto& [idx, p] : pageCache) {
        p.DestroyTexture();
    }
    pageCache.clear();

    for (auto& [idx, t] : thumbnailCache) {
        t.DestroyTexture();
    }
    thumbnailCache.clear();
}

void PdfTileCache::EvictPage(int pageIdx) {
    auto it = pageCache.find(pageIdx);
    if (it != pageCache.end()) {
        it->second.DestroyTexture();
        pageCache.erase(it);
    }
}

void PdfTileCache::EvictThumbnail(int pageIdx) {
    auto it = thumbnailCache.find(pageIdx);
    if (it != thumbnailCache.end()) {
        it->second.DestroyTexture();
        thumbnailCache.erase(it);
    }
}

} // namespace Folio
