#pragma once
/**
 * =========================================================================================
 * @file core/clipboard/clipboard_manager.hpp
 * @brief Global Coordinator for Multi-Format Clipboard Serialization, OLE Publishing & Ingestion
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PATTERNS:
 * --------------------------------
 * ClipboardManager centralizes all clipboard operations across FolioNote.
 * It implements:
 * 1. Single Responsibility: Decouples CanvasEngine and DocumentSession from platform clipboard APIs.
 * 2. Multi-Format Flavor Publishing (Producer):
 *    When the user copies canvas items (strokes, images, videos, audio, text boxes):
 *      - Generates lossless native deep copies + compressed .ink binary stream.
 *      - Generates scalable W3C SVG XML for Microsoft OneNote, Word, Figma, Illustrator.
 *      - Renders an in-memory high-DPI transparent PNG bitmap via Blend2D for Discord, Slack, Paint.
 *      - Generates plain text / URLs for video containers or text items.
 *      - Publishes all representations to the OS clipboard (via SDL / platform OLE).
 *
 * 3. Smart Priority Ingestion (Consumer):
 *    When pasting, it inspects the available formats in decreasing order of fidelity:
 *      - Priority 1: Native FolioNote stream (lossless vector ink, styles, full fidelity).
 *      - Priority 2: Windows Ink / ISF (converts OneNote ink to native StrokeObjects).
 *      - Priority 3: Vector SVG (parses paths and polylines).
 *      - Priority 4: Image data (creates an ImageObject).
 *      - Priority 5: URLs / Video links / Audio paths (creates VideoObject or AudioObject).
 *      - Priority 6: Plaintext (creates a TextBoxObject).
 */

#include <vector>
#include <memory>
#include <string>
#include <functional>

#include "core/clipboard/clipboard_data_package.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"

class DocumentSession;
class CanvasEngine;

namespace Folio {

class ClipboardManager {
public:
    /**
     * @brief Singleton access to the global ClipboardManager instance.
     */
    static ClipboardManager& Instance();

    // =========================================================================
    // Producer API (Copy / Cut)
    // =========================================================================

    /**
     * @brief Packages the given canvas objects into a multi-format package and pushes to the OS clipboard.
     *
     * Working Process:
     *   1. Clones all selected objects in memory.
     *   2. Computes the collective bounding box (AABB union).
     *   3. Generates vector SVG XML for vector apps / OneNote.
     *   4. Rasterizes a high-DPI transparent PNG via Blend2D for general apps.
     *   5. Extracts text/URL representations where available.
     *   6. Writes available flavors to the OS clipboard tray.
     *
     * @param objects Vector of CanvasObjects to copy.
     * @return true if successfully packaged and copied; false if objects empty.
     */
    bool CopyObjects(const std::vector<std::shared_ptr<CanvasObject>>& objects);

    /**
     * @brief Cuts the given canvas objects: copies to clipboard and removes them from the page with undo.
     *
     * @param objects Vector of CanvasObjects to cut.
     * @param session Active DocumentSession for history and page mutation.
     * @return true if successfully cut.
     */
    bool CutObjects(const std::vector<std::shared_ptr<CanvasObject>>& objects, DocumentSession& session);

    // =========================================================================
    // Consumer API (Paste)
    // =========================================================================

    /**
     * @brief Inspects the clipboard and pastes content onto the active page centered at target coordinates.
     *
     * Ingestion Hierarchy:
     *   1. If in-memory package has cloned objects -> clones with fresh UIDs and centers at (targetWorldX, targetWorldY).
     *   2. If OS clipboard has raster image -> imports image data into an ImageObject.
     *   3. If OS clipboard has text -> checks if text is a media URL (YouTube/video/audio) or plain text,
     *      and instantiates the corresponding object.
     *
     * @param session Active DocumentSession.
     * @param engine Active CanvasEngine (for viewport and rendering dirty marks).
     * @param targetWorldX Target X coordinate in world millimeters.
     * @param targetWorldY Target Y coordinate in world millimeters.
     * @return Vector of newly created and inserted CanvasObjects.
     */
    std::vector<std::shared_ptr<CanvasObject>> Paste(DocumentSession& session,
                                                    CanvasEngine& engine,
                                                    double targetWorldX,
                                                    double targetWorldY);

    /**
     * @brief Checks if the clipboard contains any pasteable content (internal objects, image, text, or URL).
     * @return true if paste is available.
     */
    [[nodiscard]] bool HasPasteableContent() const;

    // =========================================================================
    // Internal Package Inspection
    // =========================================================================

    /**
     * @brief Access the latest in-memory clipboard package.
     */
    [[nodiscard]] const ClipboardDataPackage& GetCurrentPackage() const noexcept {
        return currentPackage;
    }

private:
    ClipboardManager() = default;
    ~ClipboardManager() = default;
    ClipboardManager(const ClipboardManager&) = delete;
    ClipboardManager& operator=(const ClipboardManager&) = delete;

    // Helper generators
    std::string GenerateSvgXml(const std::vector<std::shared_ptr<CanvasObject>>& objects, const AABB& bounds);
    std::vector<uint8_t> RenderToPng(const std::vector<std::shared_ptr<CanvasObject>>& objects, const AABB& bounds, double dpi = 192.0, BLImage* outBlImage = nullptr);
    std::string ExtractPlainText(const std::vector<std::shared_ptr<CanvasObject>>& objects);

    ClipboardDataPackage currentPackage;
    uint32_t lastClipboardSequenceNumber = 0; ///< Tracks OS clipboard sequence number to detect external changes
};

} // namespace Folio
