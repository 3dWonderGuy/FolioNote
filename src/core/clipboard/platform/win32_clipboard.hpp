#pragma once
/**
 * =========================================================================================
 * @file core/clipboard/platform/win32_clipboard.hpp
 * @brief Native Windows Platform Clipboard Provider for High-Fidelity OLE Interoperability
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTEROPERABILITY:
 * ----------------------------------------
 * Standard Windows applications (Microsoft OneNote, Word, PowerPoint, Paint, Discord,
 * Slack, web browsers) do not consume Linux/SDL-style MIME clipboard formats. Instead,
 * they communicate over the Win32 OLE / User32 clipboard subsystem using:
 *
 *   1. "Ink Serialized Format" (ISF):
 *      Microsoft's native binary ink format. When OneNote copies ink strokes, it writes
 *      ISF to the clipboard. By reading ISF via COM (IInkDisp), FolioNote converts
 *      OneNote strokes into native vector InkContainer objects with millimeter coordinates.
 *      Conversely, when FolioNote copies ink strokes, it encodes them to ISF so OneNote
 *      pastes them as live, fully editable vector ink!
 *
 *   2. "PNG" (Registered Clipboard Format):
 *      Raw PNG image data with full 32-bit alpha transparency. Supported by Office, OneNote,
 *      Discord, Slack, Photoshop, and modern browsers for lossless transparent graphics.
 *
 *   3. CF_DIB / CF_DIBV5 (Device Independent Bitmap):
 *      Standard Win32 raster image format supported universally by all Windows applications
 *      including Microsoft Paint and legacy software.
 *
 *   4. "image/svg+xml":
 *      Standard W3C vector graphics format for modern vector editors (Illustrator, Inkscape).
 *
 *   5. CF_UNICODETEXT:
 *      Standard UTF-16 wide string for text, links, and URLs.
 *
 * MATHEMATICAL FOUNDATIONS:
 * -------------------------
 * 1. Microsoft Ink Coordinates (HIMETRIC to Millimeters):
 *    - In Microsoft Tablet PC COM (IInkDisp / IInkStrokeDisp), all coordinates and widths
 *      are stored in HIMETRIC units:
 *        1 HIMETRIC unit = 0.01 mm = 10 micrometers = (1 / 2540) inch.
 *    - Conversion from HIMETRIC to FolioNote millimeter world space:
 *        worldMm = himetricCoord * 0.01
 *    - Conversion from FolioNote millimeter world space to HIMETRIC:
 *        himetricCoord = round(worldMm * 100.0)
 *
 * 2. Color Conversion (COLORREF vs FolioColor ARGB):
 *    - Windows COLORREF format is 0x00BBGGRR:
 *        R = c & 0xFF,  G = (c >> 8) & 0xFF,  B = (c >> 16) & 0xFF
 *    - FolioColor format is 0xAARRGGBB:
 *        FolioColor(R, G, B, 255)
 *    - Converting FolioColor to COLORREF:
 *        COLORREF = RGB(folio.r(), folio.g(), folio.b())
 *
 * 3. DIB Bitmap Header & Memory Layout (CF_DIB / CF_DIBV5):
 *    - Windows CF_DIB is bottom-up (first scanline in memory is the bottom row of the image).
 *    - Row byte alignment: DWORD-aligned (multiple of 4 bytes):
 *        rowBytes = ((width * 32 + 31) / 32) * 4 = width * 4 (for 32-bit BGRA).
 *    - To convert CF_DIB into a standard .bmp memory buffer for Blend2D decoding,
 *      a 14-byte BITMAPFILEHEADER (bfType = 0x4D42, bfOffBits) is prepended.
 */

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <type_traits>

#include <blend2d/blend2d.h>

#include "core/document/document_session.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container/ink_container.hpp"
#include "core/objects/media/images/image_container.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/objects/interactive/interactive_object.hpp"
#include "core/overlay/web_overlay.hpp"
#include "core/engine/stroke_outline_builder.hpp"
#include "utils/logger.hpp"
#include "utils/guid_generator.hpp"
#include "utils/uid_generator.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>

#ifndef interface
#define interface struct
#endif

#include <msinkaut.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef LoadImage
#undef LoadImage
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef Polygon
#undef Polygon
#endif
#ifdef GetObject
#undef GetObject
#endif
#ifdef SendMessage
#undef SendMessage
#endif

namespace Folio::PlatformWin32Clipboard {

// =============================================================================
// CLIPBOARD FORMAT IDENTIFIERS
// =============================================================================

/**
 * @brief Returns the registered Windows clipboard format ID for "PNG".
 */
inline UINT GetPngClipboardFormat() {
    static UINT s_cfPng = ::RegisterClipboardFormatW(L"PNG");
    return s_cfPng;
}

/**
 * @brief Returns the registered Windows clipboard format ID for "Ink Serialized Format".
 */
inline UINT GetIsfClipboardFormat() {
    static UINT s_cfIsf = ::RegisterClipboardFormatW(L"Ink Serialized Format");
    return s_cfIsf;
}

/**
 * @brief Returns the registered Windows clipboard format ID for "image/svg+xml".
 */
inline UINT GetSvgClipboardFormat() {
    static UINT s_cfSvg = ::RegisterClipboardFormatW(L"image/svg+xml");
    return s_cfSvg;
}

// Microsoft Tablet PC InkDisp GUIDs (from Windows SDK msinkaut_i.c):
// CLSID_InkDisp: {937C1A34-151D-4610-9CA6-A8CC9BDB5D83}
static constexpr CLSID kClsidInkDisp = { 0x937C1A34, 0x151D, 0x4610, { 0x9C, 0xA6, 0xA8, 0xCC, 0x9B, 0xDB, 0x5D, 0x83 } };
// IID_IInkDisp: {9D398FA0-C4E2-4FCD-9973-975CAAF47EA6}
static constexpr IID kIidIInkDisp = { 0x9D398FA0, 0xC4E2, 0x4FCD, { 0x99, 0x73, 0x97, 0x5C, 0xAA, 0xF4, 0x7E, 0xA6 } };

// =============================================================================
// DIB & BITMAP UTILITIES
// =============================================================================

/**
 * @brief Creates a global memory handle containing a CF_DIB (BITMAPINFOHEADER + BGRA pixels).
 *
 * Mathematical Process:
 *   1. Reads dimensions (w, h) and stride from the Blend2D surface.
 *   2. Allocates GlobalAlloc(GMEM_MOVEABLE) for BITMAPINFOHEADER (40 bytes) + (w * 4 * h).
 *   3. Populates standard BITMAPINFOHEADER with biCompression = BI_RGB and biBitCount = 32.
 *   4. Inverts vertical orientation: Blend2D is top-down (row 0 at top), whereas CF_DIB is
 *      bottom-up (row 0 at bottom), so scanlines are copied in reverse order:
 *        dstRow(y) = srcRow(h - 1 - y).
 *
 * @param blImg Source Blend2D raster surface.
 * @return HGLOBAL handle ready for SetClipboardData(CF_DIB, ...), or nullptr on failure.
 */
inline HGLOBAL CreateDibFromBlImage(const BLImage& blImg) {
    if (blImg.is_empty()) return nullptr;

    BLImageData imgData;
    if (blImg.get_data(&imgData) != BL_SUCCESS) return nullptr;

    const int w = imgData.size.w;
    const int h = imgData.size.h;
    if (w <= 0 || h <= 0) return nullptr;

    const DWORD rowBytes = static_cast<DWORD>(w * 4);
    const DWORD imageSize = rowBytes * static_cast<DWORD>(h);
    const DWORD totalSize = sizeof(BITMAPINFOHEADER) + imageSize;

    HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hGlobal) return nullptr;

    auto* dest = static_cast<uint8_t*>(::GlobalLock(hGlobal));
    if (!dest) {
        ::GlobalFree(hGlobal);
        return nullptr;
    }

    auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(dest);
    std::memset(bih, 0, sizeof(BITMAPINFOHEADER));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = w;
    bih->biHeight = h; // positive height = standard bottom-up DIB
    bih->biPlanes = 1;
    bih->biBitCount = 32;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = imageSize;

    uint8_t* pixelDest = dest + sizeof(BITMAPINFOHEADER);
    const auto* srcPixels = static_cast<const uint8_t*>(imgData.pixel_data);

    for (int y = 0; y < h; ++y) {
        const uint8_t* srcRow = srcPixels + (h - 1 - y) * imgData.stride;
        uint8_t* dstRow = pixelDest + y * rowBytes;
        std::memcpy(dstRow, srcRow, rowBytes);
    }

    ::GlobalUnlock(hGlobal);
    return hGlobal;
}

/**
 * @brief Creates a global memory handle containing CF_DIBV5 (BITMAPV5HEADER with alpha channel mask).
 *
 * Mathematical Process:
 *   1. BITMAPV5HEADER (124 bytes) allows specifying explicit 32-bit channel bitmasks:
 *        bV5RedMask   = 0x00FF0000
 *        bV5GreenMask = 0x0000FF00
 *        bV5BlueMask  = 0x000000FF
 *        bV5AlphaMask = 0xFF000000
 *   2. Office / OneNote reads CF_DIBV5 to preserve transparent alpha when pasting graphics.
 *
 * @param blImg Source Blend2D raster surface.
 * @return HGLOBAL handle ready for SetClipboardData(CF_DIBV5, ...), or nullptr on failure.
 */
inline HGLOBAL CreateDibV5FromBlImage(const BLImage& blImg) {
    if (blImg.is_empty()) return nullptr;

    BLImageData imgData;
    if (blImg.get_data(&imgData) != BL_SUCCESS) return nullptr;

    const int w = imgData.size.w;
    const int h = imgData.size.h;
    if (w <= 0 || h <= 0) return nullptr;

    const DWORD rowBytes = static_cast<DWORD>(w * 4);
    const DWORD imageSize = rowBytes * static_cast<DWORD>(h);
    const DWORD totalSize = sizeof(BITMAPV5HEADER) + imageSize;

    HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hGlobal) return nullptr;

    auto* dest = static_cast<uint8_t*>(::GlobalLock(hGlobal));
    if (!dest) {
        ::GlobalFree(hGlobal);
        return nullptr;
    }

    auto* bv5 = reinterpret_cast<BITMAPV5HEADER*>(dest);
    std::memset(bv5, 0, sizeof(BITMAPV5HEADER));
    bv5->bV5Size = sizeof(BITMAPV5HEADER);
    bv5->bV5Width = w;
    bv5->bV5Height = h;
    bv5->bV5Planes = 1;
    bv5->bV5BitCount = 32;
    bv5->bV5Compression = BI_BITFIELDS;
    bv5->bV5SizeImage = imageSize;
    bv5->bV5RedMask   = 0x00FF0000;
    bv5->bV5GreenMask = 0x0000FF00;
    bv5->bV5BlueMask  = 0x000000FF;
    bv5->bV5AlphaMask = 0xFF000000;
    bv5->bV5CSType    = LCS_sRGB;
    bv5->bV5Intent    = LCS_GM_GRAPHICS;

    uint8_t* pixelDest = dest + sizeof(BITMAPV5HEADER);
    const auto* srcPixels = static_cast<const uint8_t*>(imgData.pixel_data);

    for (int y = 0; y < h; ++y) {
        const uint8_t* srcRow = srcPixels + (h - 1 - y) * imgData.stride;
        uint8_t* dstRow = pixelDest + y * rowBytes;
        std::memcpy(dstRow, srcRow, rowBytes);
    }

    ::GlobalUnlock(hGlobal);
    return hGlobal;
}

// =============================================================================
// ISF (INK SERIALIZED FORMAT) SERIALIZATION & DESERIALIZATION
// =============================================================================

/**
 * @brief Serializes all InkContainer objects in the selection into Microsoft Ink Serialized Format (ISF).
 *
 * Mathematical & COM Integration:
 *   1. Instantiates COM CLSID_InkDisp (Microsoft Tablet PC Ink Object).
 *   2. For each Stroke in each InkContainer:
 *      - Maps local stroke centerline points through the object's affine transform:
 *          pt_world = transform.map_point(pt_local.x, pt_local.y)
 *      - Converts millimeter world coordinates to HIMETRIC units:
 *          himetric_x = round(pt_world.x * 100.0)
 *          himetric_y = round(pt_world.y * 100.0)
 *      - Creates a 1D SAFEARRAY of VT_I4 containing interleaved [x0, y0, x1, y1, ...].
 *      - Calls pInk->CreateStroke(...) to register the vector path.
 *      - Sets drawing attributes: COLORREF (RGB) and stroke width in HIMETRIC:
 *          himetric_w = round(baseWidth * scale * 100.0)
 *   3. Calls pInk->Save(IPF_InkSerializedFormat) to produce the compressed ISF byte stream.
 *
 * @param objects Canvas objects to scan for ink.
 * @param outIsfBytes Destination vector populated with ISF binary data.
 * @return true if at least one stroke was successfully converted; false otherwise.
 */
inline bool SerializeStrokesToIsf(const std::vector<std::shared_ptr<CanvasObject>>& objects,
                                 std::vector<uint8_t>& outIsfBytes) {
    IInkDisp* pInk = nullptr;
    HRESULT hr = ::CoCreateInstance(kClsidInkDisp, nullptr, CLSCTX_INPROC_SERVER, kIidIInkDisp, reinterpret_cast<void**>(&pInk));
    if (FAILED(hr) || !pInk) {
        return false;
    }

    bool hasStrokes = false;

    for (const auto& obj : objects) {
        if (!obj) continue;
        auto ink = std::dynamic_pointer_cast<InkContainer>(obj);
        if (!ink) continue;

        for (const auto& stroke : ink->strokes) {
            if (stroke.centerline.size() < 2) continue;

            const auto numPoints = static_cast<long>(stroke.centerline.size());
            SAFEARRAYBOUND bound;
            bound.cElements = static_cast<ULONG>(numPoints * 2);
            bound.lLbound = 0;

            SAFEARRAY* psa = ::SafeArrayCreate(VT_I4, 1, &bound);
            if (!psa) continue;

            LONG* coords = nullptr;
            ::SafeArrayAccessData(psa, reinterpret_cast<void**>(&coords));
            for (long p = 0; p < numPoints; ++p) {
                BLPoint tp = ink->transform.map_point(stroke.centerline[p].x, stroke.centerline[p].y);
                coords[p * 2]     = static_cast<LONG>(std::round(tp.x * 100.0));
                coords[p * 2 + 1] = static_cast<LONG>(std::round(tp.y * 100.0));
            }
            ::SafeArrayUnaccessData(psa);

            VARIANT varPoints;
            ::VariantInit(&varPoints);
            varPoints.vt = VT_ARRAY | VT_I4;
            varPoints.parray = psa;

            VARIANT varPacketDesc;
            ::VariantInit(&varPacketDesc);
            varPacketDesc.vt = VT_ERROR;
            varPacketDesc.scode = DISP_E_PARAMNOTFOUND;

            IInkStrokeDisp* pStroke = nullptr;
            hr = pInk->CreateStroke(varPoints, varPacketDesc, &pStroke);
            ::VariantClear(&varPoints);

            if (SUCCEEDED(hr) && pStroke) {
                IInkDrawingAttributes* pDA = nullptr;
                if (SUCCEEDED(pStroke->get_DrawingAttributes(&pDA)) && pDA) {
                    COLORREF winColor = RGB(stroke.color.r(), stroke.color.g(), stroke.color.b());
                    pDA->put_Color(winColor);

                    const double scale = std::hypot(ink->transform.m00, ink->transform.m01);
                    const auto wHimetric = static_cast<long>(std::round(stroke.baseWidth * (scale > 1e-4 ? scale : 1.0) * 100.0));
                    pDA->put_Width(static_cast<float>(wHimetric > 0 ? wHimetric : 50));
                    pDA->Release();
                }
                pStroke->Release();
                hasStrokes = true;
            }
        }
    }

    if (!hasStrokes) {
        pInk->Release();
        return false;
    }

    VARIANT varIsf;
    ::VariantInit(&varIsf);
    hr = pInk->Save(InkPersistenceFormat::IPF_InkSerializedFormat,
                    InkPersistenceCompressionMode::IPCM_Default,
                    &varIsf);
    pInk->Release();

    if (SUCCEEDED(hr) && (varIsf.vt & VT_ARRAY) && varIsf.parray) {
        LONG lBound = 0, uBound = 0;
        ::SafeArrayGetLBound(varIsf.parray, 1, &lBound);
        ::SafeArrayGetUBound(varIsf.parray, 1, &uBound);
        const long isfSize = uBound - lBound + 1;
        void* pData = nullptr;
        ::SafeArrayAccessData(varIsf.parray, &pData);
        outIsfBytes.resize(isfSize);
        std::memcpy(outIsfBytes.data(), pData, isfSize);
        ::SafeArrayUnaccessData(varIsf.parray);
        ::VariantClear(&varIsf);
        return true;
    }

    ::VariantClear(&varIsf);
    return false;
}

/**
 * @brief Parses raw Microsoft ISF bytes from OneNote into a native FolioNote InkContainer.
 *
 * Mathematical & Geometric Process:
 *   1. Instantiates COM CLSID_InkDisp and calls pInk->Load(varData) with the ISF byte array.
 *   2. Enumerates IInkStrokes:
 *      - Retrieves points in HIMETRIC units via pStroke->GetPoints(0, -1, &varPoints).
 *      - Converts HIMETRIC coordinates to world millimeters:
 *          px = coords[p * 2] * 0.01
 *          py = coords[p * 2 + 1] * 0.01
 *      - Extracts COLORREF and converts 0x00BBGGRR to FolioColor(R, G, B, 255).
 *      - Extracts stroke width in HIMETRIC and converts to millimeters: widthMm = winWidth * 0.01.
 *      - Builds discrete segments and calls StrokeOutlineBuilder::BuildOutline to produce the BLPath.
 *   3. Centers the resulting InkContainer at (targetWorldX, targetWorldY) and bakes the transform.
 *
 * @param isfData Pointer to raw ISF bytes.
 * @param isfSize Size in bytes.
 * @param targetWorldX Target X coordinate in millimeters.
 * @param targetWorldY Target Y coordinate in millimeters.
 * @return std::shared_ptr<InkContainer> with imported vector strokes, or nullptr on failure.
 */
inline std::shared_ptr<InkContainer> ParseIsfToInkContainer(const uint8_t* isfData,
                                                          size_t isfSize,
                                                          double targetWorldX,
                                                          double targetWorldY) {
    if (!isfData || isfSize == 0) return nullptr;

    SAFEARRAYBOUND bound;
    bound.cElements = static_cast<ULONG>(isfSize);
    bound.lLbound = 0;

    SAFEARRAY* psa = ::SafeArrayCreate(VT_UI1, 1, &bound);
    if (!psa) return nullptr;

    void* dest = nullptr;
    ::SafeArrayAccessData(psa, &dest);
    std::memcpy(dest, isfData, isfSize);
    ::SafeArrayUnaccessData(psa);

    VARIANT varData;
    ::VariantInit(&varData);
    varData.vt = VT_ARRAY | VT_UI1;
    varData.parray = psa;

    IInkDisp* pInk = nullptr;
    HRESULT hr = ::CoCreateInstance(kClsidInkDisp, nullptr, CLSCTX_INPROC_SERVER, kIidIInkDisp, reinterpret_cast<void**>(&pInk));
    if (FAILED(hr) || !pInk) {
        ::VariantClear(&varData);
        return nullptr;
    }

    hr = pInk->Load(varData);
    ::VariantClear(&varData);

    if (FAILED(hr)) {
        pInk->Release();
        return nullptr;
    }

    IInkStrokes* pStrokes = nullptr;
    if (FAILED(pInk->get_Strokes(&pStrokes)) || !pStrokes) {
        pInk->Release();
        return nullptr;
    }

    long strokeCount = 0;
    pStrokes->get_Count(&strokeCount);
    if (strokeCount <= 0) {
        pStrokes->Release();
        pInk->Release();
        return nullptr;
    }

    auto inkContainer = std::make_shared<InkContainer>();

    for (long i = 0; i < strokeCount; ++i) {
        IInkStrokeDisp* pStroke = nullptr;
        if (FAILED(pStrokes->Item(i, &pStroke)) || !pStroke) continue;

        COLORREF winColor = RGB(0, 0, 0);
        long winColorLong = 0;
        float winWidthFloat = 50.0f; // default in HIMETRIC units
        IInkDrawingAttributes* pDA = nullptr;
        if (SUCCEEDED(pStroke->get_DrawingAttributes(&pDA)) && pDA) {
            if (SUCCEEDED(pDA->get_Color(&winColorLong))) {
                winColor = static_cast<COLORREF>(winColorLong);
            }
            pDA->get_Width(&winWidthFloat);
            pDA->Release();
        }

        VARIANT varPoints;
        ::VariantInit(&varPoints);
        if (SUCCEEDED(pStroke->GetPoints(0, -1, &varPoints))) {
            if ((varPoints.vt & VT_ARRAY) && (varPoints.vt & VT_I4) && varPoints.parray) {
                LONG lBound = 0, uBound = 0;
                ::SafeArrayGetLBound(varPoints.parray, 1, &lBound);
                ::SafeArrayGetUBound(varPoints.parray, 1, &uBound);
                const long numCoords = uBound - lBound + 1;
                const long numPoints = numCoords / 2;

                LONG* coords = nullptr;
                ::SafeArrayAccessData(varPoints.parray, reinterpret_cast<void**>(&coords));
                if (coords && numPoints >= 2) {
                    Stroke s;
                    const uint8_t r = GetRValue(winColor);
                    const uint8_t g = GetGValue(winColor);
                    const uint8_t b = GetBValue(winColor);
                    s.color = BLRgba32(r, g, b, 255);

                    const double widthMm = (winWidthFloat > 0.0f) ? (static_cast<double>(winWidthFloat) * 0.01) : 0.5;
                    s.baseWidth = widthMm;

                    s.segments.reserve(numPoints - 1);
                    s.centerline.reserve(numPoints);

                    for (long p = 0; p < numPoints; ++p) {
                        const double px = coords[p * 2] * 0.01;     // HIMETRIC -> mm
                        const double py = coords[p * 2 + 1] * 0.01; // HIMETRIC -> mm
                        s.centerline.emplace_back(px, py);
                        if (p > 0) {
                            Segment1D seg;
                            seg.p0 = s.centerline[p - 1];
                            seg.p1 = Point2D(px, py);
                            seg.width = static_cast<float>(widthMm);
                            s.segments.push_back(seg);
                        }
                    }

                    // Build closed polygon outline contour (BLPath)
                    std::vector<StrokeOutlineBuilder::InputPoint> pts;
                    pts.reserve(s.centerline.size());
                    for (const auto& cp : s.centerline) {
                        pts.push_back({ static_cast<float>(cp.x), static_cast<float>(cp.y), static_cast<float>(widthMm), 1.0f });
                    }
                    s.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, CapType::Round);

                    inkContainer->AddStroke(s);
                }
                ::SafeArrayUnaccessData(varPoints.parray);
            }
            ::VariantClear(&varPoints);
        }
        pStroke->Release();
    }

    pStrokes->Release();
    pInk->Release();

    if (inkContainer->strokes.empty()) {
        return nullptr;
    }

    inkContainer->UpdateBounds();

    // Center container at (targetWorldX, targetWorldY)
    const double cx = (inkContainer->bounds.minX + inkContainer->bounds.maxX) * 0.5;
    const double cy = (inkContainer->bounds.minY + inkContainer->bounds.maxY) * 0.5;
    const double dx = targetWorldX - cx;
    const double dy = targetWorldY - cy;

    BLMatrix2D trans = BLMatrix2D::make_translation(dx, dy);
    inkContainer->ApplyTransform(trans);
    inkContainer->BakeTransform();

    return inkContainer;
}

// =============================================================================
// HIGH-LEVEL PUBLISH & INGEST IMPLEMENTATION
// =============================================================================

/**
 * @brief Publishes all multi-format representations to the native Windows OS clipboard.
 *
 * Formats Published:
 *   1. "Ink Serialized Format" (ISF) -> for OneNote vector ink.
 *   2. "PNG" -> for Office, Discord, Slack, Photoshop (lossless 32-bit alpha).
 *   3. CF_DIB / CF_DIBV5 -> for Microsoft Paint and universal Win32 paste.
 *   4. "image/svg+xml" -> for vector applications.
 *   5. CF_UNICODETEXT -> for plain text / URLs.
 *
 * @param objects Original selected canvas objects.
 * @param svgXml Serialized SVG XML string.
 * @param pngBytes Rendered PNG byte buffer.
 * @param blImg Blend2D rendered raster surface.
 * @param plainText Extracted plaintext string.
 * @param[out] outSeqNumber Captured Windows clipboard sequence number after publishing.
 * @return true if clipboard opened and data was set successfully.
 */
inline bool PublishToWindowsClipboard(const std::vector<std::shared_ptr<CanvasObject>>& objects,
                                     const std::string& svgXml,
                                     const std::vector<uint8_t>& pngBytes,
                                     const BLImage& blImg,
                                     const std::string& plainText,
                                     uint32_t& outSeqNumber) {
    if (!::OpenClipboard(nullptr)) {
        LOG_WARN(General, "PublishToWindowsClipboard: Failed to open Win32 clipboard.");
        return false;
    }

    ::EmptyClipboard();

    // 1. Publish "Ink Serialized Format" (ISF) if ink strokes are present
    std::vector<uint8_t> isfBytes;
    if (SerializeStrokesToIsf(objects, isfBytes) && !isfBytes.empty()) {
        const UINT cfIsf = GetIsfClipboardFormat();
        HGLOBAL hIsf = ::GlobalAlloc(GMEM_MOVEABLE, isfBytes.size());
        if (hIsf) {
            void* p = ::GlobalLock(hIsf);
            if (p) {
                std::memcpy(p, isfBytes.data(), isfBytes.size());
                ::GlobalUnlock(hIsf);
                ::SetClipboardData(cfIsf, hIsf);
                LOG_INFO(General, "Published Ink Serialized Format (ISF: " + std::to_string(isfBytes.size()) + " B) to Windows clipboard.");
            } else {
                ::GlobalFree(hIsf);
            }
        }
    }

    // 2. Publish "PNG" format (lossless 32-bit alpha for OneNote, Discord, Office)
    if (!pngBytes.empty()) {
        const UINT cfPng = GetPngClipboardFormat();
        HGLOBAL hPng = ::GlobalAlloc(GMEM_MOVEABLE, pngBytes.size());
        if (hPng) {
            void* p = ::GlobalLock(hPng);
            if (p) {
                std::memcpy(p, pngBytes.data(), pngBytes.size());
                ::GlobalUnlock(hPng);
                ::SetClipboardData(cfPng, hPng);
            } else {
                ::GlobalFree(hPng);
            }
        }
    }

    // 3. Publish CF_DIB & CF_DIBV5 (universal bitmap for Paint, Word, etc.)
    if (!blImg.is_empty()) {
        HGLOBAL hDibV5 = CreateDibV5FromBlImage(blImg);
        if (hDibV5) {
            ::SetClipboardData(CF_DIBV5, hDibV5);
        }
        HGLOBAL hDib = CreateDibFromBlImage(blImg);
        if (hDib) {
            ::SetClipboardData(CF_DIB, hDib);
        }
    }

    // 4. Publish "image/svg+xml"
    if (!svgXml.empty()) {
        const UINT cfSvg = GetSvgClipboardFormat();
        HGLOBAL hSvg = ::GlobalAlloc(GMEM_MOVEABLE, svgXml.size() + 1);
        if (hSvg) {
            void* p = ::GlobalLock(hSvg);
            if (p) {
                std::memcpy(p, svgXml.c_str(), svgXml.size() + 1);
                ::GlobalUnlock(hSvg);
                ::SetClipboardData(cfSvg, hSvg);
            } else {
                ::GlobalFree(hSvg);
            }
        }
    }

    // 5. Publish CF_UNICODETEXT (plain text / URLs)
    if (!plainText.empty()) {
        const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, plainText.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(wlen) * sizeof(wchar_t));
            if (hText) {
                auto* pW = static_cast<wchar_t*>(::GlobalLock(hText));
                if (pW) {
                    ::MultiByteToWideChar(CP_UTF8, 0, plainText.c_str(), -1, pW, wlen);
                    ::GlobalUnlock(hText);
                    ::SetClipboardData(CF_UNICODETEXT, hText);
                } else {
                    ::GlobalFree(hText);
                }
            }
        }
    }

    ::CloseClipboard();
    outSeqNumber = ::GetClipboardSequenceNumber();
    return true;
}

/**
 * @brief Inspects whether the native Windows OS clipboard contains supported pasteable content.
 */
inline bool HasWindowsClipboardContent() {
    const UINT cfIsf = GetIsfClipboardFormat();
    const UINT cfPng = GetPngClipboardFormat();
    const UINT cfSvg = GetSvgClipboardFormat();

    return (::IsClipboardFormatAvailable(cfIsf) ||
            ::IsClipboardFormatAvailable(cfPng) ||
            ::IsClipboardFormatAvailable(CF_DIB) ||
            ::IsClipboardFormatAvailable(CF_DIBV5) ||
            ::IsClipboardFormatAvailable(cfSvg) ||
            ::IsClipboardFormatAvailable(CF_UNICODETEXT) ||
            ::IsClipboardFormatAvailable(CF_TEXT));
}

/**
 * @brief Attempts to ingest clipboard content from the native Windows OS clipboard.
 *
 * Ingestion Hierarchy:
 *   Priority 1: "Ink Serialized Format" (ISF) -> native editable InkContainer with vector strokes.
 *   Priority 2: "PNG" -> native ImageObject with full alpha transparency.
 *   Priority 3: CF_DIBV5 / CF_DIB -> synthesized BMP decoded to native ImageObject.
 *   Priority 4: CF_UNICODETEXT -> InteractiveObject (URL) or TextBoxObject.
 *
 * @param session Active DocumentSession.
 * @param engine Active CanvasEngine.
 * @param targetWorldX Target X coordinate in millimeters.
 * @param targetWorldY Target Y coordinate in millimeters.
 * @param[out] outCreated Newly instantiated and placed CanvasObjects.
 * @return true if data was successfully ingested and added to the page; false otherwise.
 */
inline bool IngestFromWindowsClipboard(DocumentSession& session,
                                     CanvasEngine& engine,
                                     double targetWorldX,
                                     double targetWorldY,
                                     std::vector<std::shared_ptr<CanvasObject>>& outCreated) {
    if (!::OpenClipboard(nullptr)) {
        return false;
    }

    auto activePage = session.GetActivePage();
    if (!activePage) {
        ::CloseClipboard();
        return false;
    }

    const double screenDpi = engine.transform.pixelsPerMm * 25.4;

    // --- Priority 1: Ink Serialized Format (ISF from OneNote) ---
    const UINT cfIsf = GetIsfClipboardFormat();
    if (::IsClipboardFormatAvailable(cfIsf)) {
        HANDLE hData = ::GetClipboardData(cfIsf);
        if (hData) {
            const void* pData = ::GlobalLock(hData);
            const size_t dataSize = ::GlobalSize(hData);
            if (pData && dataSize > 0) {
                auto inkContainer = ParseIsfToInkContainer(static_cast<const uint8_t*>(pData), dataSize, targetWorldX, targetWorldY);
                ::GlobalUnlock(hData);
                if (inkContainer) {
                    inkContainer->uid = UIDGenerator::Next();
                    inkContainer->guuid = GUIDGenerator::GenerateV4();
                    inkContainer->isSelected = 1;

                    session.AddObject(inkContainer);
                    outCreated.push_back(inkContainer);
                    engine.selectionGizmo.SetSelectedObjects({inkContainer});
                    engine.needsFullRebake = true;
                    engine.isDirty = true;
                    ::CloseClipboard();
                    LOG_INFO(General, "Ingested live vector ink from OneNote (ISF: " +
                             std::to_string(inkContainer->strokes.size()) + " strokes) at (" +
                             std::to_string(targetWorldX) + ", " + std::to_string(targetWorldY) + ") mm");
                    return true;
                }
            } else if (pData) {
                ::GlobalUnlock(hData);
            }
        }
    }

    // --- Priority 2: PNG format (from OneNote, Office, Discord, web browsers) ---
    const UINT cfPng = GetPngClipboardFormat();
    if (::IsClipboardFormatAvailable(cfPng)) {
        HANDLE hData = ::GetClipboardData(cfPng);
        if (hData) {
            const void* pData = ::GlobalLock(hData);
            const size_t dataSize = ::GlobalSize(hData);
            if (pData && dataSize > 0) {
                std::string relPath = CanvasEngine::DeduplicateAndSaveImage("", pData, dataSize, &session, ".png");
                auto img = std::make_shared<ImageObject>(static_cast<const uint8_t*>(pData), dataSize, relPath, screenDpi);
                ::GlobalUnlock(hData);

                if (img->isLoaded) {
                    img->uid = UIDGenerator::Next();
                    img->guuid = GUIDGenerator::GenerateV4();
                    img->worldX = targetWorldX - img->worldWidth * 0.5;
                    img->worldY = targetWorldY - img->worldHeight * 0.5;
                    img->UpdateBounds();
                    img->isSelected = 1;

                    session.AddImage(img);
                    outCreated.push_back(img);
                    engine.selectionGizmo.SetSelectedObjects({img});
                    engine.needsFullRebake = true;
                    engine.isDirty = true;
                    ::CloseClipboard();
                    LOG_INFO(General, "Ingested PNG image from Windows clipboard (" +
                             std::to_string(img->naturalWidth) + "x" + std::to_string(img->naturalHeight) + " px)");
                    return true;
                }
            } else if (pData) {
                ::GlobalUnlock(hData);
            }
        }
    }

    // --- Priority 3: CF_DIB / CF_DIBV5 (from Paint or legacy tools) ---
    if (::IsClipboardFormatAvailable(CF_DIB) || ::IsClipboardFormatAvailable(CF_DIBV5)) {
        const UINT dibFmt = ::IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
        HANDLE hData = ::GetClipboardData(dibFmt);
        if (hData) {
            const auto* pData = static_cast<const uint8_t*>(::GlobalLock(hData));
            const size_t dataSize = ::GlobalSize(hData);
            if (pData && dataSize >= sizeof(BITMAPINFOHEADER)) {
                const auto* bih = reinterpret_cast<const BITMAPINFOHEADER*>(pData);

                // Synthesize 14-byte BITMAPFILEHEADER
                BITMAPFILEHEADER bfh;
                std::memset(&bfh, 0, sizeof(BITMAPFILEHEADER));
                bfh.bfType = 0x4D42; // "BM"
                bfh.bfSize = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + dataSize);

                DWORD colorTableSize = 0;
                if (bih->biBitCount <= 8) {
                    const DWORD numColors = bih->biClrUsed ? bih->biClrUsed : (1U << bih->biBitCount);
                    colorTableSize = numColors * sizeof(RGBQUAD);
                } else if (bih->biCompression == BI_BITFIELDS) {
                    colorTableSize = 3 * sizeof(DWORD);
                }
                bfh.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + bih->biSize + colorTableSize);

                std::vector<uint8_t> bmpBuffer(sizeof(BITMAPFILEHEADER) + dataSize);
                std::memcpy(bmpBuffer.data(), &bfh, sizeof(BITMAPFILEHEADER));
                std::memcpy(bmpBuffer.data() + sizeof(BITMAPFILEHEADER), pData, dataSize);
                ::GlobalUnlock(hData);

                // Decode BMP buffer via Blend2D (auto-detects BMP header)
                BLImage decodedBmp;
                if (decodedBmp.read_from_data(bmpBuffer.data(), bmpBuffer.size()) == BL_SUCCESS && !decodedBmp.is_empty()) {
                    BLArray<uint8_t> pngBytes;
                    BLImageCodec pngCodec;
                    pngCodec.find_by_name("PNG");
                    decodedBmp.write_to_data(pngBytes, pngCodec);

                    if (!pngBytes.is_empty()) {
                        std::string relPath = CanvasEngine::DeduplicateAndSaveImage("", pngBytes.data(), pngBytes.size(), &session, ".png");
                        auto img = std::make_shared<ImageObject>(pngBytes.data(), pngBytes.size(), relPath, screenDpi);
                        if (img->isLoaded) {
                            img->uid = UIDGenerator::Next();
                            img->guuid = GUIDGenerator::GenerateV4();
                            img->worldX = targetWorldX - img->worldWidth * 0.5;
                            img->worldY = targetWorldY - img->worldHeight * 0.5;
                            img->UpdateBounds();
                            img->isSelected = 1;

                            session.AddImage(img);
                            outCreated.push_back(img);
                            engine.selectionGizmo.SetSelectedObjects({img});
                            engine.needsFullRebake = true;
                            engine.isDirty = true;
                            ::CloseClipboard();
                            LOG_INFO(General, "Ingested DIB bitmap from Windows clipboard (" +
                                     std::to_string(img->naturalWidth) + "x" + std::to_string(img->naturalHeight) + " px)");
                            return true;
                        }
                    }
                }
            } else if (pData) {
                ::GlobalUnlock(hData);
            }
        }
    }

    // --- Priority 4: CF_UNICODETEXT (Plain text, URLs, paths) ---
    if (::IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const auto* pWText = static_cast<const wchar_t*>(::GlobalLock(hData));
            if (pWText) {
                const int utf8Len = ::WideCharToMultiByte(CP_UTF8, 0, pWText, -1, nullptr, 0, nullptr, nullptr);
                std::string clipText;
                if (utf8Len > 0) {
                    clipText.resize(utf8Len - 1);
                    ::WideCharToMultiByte(CP_UTF8, 0, pWText, -1, clipText.data(), utf8Len, nullptr, nullptr);
                }
                ::GlobalUnlock(hData);

                if (!clipText.empty()) {
                    const bool isHttp = (clipText.rfind("http://", 0) == 0 || clipText.rfind("https://", 0) == 0);
                    const bool isYouTube = (clipText.find("youtube.com") != std::string::npos || clipText.find("youtu.be") != std::string::npos);

                    if (isYouTube || isHttp) {
                        auto overlay = std::make_unique<WebOverlay>(clipText, isYouTube ? "YouTube Video" : "Web Embed", engine.sdlWindow);
                        const double cardW = 160.0;
                        const double cardH = isYouTube ? 95.0 : 110.0;
                        auto interactiveObj = std::make_shared<InteractiveObject>(
                            targetWorldX - cardW * 0.5,
                            targetWorldY - cardH * 0.5,
                            cardW, cardH,
                            std::move(overlay)
                        );
                        interactiveObj->uid = UIDGenerator::Next();
                        interactiveObj->guuid = GUIDGenerator::GenerateV4();
                        interactiveObj->isSelected = 1;

                        session.AddObject(interactiveObj);
                        outCreated.push_back(interactiveObj);
                        engine.selectionGizmo.SetSelectedObjects({interactiveObj});
                        engine.needsFullRebake = true;
                        engine.isDirty = true;
                        ::CloseClipboard();
                        LOG_INFO(General, "Ingested URL from Windows clipboard as InteractiveObject: " + clipText);
                        return true;
                    }

                    // Fallback: Create TextBoxObject for text
                    auto tb = std::make_shared<TextBoxObject>();
                    tb->uid = UIDGenerator::Next();
                    tb->guuid = GUIDGenerator::GenerateV4();
                    tb->text = clipText;
                    tb->worldX = targetWorldX - 40.0;
                    tb->worldY = targetWorldY - 15.0;
                    tb->worldWidth = 80.0;
                    tb->worldHeight = 30.0;
                    tb->UpdateBounds();
                    tb->isSelected = 1;

                    session.AddTextBox(tb);
                    outCreated.push_back(tb);
                    engine.selectionGizmo.SetSelectedObjects({tb});
                    engine.needsFullRebake = true;
                    engine.isDirty = true;
                    ::CloseClipboard();
                    LOG_INFO(General, "Ingested text from Windows clipboard into TextBoxObject (" + std::to_string(clipText.size()) + " chars)");
                    return true;
                }
            }
        }
    }

    ::CloseClipboard();
    return false;
}

} // namespace Folio::PlatformWin32Clipboard

#endif // _WIN32
