/**
 * =========================================================================================
 * @file core/overlay/web_overlay.cpp
 * @brief Implementation of WebOverlay Layer 3 Subsystem for Live Web & YouTube Playback
 * =========================================================================================
 */

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <urlmon.h>
#endif

#include "core/overlay/web_overlay.hpp"
#include "core/text/font_manager.hpp"
#include "core/objects/media/images/image_decoder.hpp"
#include "utils/logger.hpp"

#include <webview.h>
#include <SDL3/SDL.h>

#include <thread>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Folio {

// =============================================================================
// CONSTRUCTOR & DESTRUCTOR
// =============================================================================

/**
 * @brief Constructs a WebOverlay, parses the URL for YouTube or generic web signatures,
 * and sets up display metadata.
 *
 * @param[in] url The raw target URL.
 * @param[in] title Optional title string.
 * @param[in] parentWin Host SDL window pointer.
 */
WebOverlay::WebOverlay(std::string url, std::string title, SDL_Window* parentWin)
    : rawUrl(std::move(url)), pageTitle(std::move(title)), parentWindow(parentWin)
{
    ParseUrl(rawUrl);
}

WebOverlay::~WebOverlay() {
    Shutdown();
}

// =============================================================================
// URL PARSER & METADATA EXTRACTION
// =============================================================================

/**
 * @brief Parses incoming URL, extracts YouTube video ID if applicable,
 * formats the canonical iframe embed URL, and extracts domain name.
 *
 * Mathematical / Algorithmic Process:
 * 1. Checks for YouTube domain indicators: "youtu.be", "youtube.com/watch", "youtube.com/embed", "youtube.com/shorts".
 * 2. Parses ID substring between token delimiters ('=', '/', '?', '&', '#').
 * 3. Builds canonical embed string: "https://www.youtube.com/embed/<id>?autoplay=1&enablejsapi=1".
 * 4. Extracts domain name by finding protocol delimiter "://" and following path slash '/'.
 *
 * @param[in] url Raw URL string to parse.
 */
void WebOverlay::ParseUrl(const std::string& url) {
    if (url.empty()) return;

    // Normalize protocol
    std::string normUrl = url;
    if (normUrl.find("://") == std::string::npos) {
        normUrl = "https://" + normUrl;
    }

    // 1. YouTube detection
    isYouTubeVideo = false;
    youTubeId.clear();

    // Check youtu.be/<id>
    auto pos = normUrl.find("youtu.be/");
    if (pos != std::string::npos) {
        std::string sub = normUrl.substr(pos + 9);
        auto end = sub.find_first_of("?&#/");
        youTubeId = (end != std::string::npos) ? sub.substr(0, end) : sub;
        isYouTubeVideo = !youTubeId.empty();
    }

    // Check youtube.com/watch?v=<id>
    if (!isYouTubeVideo) {
        pos = normUrl.find("v=");
        if (pos != std::string::npos && normUrl.find("youtube.com") != std::string::npos) {
            std::string sub = normUrl.substr(pos + 2);
            auto end = sub.find_first_of("&?#/");
            youTubeId = (end != std::string::npos) ? sub.substr(0, end) : sub;
            isYouTubeVideo = !youTubeId.empty();
        }
    }

    // Check youtube.com/embed/<id>
    if (!isYouTubeVideo) {
        pos = normUrl.find("/embed/");
        if (pos != std::string::npos) {
            std::string sub = normUrl.substr(pos + 7);
            auto end = sub.find_first_of("?&#/");
            youTubeId = (end != std::string::npos) ? sub.substr(0, end) : sub;
            isYouTubeVideo = !youTubeId.empty();
        }
    }

    // Check youtube.com/shorts/<id>
    if (!isYouTubeVideo) {
        pos = normUrl.find("/shorts/");
        if (pos != std::string::npos) {
            std::string sub = normUrl.substr(pos + 8);
            auto end = sub.find_first_of("?&#/");
            youTubeId = (end != std::string::npos) ? sub.substr(0, end) : sub;
            isYouTubeVideo = !youTubeId.empty();
        }
    }

    // Construct embed URL
    if (isYouTubeVideo) {
        embedUrl = "https://www.youtube.com/embed/" + youTubeId + "?autoplay=1&enablejsapi=1";
        domainName = "youtube.com";
        if (pageTitle.empty()) {
            pageTitle = "YouTube Video (" + youTubeId + ")";
        }
    } else {
        embedUrl = normUrl;
        // Extract domain name
        auto protoPos = normUrl.find("://");
        if (protoPos != std::string::npos) {
            std::string afterProto = normUrl.substr(protoPos + 3);
            auto slashPos = afterProto.find('/');
            domainName = (slashPos != std::string::npos) ? afterProto.substr(0, slashPos) : afterProto;
        } else {
            domainName = normUrl;
        }
        if (pageTitle.empty()) {
            pageTitle = domainName;
        }
    }
}

// =============================================================================
// IInteractiveOverlay LIFECYCLE
// =============================================================================

bool WebOverlay::Initialize() {
    // 1. Resolve vector fonts via FontManager
    titleFont = FontManager::Instance().GetFont("Segoe UI", 11.0f, true);
    domainFont = FontManager::Instance().GetFont("Segoe UI", 9.5f, false);
    btnFont = FontManager::Instance().GetFont("Segoe UI", 10.0f, true);
    fontsLoaded = true;

    // 2. Fetch poster thumbnail if YouTube
    if (isYouTubeVideo && !youTubeId.empty()) {
        FetchYouTubeThumbnailAsync();
    }

    return true;
}

void WebOverlay::Shutdown() {
    Deactivate();

#if defined(_WIN32)
    if (nativeWebview) {
        webview_destroy(static_cast<webview_t>(nativeWebview));
        nativeWebview = nullptr;
        nativeWidgetHwnd = nullptr;
    }
#endif
}

void WebOverlay::OnUpdate(uint64_t nowMs, double deltaTime) {
    (void)nowMs;
    (void)deltaTime;
}

// =============================================================================
// POSTER THUMBNAIL CACHING
// =============================================================================

/**
 * @brief Downloads and decodes YouTube HQ poster thumbnail asynchronously.
 *
 * GENERAL WORKING PROCESS:
 * 1. Checks whether thumbnail is already loaded or in-flight; returns immediately if so.
 * 2. Caches JPEG in OS temp folder: %TEMP%/FolioNote/yt_thumbs/<id>.jpg.
 * 3. Uses URLDownloadToFileA or curl.exe to retrieve image from YouTube CDN.
 * 4. Decodes image into Blend2D BLImage using ImageDecoder::DecodeFromFile.
 */
void WebOverlay::FetchYouTubeThumbnailAsync() {
    if (!isYouTubeVideo || youTubeId.empty()) return;
    if (thumbnailLoaded || isFetchingThumbnail.exchange(true)) return;

    std::string vidId = youTubeId;
    std::thread([this, vidId]() {
        std::string thumbUrl = "https://img.youtube.com/vi/" + vidId + "/hqdefault.jpg";
        std::filesystem::path cacheDir = std::filesystem::temp_directory_path() / "FolioNote" / "yt_thumbs";
        std::error_code ec;
        std::filesystem::create_directories(cacheDir, ec);
        std::filesystem::path cacheFile = cacheDir / (vidId + ".jpg");

        bool downloaded = false;
        if (std::filesystem::exists(cacheFile, ec)) {
            downloaded = true;
        } else {
#if defined(_WIN32)
            HRESULT hr = URLDownloadToFileA(nullptr, thumbUrl.c_str(), cacheFile.string().c_str(), 0, nullptr);
            downloaded = (hr == S_OK && std::filesystem::exists(cacheFile, ec));
            if (!downloaded) {
                std::string curlCmd = "curl.exe -s -L -o \"" + cacheFile.string() + "\" \"" + thumbUrl + "\"";
                int ret = std::system(curlCmd.c_str());
                downloaded = (ret == 0 && std::filesystem::exists(cacheFile, ec));
            }
#else
            std::string curlCmd = "curl -s -L -o \"" + cacheFile.string() + "\" \"" + thumbUrl + "\"";
            int ret = std::system(curlCmd.c_str());
            downloaded = (ret == 0 && std::filesystem::exists(cacheFile, ec));
#endif
        }

        if (downloaded) {
            auto decoded = ImageDecoder::DecodeFromFile(cacheFile.string());
            if (!decoded.image.is_empty()) {
                thumbnailImg = std::move(decoded.image);
                thumbnailLoaded = true;
                LOG_INFO(CanvasObject, "WebOverlay: Loaded poster thumbnail for YouTube ID: " + vidId);
            }
        }
        isFetchingThumbnail.store(false);
    }).detach();
}

// =============================================================================
// NATIVE CHILD WINDOW GEOMETRY & MANAGEMENT
// =============================================================================

/**
 * @brief Synchronizes OS child window size and position with screenRect.
 *
 * Mathematical Layout:
 *   Total card:     [screenRect.x, screenRect.y, screenRect.width, screenRect.height]
 *   Top Header bar: [screenRect.x, screenRect.y, screenRect.width, kHeaderHeight (36px)]
 *   Web Content:    [cx = screenRect.x, cy = screenRect.y + 36, cw = screenRect.width, ch = screenRect.height - 36]
 *
 * @param[in] screenRect Projected integer screen pixel bounding box.
 */
void WebOverlay::UpdateChildWindowGeometry(const OverlayRect& screenRect) {
#if defined(_WIN32)
    if (!nativeWidgetHwnd) return;
    HWND hwnd = static_cast<HWND>(nativeWidgetHwnd);

    if (!isActive || !isVisibleInFrustum || screenRect.IsEmpty()) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    int cx = screenRect.x;
    int cy = screenRect.y + kHeaderHeight;
    int cw = std::max(1, screenRect.width);
    int ch = std::max(1, screenRect.height - kHeaderHeight);

    SetWindowPos(hwnd, HWND_TOP, cx, cy, cw, ch, SWP_NOACTIVATE | SWP_SHOWWINDOW);
#else
    (void)screenRect;
#endif
}

// =============================================================================
// ACTIVATION & DEACTIVATION
// =============================================================================

void WebOverlay::Activate() {
#if defined(_WIN32)
    if (isActive) return;

    // Ensure COM apartment threaded initialized for WebView2
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (!nativeWebview) {
        HWND parentHwnd = nullptr;
        if (parentWindow) {
            SDL_PropertiesID props = SDL_GetWindowProperties(parentWindow);
            parentHwnd = static_cast<HWND>(
                SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        }

        if (parentHwnd) {
            nativeWebview = webview_create(0, parentHwnd);
            if (nativeWebview) {
                nativeWidgetHwnd = webview_get_native_handle(
                    static_cast<webview_t>(nativeWebview),
                    WEBVIEW_NATIVE_HANDLE_KIND_UI_WIDGET
                );
                webview_navigate(static_cast<webview_t>(nativeWebview), embedUrl.c_str());
                LOG_INFO(CanvasObject, "WebOverlay: Initialized live WebView2 for " + embedUrl);
            } else {
                LOG_ERROR(CanvasObject, "WebOverlay: webview_create failed on parent HWND");
            }
        } else {
            LOG_ERROR(CanvasObject, "WebOverlay: No valid parent HWND resolved from SDL_Window");
        }
    }

    if (nativeWidgetHwnd) {
        isActive = true;
        if (!currentScreenRect.IsEmpty()) {
            UpdateChildWindowGeometry(currentScreenRect);
        }
        ShowWindow(static_cast<HWND>(nativeWidgetHwnd), SW_SHOW);
    }
#else
    isActive = true;
#endif
}

void WebOverlay::Deactivate() {
#if defined(_WIN32)
    if (!isActive) return;
    isActive = false;
    if (nativeWidgetHwnd) {
        ShowWindow(static_cast<HWND>(nativeWidgetHwnd), SW_HIDE);
    }
#else
    isActive = false;
#endif
}

void WebOverlay::ToggleActive() {
    if (isActive) {
        Deactivate();
    } else {
        Activate();
    }
}

void WebOverlay::Reload() {
#if defined(_WIN32)
    if (nativeWebview) {
        webview_navigate(static_cast<webview_t>(nativeWebview), embedUrl.c_str());
    }
#endif
}

void WebOverlay::Navigate(const std::string& newUrl) {
    rawUrl = newUrl;
    ParseUrl(rawUrl);
#if defined(_WIN32)
    if (nativeWebview) {
        webview_navigate(static_cast<webview_t>(nativeWebview), embedUrl.c_str());
    }
#endif
    if (isYouTubeVideo && !thumbnailLoaded) {
        FetchYouTubeThumbnailAsync();
    }
}

void WebOverlay::OpenInExternalBrowser() const {
    std::string urlToOpen = rawUrl.empty() ? embedUrl : rawUrl;
    if (!urlToOpen.empty()) {
        SDL_OpenURL(urlToOpen.c_str());
    }
}

// =============================================================================
// RENDERING
// =============================================================================

/**
 * @brief Renders the Blend2D Layer 3 presentation.
 *
 * LAYOUT MATHEMATICS:
 *   Total:   x = screenRect.x, y = screenRect.y, w = screenRect.width, h = screenRect.height
 *   Header:  x, y, w, 36.0 px
 *   Content: x, y + 36.0, w, h - 36.0 px
 *
 * @param[in,out] ctx Blend2D rendering context targeting screen composite surface.
 * @param[in] screenRect Projected screen pixel bounding box.
 */
void WebOverlay::OnRenderOverlay(BLContext& ctx, const OverlayRect& screenRect) {
    if (screenRect.IsEmpty()) return;
    currentScreenRect = screenRect;

    // Synchronize native OS child window bounds if active
    if (isActive) {
        UpdateChildWindowGeometry(screenRect);
    }

    ctx.save();

    const double cornerR = 6.0;
    const double x = static_cast<double>(screenRect.x);
    const double y = static_cast<double>(screenRect.y);
    const double w = static_cast<double>(screenRect.width);
    const double h = static_cast<double>(screenRect.height);
    const double headerH = static_cast<double>(kHeaderHeight);
    const double contentH = std::max(10.0, h - headerH);

    // 1. Clip entire overlay to rectangle
    ctx.clip_to_rect(BLRect(x, y, w, h));

    // 2. Render Header Bar Background (Dark glassmorphic slate)
    ctx.set_fill_style(BLRgba32(0x18, 0x1C, 0x26, 0xF8));
    ctx.fill_rect(BLRect(x, y, w, headerH));

    // Header bottom border separator line
    ctx.set_stroke_style(BLRgba32(0x2E, 0x36, 0x48, 0xFF));
    ctx.set_stroke_width(1.0);
    ctx.stroke_line(x, y + headerH - 0.5, x + w, y + headerH - 0.5);

    // 3. Render Header Icon / Badge
    double textStartX = x + 10.0;
    if (isYouTubeVideo) {
        // YouTube Red Pill Badge
        const double badgeW = 28.0;
        const double badgeH = 18.0;
        const double badgeX = x + 10.0;
        const double badgeY = y + (headerH - badgeH) * 0.5;

        ctx.set_fill_style(BLRgba32(0xFF, 0x00, 0x00, 0xFF)); // YouTube Red
        ctx.fill_round_rect(BLRoundRect(badgeX, badgeY, badgeW, badgeH, 4.0, 4.0));

        // White Play Triangle
        BLPath tri;
        tri.move_to(badgeX + 11.0, badgeY + 5.0);
        tri.line_to(badgeX + 19.0, badgeY + 9.0);
        tri.line_to(badgeX + 11.0, badgeY + 13.0);
        tri.close();
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.fill_path(tri);

        textStartX = badgeX + badgeW + 8.0;
    } else {
        // Generic Web Globe Icon
        const double cx = x + 18.0;
        const double cy = y + headerH * 0.5;
        ctx.set_stroke_style(BLRgba32(0x3B, 0x82, 0xF6, 0xFF)); // Electric blue
        ctx.set_stroke_width(1.2);
        ctx.stroke_circle(cx, cy, 7.0);
        ctx.stroke_ellipse(cx, cy, 3.5, 7.0);
        ctx.stroke_line(cx - 7.0, cy, cx + 7.0, cy);

        textStartX = x + 32.0;
    }

    // 4. Render Title & Domain Text
    if (fontsLoaded) {
        // Title (truncated to available width)
        const double availableTextW = std::max(50.0, w - (textStartX - x) - 140.0);
        ctx.set_fill_style(BLRgba32(0xEE, 0xF2, 0xF6, 0xFF));
        ctx.fill_utf8_text(BLPoint(textStartX, y + 16.0), titleFont, pageTitle.c_str());

        // Domain subtitle
        ctx.set_fill_style(BLRgba32(0x8A, 0x94, 0xA6, 0xFF));
        ctx.fill_utf8_text(BLPoint(textStartX, y + 28.0), domainFont, domainName.c_str());
    }

    // 5. Header Action Buttons (Right side)
    // Button: [LIVE TOGGLE / BADGE]
    const double liveBtnW = 62.0;
    const double liveBtnH = 20.0;
    const double liveBtnX = x + w - liveBtnW - 8.0;
    const double liveBtnY = y + (headerH - liveBtnH) * 0.5;

    if (isActive) {
        // Active "LIVE" badge (emerald green)
        ctx.set_fill_style(BLRgba32(0x0E, 0x3A, 0x22, 0xFF));
        ctx.fill_round_rect(BLRoundRect(liveBtnX, liveBtnY, liveBtnW, liveBtnH, 4.0, 4.0));
        ctx.set_stroke_style(BLRgba32(0x10, 0xB9, 0x81, 0xFF));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(liveBtnX, liveBtnY, liveBtnW, liveBtnH, 4.0, 4.0));

        // Green pulsing dot
        ctx.set_fill_style(BLRgba32(0x10, 0xB9, 0x81, 0xFF));
        ctx.fill_circle(liveBtnX + 10.0, liveBtnY + liveBtnH * 0.5, 3.5);

        if (fontsLoaded) {
            ctx.set_fill_style(BLRgba32(0xD1, 0xFA, 0xE5, 0xFF));
            ctx.fill_utf8_text(BLPoint(liveBtnX + 18.0, liveBtnY + 14.0), btnFont, "LIVE");
        }
    } else {
        // Dormant "START" button
        ctx.set_fill_style(BLRgba32(0x25, 0x2E, 0x3E, 0xEE));
        ctx.fill_round_rect(BLRoundRect(liveBtnX, liveBtnY, liveBtnW, liveBtnH, 4.0, 4.0));
        ctx.set_stroke_style(BLRgba32(0x4B, 0x5E, 0x7E, 0xFF));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(liveBtnX, liveBtnY, liveBtnW, liveBtnH, 4.0, 4.0));

        if (fontsLoaded) {
            ctx.set_fill_style(BLRgba32(0xE2, 0xE8, 0xF0, 0xFF));
            ctx.fill_utf8_text(BLPoint(liveBtnX + 12.0, liveBtnY + 14.0), btnFont, "PLAY");
        }
    }

    // Button: [EXTERNAL BROWSER LINK]
    const double extBtnW = 24.0;
    const double extBtnH = 20.0;
    const double extBtnX = liveBtnX - extBtnW - 6.0;
    const double extBtnY = liveBtnY;

    ctx.set_fill_style(BLRgba32(0x20, 0x26, 0x34, 0xAA));
    ctx.fill_round_rect(BLRoundRect(extBtnX, extBtnY, extBtnW, extBtnH, 4.0, 4.0));
    // Arrow box glyph ↗
    ctx.set_stroke_style(BLRgba32(0x94, 0xA3, 0xB8, 0xFF));
    ctx.set_stroke_width(1.2);
    ctx.stroke_line(extBtnX + 8.0, extBtnY + 12.0, extBtnX + 15.0, extBtnY + 6.0);
    ctx.stroke_line(extBtnX + 11.0, extBtnY + 6.0, extBtnX + 15.0, extBtnY + 6.0);
    ctx.stroke_line(extBtnX + 15.0, extBtnY + 6.0, extBtnX + 15.0, extBtnY + 10.0);

    // 6. Content Area Presentation (Below Header)
    const double contentY = y + headerH;

    if (!isActive) {
        // DORMANT POSTER PASS: Renders thumbnail and interactive play invitation
        // Background slate
        ctx.set_fill_style(BLRgba32(0x0F, 0x11, 0x17, 0xFF));
        ctx.fill_rect(BLRect(x, contentY, w, contentH));

        // Poster image blit (aspect fit/fill)
        if (thumbnailLoaded && !thumbnailImg.is_empty()) {
            const double imgW = static_cast<double>(thumbnailImg.width());
            const double imgH = static_cast<double>(thumbnailImg.height());
            if (imgW > 0.0 && imgH > 0.0) {
                double scale = std::max(w / imgW, contentH / imgH);
                double destW = imgW * scale;
                double destH = imgH * scale;
                double destX = x + (w - destW) * 0.5;
                double destY = contentY + (contentH - destH) * 0.5;

                ctx.save();
                ctx.clip_to_rect(BLRect(x, contentY, w, contentH));
                ctx.blit_image(BLRect(destX, destY, destW, destH), thumbnailImg);

                // Dark vignette overlay
                ctx.set_fill_style(BLRgba32(0x00, 0x00, 0x00, 0x60));
                ctx.fill_rect(BLRect(x, contentY, w, contentH));
                ctx.restore();
            }
        }

        // Center Prominent Play Button / Callout
        const double playBtnW = 68.0;
        const double playBtnH = 46.0;
        const double playBtnX = x + (w - playBtnW) * 0.5;
        const double playBtnY = contentY + (contentH - playBtnH) * 0.5;

        // Button background
        if (isYouTubeVideo) {
            ctx.set_fill_style(BLRgba32(0xDC, 0x26, 0x26, 0xF0)); // Red-600
        } else {
            ctx.set_fill_style(BLRgba32(0x25, 0x63, 0xEB, 0xF0)); // Blue-600
        }
        ctx.fill_round_rect(BLRoundRect(playBtnX, playBtnY, playBtnW, playBtnH, 12.0, 12.0));

        // Subtle drop shadow / glow
        ctx.set_stroke_style(BLRgba32(0xFF, 0xFF, 0xFF, 0x60));
        ctx.set_stroke_width(1.5);
        ctx.stroke_round_rect(BLRoundRect(playBtnX, playBtnY, playBtnW, playBtnH, 12.0, 12.0));

        // Centered White Triangle
        BLPath playTri;
        playTri.move_to(playBtnX + 27.0, playBtnY + 14.0);
        playTri.line_to(playBtnX + 45.0, playBtnY + 23.0);
        playTri.line_to(playBtnX + 27.0, playBtnY + 32.0);
        playTri.close();
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.fill_path(playTri);

        // Click to launch live web text
        if (fontsLoaded && contentH > 100.0) {
            const char* label = isYouTubeVideo ? "Click to Watch Live YouTube Video" : "Click to Open Live Interactive Web Page";
            ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xEE));
            // Measure or center roughly
            ctx.fill_utf8_text(BLPoint(x + (w * 0.5) - 95.0, playBtnY + playBtnH + 20.0), btnFont, label);
        }
    } else {
        // ACTIVE LIVE PASS: The native OS child window occupies (x, contentY, w, contentH).
        // Blend2D draws a subtle 1px border around the card to frame the live view.
        ctx.set_stroke_style(BLRgba32(0x3B, 0x82, 0xF6, 0xAA));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(x, y, w, h, cornerR, cornerR));
    }

    ctx.restore();
}

// =============================================================================
// INPUT ROUTING
// =============================================================================

/**
 * @brief Consumes input events targeting the header buttons or activating dormant mode.
 *
 * Hit-testing zones:
 * 1. Header Bar:
 *    - Live/Play toggle button: activates or deactivates webview.
 *    - External link button: launches system browser.
 *    - Other header clicks: returns false so SelectionGizmo can drag/move the card!
 * 2. Content Area (Below header):
 *    - When dormant: single click activates live webview.
 *    - When active: native child window receives events directly from OS message loop.
 */
bool WebOverlay::OnInputEvent(const SDL_Event& event, const OverlayRect& screenRect) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mx = static_cast<int>(event.button.x);
        int my = static_cast<int>(event.button.y);

        if (!screenRect.Contains(mx, my)) {
            return false;
        }

        // Check if click is within top 36px header bar
        if (my >= screenRect.y && my < (screenRect.y + kHeaderHeight)) {
            // Live toggle button rect:
            const int liveBtnW = 62;
            const int liveBtnH = 20;
            const int liveBtnX = screenRect.x + screenRect.width - liveBtnW - 8;
            const int liveBtnY = screenRect.y + (kHeaderHeight - liveBtnH) / 2;

            if (mx >= liveBtnX && mx < (liveBtnX + liveBtnW) &&
                my >= liveBtnY && my < (liveBtnY + liveBtnH)) {
                ToggleActive();
                return true; // Consumed by button
            }

            // External link button rect:
            const int extBtnW = 24;
            const int extBtnH = 20;
            const int extBtnX = liveBtnX - extBtnW - 6;
            const int extBtnY = liveBtnY;

            if (mx >= extBtnX && mx < (extBtnX + extBtnW) &&
                my >= extBtnY && my < (extBtnY + extBtnH)) {
                OpenInExternalBrowser();
                return true; // Consumed by button
            }

            // Clicked header background: return false so user can drag the card!
            return false;
        }

        // Click is within content area:
        if (!isActive) {
            // Dormant click -> activate live player!
            Activate();
            return true;
        }
    }

    return false;
}

// =============================================================================
// SUBSYSTEM HOOKS & CLONING
// =============================================================================

void WebOverlay::OnFocusGained() {
}

void WebOverlay::OnFocusLost() {
}

void WebOverlay::OnVisibilityChanged(bool isVisible) {
    isVisibleInFrustum = isVisible;
    if (isActive && !currentScreenRect.IsEmpty()) {
        UpdateChildWindowGeometry(currentScreenRect);
    }
}

std::unique_ptr<IInteractiveOverlay> WebOverlay::CloneOverlay() const {
    auto clone = std::make_unique<WebOverlay>(rawUrl, pageTitle, parentWindow);
    (void)clone->Initialize();
    return clone;
}

} // namespace Folio
