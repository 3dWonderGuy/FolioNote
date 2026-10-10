#pragma once
/**
 * =========================================================================================
 * @file ui/framework/ui_overlay_host.hpp
 * @brief Unified Floating Overlay, Toast, and Modal Host for FolioNote UI
 * =========================================================================================
 *
 * GENERAL ARCHITECTURE:
 * ---------------------
 * Centralizes all non-docked floating UI elements:
 * 1. Stacked non-intrusive toast notifications with auto-dismiss countdowns.
 * 2. Animated modal dialogs with backdrop darkening and spring entrance scaling.
 * 3. Contextual anchored flyouts (color pickers, stroke size popovers).
 */

#include "imgui.h"
#include "ui/framework/ui_tokens.hpp"
#include "ui/framework/ui_animation_manager.hpp"
#include "ui/framework/ui_shape_manager.hpp"
#include "ui/framework/ui_builder.hpp"
#include <string>
#include <vector>
#include <functional>

namespace Folio::UI {

/**
 * @enum UIToastType
 * @brief Severity / semantic type for toast notifications.
 */
enum class UIToastType {
    Info,
    Success,
    Warning,
    Error
};

/**
 * @struct ToastMessage
 * @brief Descriptor for a live toast notification.
 */
struct ToastMessage {
    std::string id;
    UIToastType type = UIToastType::Info;
    std::string title;
    std::string message;
    float remainingSeconds = 4.0f;
    float initialSeconds = 4.0f;
};

/**
 * @struct ActiveModal
 * @brief Descriptor for an open modal dialog window.
 */
struct ActiveModal {
    std::string id;
    std::string title;
    std::function<void()> renderFunc;
    float width = 480.0f;
    bool isOpen = false;
};

/**
 * @class UIOverlayHost
 * @brief Centralized coordinator for toasts, modals, and flyouts.
 */
class UIOverlayHost {
public:
    static UIOverlayHost& Instance();

    /**
     * @brief Posts a non-intrusive toast notification.
     *
     * @param type Semantic type (Success, Info, Warning, Error).
     * @param title Short heading text.
     * @param message Detailed status description.
     * @param durationSeconds Time before auto-dismiss (default 4.0s).
     */
    void ShowToast(UIToastType type, const std::string& title, const std::string& message, float durationSeconds = 4.0f);

    /**
     * @brief Presents an animated modal dialog window.
     *
     * @param id Unique modal ID.
     * @param title Modal window title.
     * @param renderFunc Callback rendering modal content.
     * @param width Preferred width in pixels (default 480px).
     */
    void OpenModal(const std::string& id, const std::string& title, std::function<void()> renderFunc, float width = 480.0f);

    /**
     * @brief Closes the currently active modal dialog.
     */
    void CloseModal();

    /**
     * @brief Renders all active overlays, modals, and toasts.
     * Must be called at the end of the frame inside the main UI loop.
     *
     * @param dt Frame delta time in seconds.
     */
    void Render(float dt);

private:
    UIOverlayHost() = default;
    ~UIOverlayHost() = default;

    std::vector<ToastMessage> toasts_;
    ActiveModal currentModal_;
};

} // namespace Folio::UI
