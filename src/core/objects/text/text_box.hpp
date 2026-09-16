#pragma once
/**
 * @file text_box.hpp
 * @brief Canvas text box object with rich content model and dual live/baked rendering.
 *
 * TextBoxObject holds a sequence of TextRun spans forming a rich text document.
 * The position and size define a bounding box on the canvas (world mm).
 *
 * Two-Layer Rendering Architecture (Phase 2):
 * ─────────────────────────────────────────────────────────────────────────────
 *  LIVE LAYER (isEditing == true):
 *    Rendered every frame by an ImGui-based text editor overlay positioned
 *    over the canvas at the correct screen coordinates. Font size is scaled
 *    by the current canvas zoom so physical text size is invariant.
 *    The live layer also handles cursor, selection highlight, IME input,
 *    and incremental run splitting/merging on each keystroke.
 *
 *  BAKED LAYER (isEditing == false, isDirty == false):
 *    A BLImage PRGB32 snapshot produced by BakeContent(). The engine blits
 *    this image directly (no font layout per frame). On selection + double-click,
 *    isEditing = true and the live layer takes over.
 *    isDirty = true flags that BakeContent() must be called before next render.
 *
 *  Zoom Handling:
 *    The baked image is rendered at a fixed bake DPI (72 dpi * bakeScale).
 *    When the canvas zoom changes significantly (> 2× from bake zoom),
 *    the engine should set isDirty = true to re-bake at the new zoom.
 *    This prevents blurry baked text at high zoom levels.
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Current Phase (Scaffold):
 *   - Fields for the full rich text model are present.
 *   - Render() draws a placeholder border box (existing behavior preserved).
 *   - BakeContent() is documented but not yet implemented.
 *   - Phase 2 will implement the ImGui editor overlay and Blend2D font layout.
 *
 * Scalability:
 *   New text features (text direction, ruby/phonetic, column layout) only
 *   require changes inside this file and the Phase 2 text layout engine.
 *   The CanvasObject interface is not affected.
 */

#include <string>
#include <vector>
#include <memory>
#include <numeric>
#include <algorithm>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/objects/text/text_run.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @brief Rich text box on the canvas.
 *
 * Contains a vector<TextRun> as the authoritative content model. The plainText()
 * accessor flattens all runs for search indexing and clipboard operations.
 */
class TextBoxObject : public CanvasObject {
public:
    // =========================================================================
    // POSITION & SIZE
    // =========================================================================

    double worldX      = 0.0;    ///< Top-left X (world mm)
    double worldY      = 0.0;    ///< Top-left Y (world mm)
    double worldWidth  = 80.0;   ///< Box width (world mm) — resizable
    double worldHeight = 24.0;   ///< Box height (world mm) — grows with content

    // =========================================================================
    // LEGACY & INTEROP PROPERTIES (Synchronized with runs for Phase 1/Phase 2 bridge)
    // =========================================================================
    std::string text = "";             ///< Plain text representation (kept in sync with runs)
    std::string fontFamily = "Segoe UI";///< Default font family
    float fontSize = 16.0f;            ///< Default font size in points
    BLRgba32 textColor{0xFFFFFFFF};    ///< Default text color (opaque white)

    // =========================================================================
    // RICH CONTENT MODEL
    // =========================================================================

    /**
     * @brief Ordered sequence of styled text spans forming the document content.
     *
     * Invariants (maintained by the editor in Phase 2):
     *   - Adjacent runs with identical styling should be merged (call CompactRuns()).
     *   - No run should have an empty text string.
     *   - The document is never truly empty: at least one run with empty text
     *     is kept as a cursor anchor when the box has no content yet.
     */
    std::vector<TextRun> runs;

    // =========================================================================
    // EDITING STATE
    // =========================================================================

    bool isEditing = false;     ///< True while the ImGui text editor overlay is active
    bool isWrap    = true;      ///< Word-wrap within worldWidth

    // =========================================================================
    // BAKED LAYER (Phase 2)
    // =========================================================================

    /**
     * @brief Dirty flag — true means cachedBake is stale and must be rebuilt.
     *
     * Set to true whenever: runs change, worldWidth changes, zoom changes
     * significantly from bakeZoom. Cleared by BakeContent().
     */
    bool isDirty = true;

    /**
     * @brief Zoom level at which cachedBake was last rendered.
     *
     * Used to detect when a re-bake is needed (zoom changed > 2× from bakeZoom).
     * Phase 2 field — not yet used.
     */
    double bakeZoom = 1.0;

    /**
     * @brief Cached rasterized snapshot of the text content.
     *
     * Phase 2: populated by BakeContent(). The engine blits this BLImage
     * when isEditing == false && isDirty == false.
     * Currently unused (empty BLImage).
     */
    BLImage cachedBake;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    TextBoxObject() {
        type = ObjectType::Text;
        // Initialize with one empty run so the cursor has a valid anchor
        runs.push_back(TextRun{});
        UpdateBounds();
    }

    TextBoxObject(double x, double y, double w, double h) :
        worldX(x), worldY(y), worldWidth(w), worldHeight(h)
    {
        type = ObjectType::Text;
        runs.push_back(TextRun{});
        UpdateBounds();
    }

    // =========================================================================
    // CONTENT HELPERS
    // =========================================================================

    /**
     * @brief Flattens all runs into a single plain-text string.
     *
     * Used by:
     *   - Search indexer (NotebookSearchIndex)
     *   - Clipboard copy operations
     *   - Export serializers (plain text fallback)
     *
     * @return Concatenation of all run text fields in order.
     */
    [[nodiscard]] std::string PlainText() const {
        std::string result;
        result.reserve(256);
        for (const auto& run : runs) {
            result += run.text;
        }
        if (result.empty() && !text.empty()) {
            return text;
        }
        return result;
    }

    /**
     * @brief Synchronizes legacy string buffer into rich TextRun model.
     */
    void SyncTextToRuns() {
        if (runs.empty()) {
            TextRun r;
            r.text = text;
            r.fontFamily = fontFamily;
            r.fontSize = fontSize;
            r.color = textColor;
            runs.push_back(r);
        } else if (runs.size() == 1) {
            runs[0].text = text;
            runs[0].fontFamily = fontFamily;
            runs[0].fontSize = fontSize;
            runs[0].color = textColor;
        }
    }

    /**
     * @brief Merges adjacent runs that have identical styling.
     *
     * Called by the text editor after each batch of styling operations
     * to keep the run list compact. O(n) pass over the runs vector.
     */
    void CompactRuns() {
        if (runs.size() < 2) return;
        std::vector<TextRun> compacted;
        compacted.reserve(runs.size());
        compacted.push_back(runs[0]);
        for (size_t i = 1; i < runs.size(); ++i) {
            if (!runs[i].text.empty() && compacted.back().SameStyleAs(runs[i])) {
                compacted.back().text += runs[i].text;
            } else {
                if (!runs[i].text.empty()) compacted.push_back(runs[i]);
            }
        }
        runs = std::move(compacted);
    }

    /**
     * @brief Returns total character count across all runs.
     */
    [[nodiscard]] size_t CharCount() const noexcept {
        size_t n = 0;
        for (const auto& r : runs) n += r.text.size();
        return n;
    }

    // =========================================================================
    // BAKED CONTENT (PHASE 2 STUB)
    // =========================================================================

    /**
     * @brief Renders the rich text into cachedBake at the given zoom level.
     *
     * STUB — Phase 2 implementation will:
     *  1. Compute layout using Blend2D BLFont for each TextRun
     *  2. Render into a BLImage of size (worldWidth * zoom * 96dpi, worldHeight * zoom * 96dpi)
     *  3. Store result in cachedBake, set isDirty = false, bakeZoom = zoom
     *
     * The bake must be invalidated (isDirty = true) when:
     *   - Any run's text or style changes
     *   - worldWidth changes (re-wraps text)
     *   - Canvas zoom changes > 2× from bakeZoom
     *
     * @param zoom Current canvas zoom (world_mm / screen_pixel ratio)
     */
    void BakeContent(double zoom) {
        bakeZoom = zoom;
        isDirty  = false;
        // TODO Phase 2: implement Blend2D BLFont-based rasterization
        // cachedBake = BLImage(renderWidth, renderHeight, BL_FORMAT_PRGB32);
        // BLContext ctx(cachedBake);
        // ... layout and draw each TextRun ...
        // ctx.end();
    }

    // =========================================================================
    // BOUNDS & SPATIAL
    // =========================================================================

    void UpdateBounds() override {
        BLPoint p[4] = {
            transform.map_point(worldX,              worldY),
            transform.map_point(worldX + worldWidth, worldY),
            transform.map_point(worldX + worldWidth, worldY + worldHeight),
            transform.map_point(worldX,              worldY + worldHeight)
        };
        double minX = p[0].x, maxX = p[0].x;
        double minY = p[0].y, maxY = p[0].y;
        for (int i = 1; i < 4; ++i) {
            minX = (std::min)(minX, p[i].x);
            maxX = (std::max)(maxX, p[i].x);
            minY = (std::min)(minY, p[i].y);
            maxY = (std::max)(maxY, p[i].y);
        }
        bounds = AABB(minX, minY, maxX, maxY);
    }

    bool HitTest(double wx, double wy) const override {
        return bounds.Contains(wx, wy);
    }

    bool Intersects(const AABB& sel) const override {
        return bounds.Intersects(sel);
    }

    // =========================================================================
    // TRANSFORM — full resize + translate
    // =========================================================================

    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    void BakeTransform() override {
        if (std::abs(transform.m01) < 1e-6 && std::abs(transform.m10) < 1e-6) {
            if (transform.m00 == 1.0 && transform.m11 == 1.0 &&
                transform.m20 == 0.0 && transform.m21 == 0.0) return;

            double p0x = transform.m00 * worldX + transform.m20;
            double p0y = transform.m11 * worldY + transform.m21;
            double p1x = transform.m00 * (worldX + worldWidth)  + transform.m20;
            double p1y = transform.m11 * (worldY + worldHeight) + transform.m21;

            worldX      = (std::min)(p0x, p1x);
            worldY      = (std::min)(p0y, p1y);
            worldWidth  = (std::max)(0.5, std::abs(p1x - p0x));
            worldHeight = (std::max)(0.5, std::abs(p1y - p0y));

            transform = BLMatrix2D::make_identity();
            isDirty   = true;    // Re-bake needed after resize
            UpdateBounds();
        }
    }

    // =========================================================================
    // RENDERING (CURRENT: placeholder border; Phase 2: blit cachedBake)
    // =========================================================================

    /**
     * @brief Renders the text box on the canvas.
     *
     * CURRENT behavior (scaffold):
     *   Draws a semi-transparent background and dashed border. No text rendered
     *   until Phase 2 BakeContent() is implemented.
     *
     * PHASE 2 behavior:
     *   if (isEditing) → ImGui overlay handles rendering; Blend2D draws cursor box
     *   else if (isDirty) → call BakeContent(viewport.zoom); then blit
     *   else → blit cachedBake at worldX, worldY with worldWidth × worldHeight
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override {
        if (!isVisible) return;

        ctx.save();
        ctx.apply_transform(transform);

        // Background (current placeholder)
        ctx.set_fill_style(BLRgba32(0x18, 0x1A, 0x20,
                                    static_cast<uint8_t>(opacity * 200)));
        ctx.fill_rect(worldX, worldY, worldWidth, worldHeight);

        // Border
        double borderW = 0.5 / (viewport.zoom > 0.001 ? viewport.zoom : 1.0);
        ctx.set_stroke_style(BLRgba32(0x3E, 0x44, 0x52,
                                      static_cast<uint8_t>(opacity * 255)));
        ctx.set_stroke_width(borderW);
        ctx.stroke_rect(worldX, worldY, worldWidth, worldHeight);

        // TODO Phase 2: replace with BakeContent() + cachedBake blit
        // if (!isEditing) {
        //     if (isDirty) const_cast<TextBoxObject*>(this)->BakeContent(viewport.zoom);
        //     ctx.blit_image(BLPoint(worldX, worldY), cachedBake);
        // }

        ctx.restore();
    }

    // =========================================================================
    // DUPLICATION & PERSISTENCE
    // =========================================================================

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<TextBoxObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}
};

} // namespace Folio
