#pragma once
/**
 * @file text_editor_state.hpp
 * @brief Slint-proof, headless text editor controller and caret engine.
 *
 * Implements an interactive text editing controller decoupled from any UI toolkit.
 * Receives raw character input and keycodes forwarded from the windowing layer
 * (SDL3, Slint, or Qt) and drives the text document model (TextBoxObject).
 *
 * Mathematical & Typographical Model:
 * -----------------------------------
 * - Lines are computed with explicit line breaks ('\n') and word wrapping within worldWidth.
 * - Cursor position is represented as a character index [0, totalChars].
 * - Caret world coordinates (worldX, worldY) are computed using Blend2D font metrics
 *   (ascent, descent, advance) provided by FontManager.
 * - Selection range is defined by [min(anchor, cursor), max(anchor, cursor)].
 */

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <blend2d/blend2d.h>
#include "core/spatial/aabb.hpp"
#include "core/engine/stroke_smoother.hpp"

namespace Folio {

class TextBoxObject;

/**
 * @struct TextLineLayout
 * @brief Metrics for a single formatted line of text within the text container.
 */
struct TextLineLayout {
    size_t startCharIndex = 0;  ///< Starting character index in flattened text
    size_t charCount      = 0;  ///< Number of characters in this line
    double localY         = 0.0;///< Top Y position relative to box worldY (mm)
    double height         = 0.0;///< Line height (ascent + descent + lineGap) (mm)
    double baselineY      = 0.0;///< Baseline relative to box worldY (mm)
    double width          = 0.0;///< Total advance width of line (mm)
    std::string text;           ///< Slice of text for this line
};

/**
 * @class TextEditorState
 * @brief Headless text controller managing caret, selection, and keystrokes.
 */
class TextEditorState {
public:
    TextEditorState();
    ~TextEditorState() = default;

    /**
     * @brief Binds a target TextBoxObject for editing.
     * @param target Pointer to the text box object (or nullptr to detach).
     */
    void Attach(TextBoxObject* target);

    /**
     * @brief Detaches the current text box and finalizes layout.
     */
    void Detach();

    [[nodiscard]] bool IsActive() const noexcept { return m_target != nullptr; }
    [[nodiscard]] TextBoxObject* GetTarget() const noexcept { return m_target; }

    // =========================================================================
    // INPUT HANDLERS (Decoupled from UI frameworks)
    // =========================================================================

    /**
     * @brief Inserts a UTF-8 character string at the current cursor position.
     * @param utf8Text Text received from OS text input event.
     */
    void OnTextInput(const std::string& utf8Text);

    /**
     * @brief Processes navigation and editing keys.
     * @param keycode SDL keycode (e.g. SDLK_BACKSPACE, SDLK_RETURN, SDLK_LEFT).
     * @param keymod Active modifier bitmask (Ctrl, Shift, Alt).
     */
    void OnKeyDown(int32_t keycode, uint16_t keymod);

    /**
     * @brief Handles mouse down on the canvas to position cursor or start selection.
     * @param worldX World X coordinate of click in millimeters.
     * @param worldY World Y coordinate of click in millimeters.
     * @param shiftSelect True if Shift was held to extend selection.
     */
    void OnMouseDown(double worldX, double worldY, bool shiftSelect = false);

    /**
     * @brief Handles mouse drag to expand text selection range.
     * @param worldX World X coordinate in millimeters.
     * @param worldY World Y coordinate in millimeters.
     */
    void OnMouseDrag(double worldX, double worldY);

    /**
     * @brief Advances caret blinking phase using application time.
     * @param currentSec Current monotonic time in seconds.
     */
    void UpdateBlink(double currentSec);

    // =========================================================================
    // CARET & SELECTION GEOMETRY
    // =========================================================================

    [[nodiscard]] bool HasSelection() const noexcept {
        return m_selectionAnchor != m_cursorIndex;
    }

    [[nodiscard]] size_t SelectionStart() const noexcept {
        return (m_selectionAnchor < m_cursorIndex) ? m_selectionAnchor : m_cursorIndex;
    }

    [[nodiscard]] size_t SelectionEnd() const noexcept {
        return (m_selectionAnchor > m_cursorIndex) ? m_selectionAnchor : m_cursorIndex;
    }

    void ClearSelection() noexcept {
        m_selectionAnchor = m_cursorIndex;
    }

    [[nodiscard]] bool IsCaretVisible() const noexcept {
        return m_caretVisible;
    }

    /**
     * @brief Computes world-space position of the caret for Blend2D drawing.
     * @return Point2D Top-left point of caret line in world mm.
     */
    [[nodiscard]] Point2D GetCursorWorldPos() const;

    /**
     * @brief Computes height of caret in world millimeters.
     */
    [[nodiscard]] double GetCaretHeight() const;

    /**
     * @brief Computes list of world-space bounding boxes covering the selection.
     */
    [[nodiscard]] std::vector<AABB> GetSelectionBoxes() const;

    /**
     * @brief Recomputes line wrapping and layout using current font metrics.
     */
    void ReflowLayout();

    [[nodiscard]] const std::vector<TextLineLayout>& GetLines() const noexcept {
        return m_lines;
    }

private:
    void DeleteSelection();
    void MoveCursorTo(size_t newIndex, bool extendSelection);
    size_t FindClosestCharIndex(double worldX, double worldY) const;

    TextBoxObject* m_target = nullptr;
    size_t m_cursorIndex = 0;
    size_t m_selectionAnchor = 0;

    double m_lastBlinkTimeSec = 0.0;
    bool m_caretVisible = true;

    std::vector<TextLineLayout> m_lines;
};

} // namespace Folio
