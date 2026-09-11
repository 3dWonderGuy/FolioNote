#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <blend2d/blend2d.h>
#include "utils/logger.hpp"

#if __has_include(<fpdfview.h>)
#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>
#define FOLIO_HAS_PDFIUM 1
#endif

#include "core/render/pdf_text_layer.hpp"

namespace Folio {

struct PdfPageRenderResult {
    bool success = false;
    BLImage image;
    double widthMm = 210.0;
    double heightMm = 297.0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    std::string errorMessage;
};

class PdfRenderer {
public:
    static void InitializeLibrary() {
#if defined(FOLIO_HAS_PDFIUM)
        static bool s_initialized = false;
        if (!s_initialized) {
            FPDF_LIBRARY_CONFIG config;
            config.version = 2;
            config.m_pUserFontPaths = nullptr;
            config.m_pIsolate = nullptr;
            config.m_v8EmbedderSlot = 0;
            FPDF_InitLibraryWithConfig(&config);
            s_initialized = true;
            LOG_INFO(PdfStorage, "PDFium library initialized successfully");
        }
#endif
    }

    static void DestroyLibrary() {
#if defined(FOLIO_HAS_PDFIUM)
        FPDF_DestroyLibrary();
#endif
    }

    static PdfPageRenderResult RenderPage(const std::string& filePath, int pageIndex, double targetDpi = 150.0) {
        PdfPageRenderResult result;
        result.widthMm = 210.0;
        result.heightMm = 297.0;

        if (filePath.empty()) {
            result.errorMessage = "Empty PDF file path";
            return result;
        }

#if defined(FOLIO_HAS_PDFIUM)
        InitializeLibrary();

        FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath.c_str(), nullptr);
        if (!doc) {
            unsigned long err = FPDF_GetLastError();
            result.errorMessage = "PDFium failed to load document (error=" + std::to_string(err) + ")";
            LOG_WARN(PdfStorage, result.errorMessage + ": " + filePath);
            return result;
        }

        int pageCount = FPDF_GetPageCount(doc);
        if (pageIndex < 0 || pageIndex >= pageCount) {
            result.errorMessage = "Page index out of range";
            FPDF_CloseDocument(doc);
            return result;
        }

        FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
        if (!page) {
            result.errorMessage = "PDFium failed to load page";
            FPDF_CloseDocument(doc);
            return result;
        }

        // PDF dimensions are in points (1 pt = 1/72 inch = 0.352778 mm)
        double ptW = FPDF_GetPageWidthF(page);
        double ptH = FPDF_GetPageHeightF(page);
        constexpr double PT_TO_MM = 25.4 / 72.0;
        result.widthMm = ptW * PT_TO_MM;
        result.heightMm = ptH * PT_TO_MM;

        int pxW = static_cast<int>((result.widthMm / 25.4) * targetDpi);
        int pxH = static_cast<int>((result.heightMm / 25.4) * targetDpi);
        if (pxW < 50) pxW = 50;
        if (pxH < 50) pxH = 50;

        // Render directly into RGBA Blend2D image
        result.image.create(pxW, pxH, BL_FORMAT_PRGB32);
        BLImageData imgData;
        if (result.image.make_mutable(&imgData) == BL_SUCCESS) {
            FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(pxW, pxH, FPDFBitmap_BGRA, imgData.pixel_data, static_cast<int>(imgData.stride));
            // Fill background white
            FPDFBitmap_FillRect(bitmap, 0, 0, pxW, pxH, 0xFFFFFFFF);
            // Render page with text antialiasing and high-quality vector rendering
            FPDF_RenderPageBitmap(bitmap, page, 0, 0, pxW, pxH, 0, FPDF_ANNOT | FPDF_PRINTING);
            FPDFBitmap_Destroy(bitmap);

            result.pixelWidth = pxW;
            result.pixelHeight = pxH;
            result.success = true;
        }

        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        return result;
#else
        result.errorMessage = "PDFium not configured yet. Using high-fidelity vector placeholder.";
        return result;
#endif
    }

    static bool LoadTextLayer(const std::string& filePath, int pageIndex, PdfTextLayer& outTextLayer) {
#if defined(FOLIO_HAS_PDFIUM)
        InitializeLibrary();
        FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath.c_str(), nullptr);
        if (!doc) return false;

        int count = FPDF_GetPageCount(doc);
        if (pageIndex < 0 || pageIndex >= count) {
            FPDF_CloseDocument(doc);
            return false;
        }

        FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
        if (!page) {
            FPDF_CloseDocument(doc);
            return false;
        }

        bool ok = outTextLayer.LoadFromPage(page);
        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        return ok;
#else
        return false;
#endif
    }
};

} // namespace Folio
