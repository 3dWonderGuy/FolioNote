#pragma once

#include <string>
#include <vector>
#include <memory>
#include "imgui.h"
#include <blend2d/blend2d.h>
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include "core/md_engine/md_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "input/input_state_machine.hpp"
#include "app/theme_manager.hpp"

namespace Folio {

/**
 * @class MdEditorView
 * @brief High-performance dual-pane Markdown editor and physical paper preview view.
 *
 * Implements FolioNote's Universal Layered Architecture for Markdown documents:
 * - Left Pane: Clean typography raw Markdown code editor with syntax helpers.
 * - Right Pane: Physical millimeter paper layout rasterized at 120 FPS via MdEngine and Blend2D.
 * - Quick Toolbar: Direct formatting shortcuts (Bold, Italic, Headings, Lists, Tables, LaTeX).
 * - Full synchronization with CanvasPage::dedicatedMdContent and SQLite auto-save.
 */
class MdEditorView {
public:
    static inline MdEditorView* s_activeInstance = nullptr;

    MdEngine engine;

    enum class ViewMode {
        SplitView,    // Raw Editor on Left, Live Paper Preview on Right
        EditorOnly,   // Distraction-free writing mode
        PreviewOnly   // Paper reading & presentation mode
    };
    ViewMode currentMode = ViewMode::SplitView;

    // View split ratio (0.2 to 0.8)
    float splitRatio = 0.50f;

    // Live Paper Preview Render Surface (Blend2D -> OpenGL texture)
    GLuint glTexture = 0;
    BLImage previewSurface;
    int surfaceW = 0;
    int surfaceH = 0;
    int allocatedCapacityW = 0;
    int allocatedCapacityH = 0;
    bool needsRebake = true;

    // Preview Camera / Pan-Zoom State
    float previewZoom = 1.0f;
    float previewScrollY = 0.0f;
    float maxScrollY = 0.0f;
    bool isMiddlePanning = false;
    ImVec2 middlePanLastPos{0.0f, 0.0f};

    // Tracking currently mounted page
    std::string activePageGuid;

    // Editor buffer (used for ImGui multiline input)
    std::string editorBuffer;

    MdEditorView();
    ~MdEditorView();

    MdEditorView(const MdEditorView&) = delete;
    MdEditorView& operator=(const MdEditorView&) = delete;

    void InitGL();
    void CleanupGL();

    /**
     * @brief Mounts an active CanvasPage and synchronizes its dedicatedMdContent buffer.
     */
    void MountPage(CanvasPage* page);

    /**
     * @brief Commits the editor buffer into the active page and marks it modified.
     */
    void CommitToPage(CanvasPage* page);

    /**
     * @brief Primary frame render function, matching FolioNote view signature.
     */
    void Render(
        float canvasX, float canvasW,
        float screenH, float titleBarH, float ribbonH,
        DocumentSession& session,
        InputStateMachine& inputSM,
        ThemeManager& themeManager,
        bool inkColorInverted
    );

    // =========================================================================
    // QUICK FORMATTING ACTIONS
    // =========================================================================
    void InsertFormattingPrefix(const std::string& prefix);
    void WrapSelectionWith(const std::string& prefix, const std::string& suffix);
    void InsertCodeBlockTemplate(const std::string& lang = "cpp");
    void InsertTableTemplate(int rows = 3, int cols = 3);
    void InsertMathBlockTemplate();
    void InsertTaskItem();

private:
    void RenderToolbar(ThemeManager& themeManager);
    void RenderEditorPane(float width, float height, ThemeManager& themeManager, CanvasPage* activePage);
    void RenderPreviewPane(float width, float height, ThemeManager& themeManager, bool invert);
    void RebakePreviewSurface(int width, int height, bool invert);
};

} // namespace Folio
