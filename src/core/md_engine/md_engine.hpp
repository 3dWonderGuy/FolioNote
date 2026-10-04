#pragma once

#include <string>
#include <vector>
#include <memory>
#include <blend2d/blend2d.h>

#include "core/md_engine/md_types.hpp"
#include "core/md_engine/md_parser.hpp"
#include "core/md_engine/md_layout_engine.hpp"
#include "core/md_engine/md_editor_state.hpp"

namespace Folio {

/**
 * @class MdEngine
 * @brief Master Markdown Engine coordinating AST parsing, physical millimeter layout,
 * interactive text editing, and 3-tier compositing pipeline rendering.
 */
class MdEngine {
public:
    MdEditorState editor;
    std::vector<MdBlock> blocks;
    MdStyleConfig config;
    double totalDocumentHeightMm = 0.0;
    bool isDirty = true;

    MdEngine();
    ~MdEngine() = default;

    // Non-copyable, movable
    MdEngine(const MdEngine&) = delete;
    MdEngine& operator=(const MdEngine&) = delete;
    MdEngine(MdEngine&&) noexcept = default;
    MdEngine& operator=(MdEngine&&) noexcept = default;

    /**
     * @brief Loads a Markdown document and triggers AST parse and layout reflow.
     */
    void LoadMarkdown(const std::string& text);

    /**
     * @brief Retrieves the raw Markdown document text.
     */
    [[nodiscard]] const std::string& GetMarkdownText() const noexcept {
        return editor.GetText();
    }

    /**
     * @brief Triggers full re-parsing and typography reflow.
     */
    void Reflow();

    // =========================================================================
    // 3-TIER COMPOSITOR INTEGRATION
    // =========================================================================

    /**
     * @brief Layer 1 (Baked / Static Cache): Renders typography, blocks, tables,
     * and paper background into a Blend2D graphics context.
     * 
     * @param ctx Blend2D rendering context.
     * @param originX_mm Canvas top-left X position in physical millimeters.
     * @param originY_mm Canvas top-left Y position in physical millimeters.
     * @param viewW_mm Visible viewport width in physical millimeters.
     * @param viewH_mm Visible viewport height in physical millimeters.
     * @param invert Dark canvas color inversion.
     */
    void RenderToBlend2D(
        BLContext& ctx,
        double originX_mm,
        double originY_mm,
        double viewW_mm,
        double viewH_mm,
        bool invert = false
    );

    /**
     * @brief Layer 2 (Live / Ephemeral): Renders text selection highlight rects
     * and the 120 FPS blinking caret.
     * 
     * @param ctx Blend2D live inking context.
     * @param originX_mm Canvas top-left X position in physical millimeters.
     * @param originY_mm Canvas top-left Y position in physical millimeters.
     * @param currentSec Current monotonic time for caret blink phase.
     */
    void RenderLiveLayer(
        BLContext& ctx,
        double originX_mm,
        double originY_mm,
        double currentSec
    );

    // =========================================================================
    // INPUT HANDLING
    // =========================================================================

    void OnMouseDown(double localX_mm, double localY_mm, bool shiftSelect = false);
    void OnMouseDrag(double localX_mm, double localY_mm);
    void OnTextInput(const std::string& text);
    void OnKeyDown(int32_t keycode, uint16_t keymod);
};

} // namespace Folio
