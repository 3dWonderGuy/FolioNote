/**
 * =========================================================================================
 * @file core/overlay/web_overlay.hpp
 * @brief Live Web View and YouTube Player Overlay Subsystem for FolioNote Layer 3
 *
 * Architecture Context:
 *   FolioNote Layer 3 interactive overlays host live, interactive components anchored to
 *   world-space coordinates on the infinite canvas.
 *
 * Capabilities:
 *   1. Official YouTube Playback: Uses embedded iframe playback (https://www.youtube.com/embed/<id>)
 *      which provides full native YouTube controls (play/pause, scrubber, volume slider,
 *      captions, gear settings for quality, fullscreen) with zero scraping failure risk
 *      and full Terms of Service compliance.
 *   2. Generic Web Page Embedding: Can embed any modern live URL (Wikipedia, Desmos graphing
 *      calculator, documentation, HTML5 apps, etc.).
 *   3. Zero-Allocation Dual-Mode Virtualization:
 *      - Dormant Mode: Renders a lightweight Blend2D card with poster thumbnail, domain badge,
 *        and "Click to Play Live" callout. Consumes ~0 MB additional RAM.
 *      - Live Mode: Creates an OS child window hosting Edge Chromium (WebView2). Coordinates
 *        track the canvas viewport projection during pan/zoom in real time.
 *   4. Integrated Canvas Interaction Header:
 *      A 36px top title bar permits moving/dragging the card across the canvas with selection
 *      gizmos, toggling live mode, reloading, and opening in external browser.
 * =========================================================================================
 */

#pragma once

#include "core/overlay/interactive_overlay.hpp"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <blend2d/blend2d.h>

struct SDL_Window;

namespace Folio {

class WebOverlay : public IInteractiveOverlay {
public:
    /**
     * @brief Constructs a WebOverlay instance for a given URL.
     *
     * @param url Raw input URL (YouTube watch/share link, embed link, or standard web URL).
     * @param title Optional user-friendly display title (defaults to domain or YouTube video title).
     * @param parentWin Pointer to the host SDL_Window (used to resolve native HWND on Windows).
     */
    WebOverlay(std::string url, std::string title = "", SDL_Window* parentWin = nullptr);

    ~WebOverlay() override;

    // =========================================================================
    // IInteractiveOverlay INTERFACE
    // =========================================================================

    /**
     * @brief Initializes subsystem resources, loads vector typography, parses URLs,
     * and triggers asynchronous poster thumbnail retrieval.
     * @return True if initialized successfully.
     */
    [[nodiscard]] bool Initialize() override;

    /**
     * @brief Shuts down the live webview engine, destroys native OS child windows,
     * and releases graphics memory.
     */
    void Shutdown() override;

    /**
     * @brief Advances overlay state and checks for background thumbnail readiness.
     * @param nowMs Monotonically increasing engine time in milliseconds.
     * @param deltaTime Elapsed duration since last tick in seconds.
     */
    void OnUpdate(uint64_t nowMs, double deltaTime) override;

    /**
     * @brief Renders the overlay on the screen composite context.
     *
     * Working Process:
     *   1. If dormant: Renders the sleek Blend2D card, YouTube poster thumbnail, and play button.
     *   2. If live: Synchronizes native child window position to screenRect with SetWindowPos,
     *      and renders the top 36px interactive header bar.
     *
     * @param ctx Blend2D rendering context targeting the final composite screen surface.
     * @param screenRect Projected screen pixel bounding box for this card.
     */
    void OnRenderOverlay(BLContext& ctx, const OverlayRect& screenRect) override;

    /**
     * @brief Routes SDL3 input events to the overlay.
     *
     * Working Process:
     *   - Intercepts clicks on the top header bar (Live toggle, Reload, External browser).
     *   - If in dormant mode, a click on the content area activates the live WebView.
     *   - Returns true if the event was consumed by header controls.
     *
     * @param event The SDL_Event received from the application loop.
     * @param screenRect Current screen pixel bounds.
     * @return True if consumed; false to allow canvas manipulation/gizmo dragging.
     */
    [[nodiscard]] bool OnInputEvent(const SDL_Event& event, const OverlayRect& screenRect) override;

    /**
     * @brief Subsystem hooks for focus and viewport frustum transitions.
     */
    void OnFocusGained() override;
    void OnFocusLost() override;
    void OnVisibilityChanged(bool isVisible) override;

    /**
     * @brief Clones the overlay configuration for duplicated canvas objects.
     */
    [[nodiscard]] std::unique_ptr<IInteractiveOverlay> CloneOverlay() const override;

    // =========================================================================
    // WEBVIEW CONTROL METHODS
    // =========================================================================

    /**
     * @brief Activates the live Edge Chromium WebView2 child window.
     */
    void Activate();

    /**
     * @brief Deactivates the live web view, hiding the native child window.
     */
    void Deactivate();

    /**
     * @brief Toggles between live interactive mode and dormant poster mode.
     */
    void ToggleActive();

    /**
     * @brief Checks if the native webview is currently live.
     */
    [[nodiscard]] bool IsActive() const noexcept { return isActive; }

    /**
     * @brief Reloads the current web page or YouTube embed.
     */
    void Reload();

    /**
     * @brief Navigates the webview to a new destination URL.
     * @param newUrl The target URL to navigate to.
     */
    void Navigate(const std::string& newUrl);

    /**
     * @brief Launches the original URL in the operating system's default browser.
     */
    void OpenInExternalBrowser() const;

    /**
     * @brief Updates the parent SDL window handle.
     * @param win The SDL_Window pointer.
     */
    void SetParentWindow(SDL_Window* win) noexcept { parentWindow = win; }

    // =========================================================================
    // GETTERS & METADATA
    // =========================================================================

    [[nodiscard]] const std::string& GetUrl() const noexcept { return rawUrl; }
    [[nodiscard]] const std::string& GetEmbedUrl() const noexcept { return embedUrl; }
    [[nodiscard]] const std::string& GetTitle() const noexcept { return pageTitle; }
    [[nodiscard]] bool IsYouTube() const noexcept { return isYouTubeVideo; }
    [[nodiscard]] const std::string& GetYouTubeId() const noexcept { return youTubeId; }

private:
    /**
     * @brief Parses incoming URL, detects YouTube domains, extracts video IDs,
     * and constructs the canonical embed player URL.
     * @param url The input URL string.
     */
    void ParseUrl(const std::string& url);

    /**
     * @brief Fetches YouTube poster thumbnail in a background worker thread.
     */
    void FetchYouTubeThumbnailAsync();

    /**
     * @brief Adjusts native child window bounds to match projected screenRect.
     * @param screenRect Projected screen pixel bounds.
     */
    void UpdateChildWindowGeometry(const OverlayRect& screenRect);

    // =========================================================================
    // MEMBER VARIABLES
    // =========================================================================

    std::string rawUrl;
    std::string embedUrl;
    std::string pageTitle;
    std::string domainName;
    bool isYouTubeVideo = false;
    std::string youTubeId;

    SDL_Window* parentWindow = nullptr;
    void* nativeWebview = nullptr;      ///< webview_t handle
    void* nativeWidgetHwnd = nullptr;   ///< HWND child widget handle
    bool isActive = false;
    bool isVisibleInFrustum = true;

    // Header layout constants
    static constexpr int kHeaderHeight = 36;
    OverlayRect currentScreenRect{};
    OverlayRect lastScreenRect{};

    // Visual assets
    BLImage thumbnailImg;
    bool thumbnailLoaded = false;
    std::atomic<bool> isFetchingThumbnail{false};

    // Vector typography
    BLFont titleFont;
    BLFont domainFont;
    BLFont btnFont;
    bool fontsLoaded = false;
};

} // namespace Folio
