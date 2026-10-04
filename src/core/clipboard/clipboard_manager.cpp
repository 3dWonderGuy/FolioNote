/**
 * =========================================================================================
 * @file core/clipboard/clipboard_manager.cpp
 * @brief Implementation of Multi-Format Clipboard Coordinator
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INVERSION OF CONTROL:
 * --------------------------------------------
 * ClipboardManager isolates all platform clipboard communication away from CanvasEngine.
 *
 * It provides:
 * 1. Multi-format packaging on Copy:
 *    - Deep clone of objects (preserves full parameters).
 *    - Vector SVG output (<polyline>, <rect>, <text> tags) formatted for Microsoft OneNote,
 *      Word, PowerPoint, and Illustrator.
 *    - High-resolution transparent PNG bitmap (rendered via Blend2D at 192 DPI) for
 *      universal paste into Discord, Slack, Paint, Teams, browser chat, etc.
 *    - Plaintext / URL extraction (video URLs, audio paths, textbox text) pushed to SDL/OS clipboard.
 * 2. Smart Ingestion on Paste:
 *    - If internal package is available, pastes clones centered at target world coordinates.
 *    - Else if OS clipboard has an image (PNG/JPEG/BMP/WEBP), creates an ImageObject.
 *    - Else if OS clipboard has text:
 *        * Checks for YouTube URLs -> creates VideoObject.
 *        * Checks for audio/video file paths -> creates AudioObject or VideoObject.
 *        * Falls back to TextBoxObject with the plain text.
 */

#include "core/clipboard/clipboard_manager.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#include <SDL3/SDL.h>

#include "core/document/document_session.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/history/canvas_command.hpp"
#include "core/objects/ink_container/ink_container.hpp"
#include "core/objects/text/text_box.hpp"
#include "core/objects/media/images/image_container.hpp"
#include "core/objects/media/videos/video_container.hpp"
#include "core/objects/media/audio/audio_container.hpp"
#include "core/overlay/web_overlay.hpp"
#include "utils/logger.hpp"
#include "utils/guid_generator.hpp"
#include "utils/uid_generator.hpp"

#if defined(_WIN32)
#include "core/clipboard/platform/win32_clipboard.hpp"
#endif

namespace Folio {

ClipboardManager& ClipboardManager::Instance() {
    static ClipboardManager instance;
    return instance;
}

// =============================================================================
// PRODUCER IMPLEMENTATION (COPY / CUT)
// =============================================================================

bool ClipboardManager::CopyObjects(const std::vector<std::shared_ptr<CanvasObject>>& objects) {
    if (objects.empty()) {
        return false;
    }

    currentPackage.Clear();
    currentPackage.clonedObjects.reserve(objects.size());

    // 1. In-Memory Deep Clone & AABB Union Calculation
    AABB totalBox;
    bool hasBounds = false;

    for (const auto& obj : objects) {
        if (!obj) continue;
        auto cloned = std::shared_ptr<CanvasObject>(obj->Clone().release());
        if (cloned) {
            currentPackage.clonedObjects.push_back(cloned);
            if (!hasBounds) {
                totalBox = obj->bounds;
                hasBounds = true;
            } else {
                totalBox.Merge(obj->bounds);
            }
        }
    }

    if (currentPackage.clonedObjects.empty()) {
        return false;
    }

    currentPackage.objectCount = static_cast<uint32_t>(currentPackage.clonedObjects.size());
    currentPackage.totalBounds = totalBox;
    currentPackage.originX = (totalBox.minX + totalBox.maxX) * 0.5;
    currentPackage.originY = (totalBox.minY + totalBox.maxY) * 0.5;

    // 2. Generate SVG Representation (OneNote / Office / Vector Apps)
    currentPackage.svgXml = GenerateSvgXml(currentPackage.clonedObjects, totalBox);

    // 3. Render High-Resolution Raster Image (PNG) with Blend2D (Discord / Paint / Slack)
    // Optimization for single ImageObject: retain pristine source bytes without re-encoding
    BLImage blSurface;
    if (currentPackage.clonedObjects.size() == 1) {
        if (auto imgObj = std::dynamic_pointer_cast<ImageObject>(currentPackage.clonedObjects[0])) {
            if (!imgObj->embeddedData.empty() && imgObj->imageFormat == ImageFormat::PNG) {
                currentPackage.pngBytes = imgObj->embeddedData;
            } else if (!imgObj->cachedBlImage.is_empty()) {
                BLArray<uint8_t> pngBuffer;
                BLImageCodec codec;
                codec.find_by_name("PNG");
                if (codec.is_valid()) {
                    imgObj->cachedBlImage.write_to_data(pngBuffer, codec);
                    currentPackage.pngBytes.assign(pngBuffer.data(), pngBuffer.data() + pngBuffer.size());
                }
            }
            blSurface = imgObj->cachedBlImage;
        }
    }

    if (currentPackage.pngBytes.empty()) {
        currentPackage.pngBytes = RenderToPng(currentPackage.clonedObjects, totalBox, 192.0, &blSurface);
    }

    // 4. Extract Plaintext / URLs
    currentPackage.plainText = ExtractPlainText(currentPackage.clonedObjects);

    // 5. Publish to Platform & OS Clipboard
#if defined(_WIN32)
    PlatformWin32Clipboard::PublishToWindowsClipboard(
        currentPackage.clonedObjects,
        currentPackage.svgXml,
        currentPackage.pngBytes,
        blSurface,
        currentPackage.plainText,
        lastClipboardSequenceNumber
    );
#else
    if (!currentPackage.plainText.empty()) {
        SDL_SetClipboardText(currentPackage.plainText.c_str());
    }

    // Publish SVG mime type to OS clipboard if supported
    if (!currentPackage.svgXml.empty()) {
        static const char* s_svgMimeTypes[] = { "image/svg+xml" };
        SDL_SetClipboardData([](void* userdata, const char* mime_type, size_t* size) -> const void* {
            auto* pkg = static_cast<ClipboardDataPackage*>(userdata);
            if (std::string(mime_type) == "image/svg+xml") {
                *size = pkg->svgXml.size();
                return pkg->svgXml.data();
            }
            return nullptr;
        }, nullptr, &currentPackage, s_svgMimeTypes, 1);
    }
#endif

    LOG_INFO(General, "Copied " + std::to_string(currentPackage.objectCount) +
             " objects (Bounds: " + std::to_string(totalBox.Width()) + "x" +
             std::to_string(totalBox.Height()) + " mm). Generated SVG: " +
             std::to_string(currentPackage.svgXml.size()) + " B, PNG: " +
             std::to_string(currentPackage.pngBytes.size()) + " B.");

    return true;
}

bool ClipboardManager::CutObjects(const std::vector<std::shared_ptr<CanvasObject>>& objects,
                                 DocumentSession& session) {
    if (!CopyObjects(objects)) {
        return false;
    }

    auto activePage = session.GetActivePage();
    if (!activePage) return false;

    std::vector<std::shared_ptr<CanvasObject>> toRemove;
    toRemove.reserve(objects.size());

    for (const auto& obj : objects) {
        if (obj) {
            toRemove.push_back(obj);
        }
    }

    if (!toRemove.empty()) {
        session.RecordHistoryCommand(activePage, std::make_unique<Folio::RemoveObjectsCommand>(toRemove));
        for (const auto& obj : toRemove) {
            activePage->RemoveObject(obj);
        }
        activePage->isModified = true;
        session.NotifyPageModified(activePage);
        LOG_INFO(General, "Cut " + std::to_string(toRemove.size()) + " objects from active page.");
    }

    return true;
}

// =============================================================================
// CONSUMER IMPLEMENTATION (PASTE)
// =============================================================================

std::vector<std::shared_ptr<CanvasObject>> ClipboardManager::Paste(DocumentSession& session,
                                                                 CanvasEngine& engine,
                                                                 double targetWorldX,
                                                                 double targetWorldY) {
    auto activePage = session.GetActivePage();
    if (!activePage) return {};

    std::vector<std::shared_ptr<CanvasObject>> createdObjects;

#if defined(_WIN32)
    const DWORD currentSeq = ::GetClipboardSequenceNumber();
    const bool externalClipboardChanged = (currentSeq != lastClipboardSequenceNumber);

    // If external clipboard was modified since our last Copy, attempt to ingest external OS clipboard first!
    if (externalClipboardChanged || currentPackage.clonedObjects.empty()) {
        if (PlatformWin32Clipboard::IngestFromWindowsClipboard(session, engine, targetWorldX, targetWorldY, createdObjects)) {
            // Update sequence number to prevent redundant re-ingestion
            lastClipboardSequenceNumber = ::GetClipboardSequenceNumber();
            return createdObjects;
        }
    }
#endif

    // --- Priority 1: Native In-Memory Cloned Objects ---
    if (!currentPackage.clonedObjects.empty()) {
        double dx = targetWorldX - currentPackage.originX;
        double dy = targetWorldY - currentPackage.originY;
        BLMatrix2D trans = BLMatrix2D::make_translation(dx, dy);

        createdObjects.reserve(currentPackage.clonedObjects.size());
        for (const auto& obj : currentPackage.clonedObjects) {
            if (!obj) continue;
            auto clone = std::shared_ptr<CanvasObject>(obj->Clone().release());
            if (!clone) continue;

            clone->uid = UIDGenerator::Next();
            clone->guuid = GUIDGenerator::GenerateV4();
            clone->ApplyTransform(trans);
            clone->UpdateBounds();
            clone->isSelected = 1;

            activePage->AddObject(clone);
            createdObjects.push_back(clone);
        }

        if (!createdObjects.empty()) {
            session.RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectsCommand>(createdObjects));
            activePage->isModified = true;
            session.NotifyPageModified(activePage);
            engine.selectionGizmo.SetSelectedObjects(createdObjects);
            engine.isDirty = true;
            engine.needsFullRebake = true;
            LOG_INFO(General, "Pasted " + std::to_string(createdObjects.size()) +
                     " native objects at (" + std::to_string(targetWorldX) + ", " +
                     std::to_string(targetWorldY) + ") mm");
            return createdObjects;
        }
    }

    // --- Priority 2: OS Clipboard Image (PNG, JPEG, BMP, etc.) ---
    const char* imageMimes[] = { "image/png", "image/jpeg", "image/bmp", "image/gif", "image/webp", "image/svg+xml" };
    for (const char* mime : imageMimes) {
        if (SDL_HasClipboardData(mime)) {
            size_t dataSize = 0;
            void* clipData = SDL_GetClipboardData(mime, &dataSize);
            if (clipData && dataSize > 0) {
                std::string relPath = CanvasEngine::DeduplicateAndSaveImage("", clipData, dataSize, &session, ".png");
                const double screenDpi = engine.transform.pixelsPerMm * 25.4;
                auto img = std::make_shared<Folio::ImageObject>(static_cast<const uint8_t*>(clipData), dataSize, relPath, screenDpi);

                if (img->isLoaded) {
                    img->uid = UIDGenerator::Next();
                    img->guuid = GUIDGenerator::GenerateV4();
                    img->worldX = targetWorldX - img->worldWidth * 0.5;
                    img->worldY = targetWorldY - img->worldHeight * 0.5;
                    img->UpdateBounds();
                    img->isSelected = 1;

                    session.AddImage(img);
                    createdObjects.push_back(img);
                    engine.selectionGizmo.SetSelectedObjects({img});
                    engine.needsFullRebake = true;
                    engine.isDirty = true;
                    SDL_free(clipData);
                    LOG_INFO(General, "Pasted image from OS clipboard (" +
                             std::to_string(img->naturalWidth) + "x" +
                             std::to_string(img->naturalHeight) + " px)");
                    return createdObjects;
                }
                SDL_free(clipData);
            }
        }
    }

    // --- Priority 3: OS Clipboard Text / URL ---
    if (SDL_HasClipboardText()) {
        char* textPtr = SDL_GetClipboardText();
        if (textPtr) {
            std::string clipText(textPtr);
            SDL_free(textPtr);

            if (!clipText.empty()) {
                // Check if text is a YouTube or Web URL
                bool isHttp = (clipText.rfind("http://", 0) == 0 || clipText.rfind("https://", 0) == 0);
                bool isYouTube = (clipText.find("youtube.com") != std::string::npos || clipText.find("youtu.be") != std::string::npos);

                if (isYouTube) {
                    // Create a VideoObject for YouTube URLs
                    double cardW = 160.0;
                    double cardH = 95.0;
                    auto vid = std::make_shared<Folio::VideoObject>(
                        clipText, "YouTube Video", cardW, cardH
                    );
                    vid->uid = UIDGenerator::Next();
                    vid->guuid = GUIDGenerator::GenerateV4();
                    vid->worldX = targetWorldX - cardW * 0.5;
                    vid->worldY = targetWorldY - cardH * 0.5;
                    vid->UpdateBounds();
                    vid->isSelected = 1;

                    session.AddObject(vid);
                    createdObjects.push_back(vid);
                    engine.selectionGizmo.SetSelectedObjects({vid});
                    engine.needsFullRebake = true;
                    engine.isDirty = true;
                    LOG_INFO(General, "Pasted YouTube URL as VideoObject: " + clipText);
                    return createdObjects;
                }

                // Fallback: Create a TextBoxObject for plain text
                auto tb = std::make_shared<Folio::TextBoxObject>();
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
                createdObjects.push_back(tb);
                engine.selectionGizmo.SetSelectedObjects({tb});
                engine.needsFullRebake = true;
                engine.isDirty = true;
                LOG_INFO(General, "Pasted text from clipboard into TextBoxObject (" + std::to_string(clipText.size()) + " chars)");
                return createdObjects;
            }
        }
    }

    return createdObjects;
}

bool ClipboardManager::HasPasteableContent() const {
#if defined(_WIN32)
    const DWORD currentSeq = ::GetClipboardSequenceNumber();
    if (currentSeq == lastClipboardSequenceNumber && !currentPackage.clonedObjects.empty()) {
        return true;
    }
    if (PlatformWin32Clipboard::HasWindowsClipboardContent()) {
        return true;
    }
#endif
    if (!currentPackage.clonedObjects.empty()) return true;
    if (SDL_HasClipboardText()) return true;

    const char* imageMimes[] = { "image/png", "image/jpeg", "image/bmp", "image/gif", "image/webp", "image/svg+xml" };
    for (const char* mime : imageMimes) {
        if (SDL_HasClipboardData(mime)) return true;
    }

    return false;
}

// =============================================================================
// FORMAT GENERATOR HELPERS
// =============================================================================

std::string ClipboardManager::GenerateSvgXml(const std::vector<std::shared_ptr<CanvasObject>>& objects,
                                            const AABB& bounds) {
    double w = std::max(1.0, bounds.Width());
    double h = std::max(1.0, bounds.Height());

    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
       << "viewBox=\"" << bounds.minX << " " << bounds.minY << " " << w << " " << h << "\" "
       << "width=\"" << w << "mm\" height=\"" << h << "mm\">\n";
    ss << "  <!-- Generated by FolioNote Multi-Format Clipboard -->\n";

    for (const auto& obj : objects) {
        if (!obj) continue;

        // Ink strokes
        if (auto ink = std::dynamic_pointer_cast<InkContainer>(obj)) {
            for (const auto& stroke : ink->strokes) {
                if (stroke.segments.empty()) continue;

                uint32_t val = stroke.color.value;
                uint8_t a = (val >> 24) & 0xFF;
                uint8_t r = (val >> 16) & 0xFF;
                uint8_t g = (val >> 8) & 0xFF;
                uint8_t b = val & 0xFF;
                float alpha = a / 255.0f;
                float strokeW = std::max(0.2f, static_cast<float>(stroke.baseWidth));

                ss << "  <polyline fill=\"none\" stroke=\"rgba(" << (int)r << "," << (int)g << "," << (int)b << "," << alpha << ")\" "
                   << "stroke-width=\"" << strokeW << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
                ss << stroke.segments[0].p0.x << "," << stroke.segments[0].p0.y << " ";
                for (const auto& seg : stroke.segments) {
                    ss << seg.p1.x << "," << seg.p1.y << " ";
                }
                ss << "\" />\n";
            }
        }
        // Text boxes
        else if (auto tb = std::dynamic_pointer_cast<TextBoxObject>(obj)) {
            ss << "  <text x=\"" << tb->worldX << "\" y=\"" << (tb->worldY + tb->fontSize * 0.35)
               << "\" font-family=\"" << tb->fontFamily << "\" font-size=\"" << tb->fontSize
               << "pt\" fill=\"#1f2937\">" << tb->text << "</text>\n";
        }
        // Raster Images (embed base64 PNG for SVG consumers)
        else if (auto img = std::dynamic_pointer_cast<ImageObject>(obj)) {
            if (img->isLoaded && !img->cachedBlImage.is_empty()) {
                BLArray<uint8_t> pngBuf;
                BLImageCodec codec;
                codec.find_by_name("PNG");
                if (codec.is_valid() && img->cachedBlImage.write_to_data(pngBuf, codec) == BL_SUCCESS && !pngBuf.is_empty()) {
                    static const char* b64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    std::string b64;
                    const uint8_t* p = pngBuf.data();
                    const size_t len = pngBuf.size();
                    b64.reserve(((len + 2) / 3) * 4);
                    for (size_t i = 0; i < len; i += 3) {
                        const uint32_t b = (p[i] << 16) | ((i + 1 < len ? p[i + 1] : 0) << 8) | (i + 2 < len ? p[i + 2] : 0);
                        b64.push_back(b64Chars[(b >> 18) & 0x3F]);
                        b64.push_back(b64Chars[(b >> 12) & 0x3F]);
                        b64.push_back(i + 1 < len ? b64Chars[(b >> 6) & 0x3F] : '=');
                        b64.push_back(i + 2 < len ? b64Chars[b & 0x3F] : '=');
                    }
                    ss << "  <image x=\"" << img->worldX << "\" y=\"" << img->worldY
                       << "\" width=\"" << img->worldWidth << "mm\" height=\"" << img->worldHeight
                       << "mm\" href=\"data:image/png;base64," << b64 << "\" />\n";
                }
            }
        }
    }

    ss << "</svg>\n";
    return ss.str();
}

std::vector<uint8_t> ClipboardManager::RenderToPng(const std::vector<std::shared_ptr<CanvasObject>>& objects,
                                                  const AABB& bounds,
                                                  double dpi,
                                                  BLImage* outBlImage) {
    double scale = dpi / 25.4; // pixels per millimeter
    int pixelW = std::clamp(static_cast<int>(std::ceil(bounds.Width() * scale)), 1, 8192);
    int pixelH = std::clamp(static_cast<int>(std::ceil(bounds.Height() * scale)), 1, 8192);

    BLImage img(pixelW, pixelH, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    if (!ctx.is_valid()) return {};

    // Clear with transparent alpha
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0, 0, 0, 0));
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

    // Coordinate mapping: translate so bounds.min is at origin (0,0), then scale by dpi
    ctx.scale(scale, scale);
    ctx.translate(-bounds.minX, -bounds.minY);

    // Dummy viewport matching bounding box
    Viewport vp;
    vp.bounds = bounds;
    vp.zoom = 1.0;
    vp.pixelsPerMm = scale;

    for (const auto& obj : objects) {
        if (obj) {
            obj->Render(ctx, vp);
        }
    }

    ctx.end();

    if (outBlImage) {
        *outBlImage = img;
    }

    // Encode to PNG bytes
    BLArray<uint8_t> pngBuffer;
    BLImageCodec codec;
    codec.find_by_name("PNG");
    if (codec.is_valid()) {
        img.write_to_data(pngBuffer, codec);
    }

    std::vector<uint8_t> result(pngBuffer.size());
    if (!pngBuffer.is_empty()) {
        std::memcpy(result.data(), pngBuffer.data(), pngBuffer.size());
    }
    return result;
}

std::string ClipboardManager::ExtractPlainText(const std::vector<std::shared_ptr<CanvasObject>>& objects) {
    std::string text;
    for (const auto& obj : objects) {
        if (!obj) continue;

        if (auto tb = std::dynamic_pointer_cast<TextBoxObject>(obj)) {
            if (!text.empty()) text += "\n";
            text += tb->text;
        } else if (auto vid = std::dynamic_pointer_cast<VideoObject>(obj)) {
            if (!text.empty()) text += "\n";
            text += vid->sourceUrl;
        } else if (auto aud = std::dynamic_pointer_cast<AudioObject>(obj)) {
            if (!text.empty()) text += "\n";
            text += aud->filePath;
        }
    }
    return text;
}

} // namespace Folio
