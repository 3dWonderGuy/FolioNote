#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "core/objects/media/images/image_container.hpp"
#include "core/objects/media/images/image_decoder.hpp"
#include "core/objects/media/videos/video_container.hpp"
#include "core/objects/media/audio/audio_container.hpp"
#include "core/objects/attachment_container/attachment_container.hpp"
#include "core/objects/pdf_container.hpp"
#include "core/storage/pdf_storage.hpp"
#include "core/objects/object_config.hpp"
#include "io/facade/file_manager.hpp"
#include "utils/guid_generator.hpp"
#include "utils/uid_generator.hpp"
#include "utils/logger.hpp"
#include "utils/error_codes.hpp"
#include "ui/framework/ui_overlay_host.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <SDL3/SDL_dialog.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

/**
 * @brief Deduplicates and persists an image into the active notebook's package directory.
 *
 * Mathematical Basis:
 * Uses 64-bit FNV-1a hashing (Offset Basis: 14695981039346656037ULL, Prime: 1099511628211ULL)
 * to compute a content hash over byte streams for fast O(1) content deduplication.
 *
 * Inputs:
 * - srcPath: Optional path or content:// URI to the source image file on disk.
 * - data: Optional pointer to raw in-memory image bytes.
 * - size: Byte size of in-memory image buffer.
 * - session: Pointer to active DocumentSession for notebook directory resolution.
 * - ext: File extension (e.g., ".png", ".jpg").
 *
 * Output:
 * - Relative package path (e.g., "imports/images/img_<hash>.png") or empty string on failure.
 */
std::string CanvasEngine::DeduplicateAndSaveImage(const std::string& srcPath, const void* data, size_t size, DocumentSession* session, const std::string& ext) {
    if (!session) return "";
    auto activeNb = session->workspace.GetActiveNotebook();
    if (!activeNb || activeNb->filePath.empty()) return "";

    std::filesystem::path pkgPath(activeNb->filePath);
    std::filesystem::path imgDir = pkgPath / "imports" / "images";
    Folio::FileManager::CreateDirectories(imgDir.string());

    uint64_t hash = 14695981039346656037ULL;
    if (data && size > 0) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= p[i];
            hash *= 1099511628211ULL;
        }
    } else if (!srcPath.empty() && Folio::FileManager::Exists(srcPath)) {
        std::vector<uint8_t> buf;
        if (Folio::FileManager::ReadBinary(srcPath, buf) && !buf.empty()) {
            for (size_t i = 0; i < buf.size(); ++i) {
                hash ^= buf[i];
                hash *= 1099511628211ULL;
            }
        } else {
            return "";
        }
    } else {
        return "";
    }

    char hashStr[32];
    std::snprintf(hashStr, sizeof(hashStr), "%016llx", static_cast<unsigned long long>(hash));
    std::string filename = std::string("img_") + hashStr + ext;
    std::filesystem::path destFile = imgDir / filename;

    if (!Folio::FileManager::Exists(destFile.string())) {
        if (data && size > 0) {
            std::ofstream out(destFile, std::ios::binary | std::ios::trunc);
            if (out.is_open()) {
                out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            }
        } else if (!srcPath.empty() && Folio::FileManager::Exists(srcPath)) {
            Folio::FileManager::CopySingleFile(srcPath, destFile.string(), true);
        }
    }

    return (std::filesystem::path("imports") / "images" / filename).string();
}

/**
 * @brief Asynchronous callback invoked by SDL when an image file is picked by the user.
 *
 * Working Process:
 * 1. Reads binary payload using Folio::FileManager::ReadBinary (supporting Android SAF content:// URIs).
 * 2. On failure, triggers an error toast notification without silent dropping.
 * 3. Detects/sniffs magic byte headers if the file extension is absent or generic.
 * 4. Deduplicates and caches image in notebook package.
 * 5. Decodes image in background; if decoding fails, raises error toast.
 * 6. Dispatches UI/session mutation to main thread via EnqueueMainThreadTask to prevent race conditions.
 */
void SDLCALL CanvasEngine::OnImageFileSelected(void* userdata, const char* const* filelist, int) {
    auto* ctx = static_cast<ImageFileDialogContext*>(userdata);
    if (!ctx) return;

    if (filelist && filelist[0] && filelist[0][0] != '\0') {
        std::string selectedPath = filelist[0];
        std::vector<uint8_t> rawBytes;
        bool readOk = Folio::FileManager::ReadBinary(selectedPath, rawBytes);
        if (!readOk || rawBytes.empty()) {
            LOG_ERROR(CanvasEngine, "Failed to read image bytes from path: " + selectedPath);
            if (ctx->canvas) {
                ctx->canvas->EnqueueMainThreadTask([selectedPath]() {
                    Folio::UI::UIOverlayHost::Instance().ShowErrorToast(
                        "Image Import Failed", "Unable to read image file from storage: " + selectedPath);
                });
            }
            delete ctx;
            return;
        }

        std::string ext = Folio::FileManager::GetExtension(selectedPath);
        // Sniff image magic bytes if extension is absent (standard for Android SAF content:// URIs)
        if (ext.empty() || ext == "bin") {
            if (rawBytes.size() >= 8 && rawBytes[0] == 0x89 && rawBytes[1] == 'P' && rawBytes[2] == 'N' && rawBytes[3] == 'G') {
                ext = ".png";
            } else if (rawBytes.size() >= 3 && rawBytes[0] == 0xFF && rawBytes[1] == 0xD8 && rawBytes[2] == 0xFF) {
                ext = ".jpg";
            } else if (rawBytes.size() >= 12 && rawBytes[0] == 'R' && rawBytes[1] == 'I' && rawBytes[2] == 'F' && rawBytes[3] == 'F' &&
                       rawBytes[8] == 'W' && rawBytes[9] == 'E' && rawBytes[10] == 'B' && rawBytes[11] == 'P') {
                ext = ".webp";
            } else if (rawBytes.size() >= 6 && rawBytes[0] == 'G' && rawBytes[1] == 'I' && rawBytes[2] == 'F') {
                ext = ".gif";
            } else if (rawBytes.size() >= 2 && rawBytes[0] == 'B' && rawBytes[1] == 'M') {
                ext = ".bmp";
            } else {
                ext = ".png";
            }
        }
        if (!ext.empty() && ext[0] != '.') ext = "." + ext;

        std::string storedPath = ctx->canvas->DeduplicateAndSaveImage(selectedPath, rawBytes.data(), rawBytes.size(), ctx->session, ext);

        const double screenDpi = ctx->canvas->transform.pixelsPerMm * 25.4;
        auto img = std::make_shared<Folio::ImageObject>(rawBytes.data(), rawBytes.size(), storedPath.empty() ? selectedPath : storedPath, screenDpi);
        if (!img->isLoaded) {
            LOG_ERROR(CanvasEngine, "Failed to decode image from path: " + selectedPath);
            if (ctx->canvas) {
                ctx->canvas->EnqueueMainThreadTask([selectedPath]() {
                    Folio::UI::UIOverlayHost::Instance().ShowErrorToast(
                        "Image Decode Failed", "Unsupported or corrupted image format: " + selectedPath);
                });
            }
            delete ctx;
            return;
        }

        CanvasEngine* canvas = ctx->canvas;
        DocumentSession* session = ctx->session;
        Point2D insertPos = ctx->insertPosWorld;

        // Dispatch canvas insertion to the main UI thread
        canvas->EnqueueMainThreadTask([canvas, session, img, insertPos, selectedPath, storedPath]() {
            if (!session) return;
            img->worldX = insertPos.x - img->worldWidth * 0.5;
            img->worldY = insertPos.y - img->worldHeight * 0.5;
            img->UpdateBounds();

            session->AddImage(img);
            canvas->InvalidateLayer();
            LOG_INFO(CanvasEngine, "Imported image from '" + selectedPath + "' -> '" + (storedPath.empty() ? selectedPath : storedPath) + "' (" + Folio::ImageFormatToString(img->imageFormat).data() + ")");
        });
    }

    delete ctx;
}

void CanvasEngine::OpenImageFileDialog(SDL_Window* parentWin, DocumentSession* session) {
    if (!session) return;
    Point2D centerWorld = transform.ScreenToWorld(static_cast<float>(viewportW) * 0.5f, static_cast<float>(viewportH) * 0.5f);

    auto* ctx = new ImageFileDialogContext{ this, session, centerWorld };

    static const SDL_DialogFileFilter imageFilters[] = {
        { "Supported Images (*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif;*.tif;*.tiff;*.qoi;*.svg)", "png;jpg;jpeg;webp;bmp;gif;tif;tiff;qoi;svg" },
        { "WebP Images (*.webp)", "webp" },
        { "GIF Images (*.gif)", "gif" },
        { "Vector Graphics (*.svg)", "svg" },
        { "PNG Images (*.png)", "png" },
        { "JPEG Images (*.jpg;*.jpeg)", "jpg;jpeg" },
        { "TIFF Images (*.tif;*.tiff)", "tif;tiff" },
        { "All Files (*.*)", "*" }
    };

    LOG_INFO(CanvasEngine, "Opening native image file dialog...");
    SDL_ShowOpenFileDialog(OnImageFileSelected, ctx, parentWin ? parentWin : sdlWindow, imageFilters, static_cast<int>(sizeof(imageFilters) / sizeof(imageFilters[0])), nullptr, false);
}

void SDLCALL CanvasEngine::OnAttachmentFileSelected(void* userdata, const char* const* filelist, int) {
    auto* ctx = static_cast<AttachmentFileDialogContext*>(userdata);
    if (!ctx) return;

    if (filelist && filelist[0] && filelist[0][0] != '\0') {
        std::string selectedPath = filelist[0];
        std::string filename = std::filesystem::path(selectedPath).filename().string();
        if (filename.empty()) filename = selectedPath;

        CanvasEngine* canvas = ctx->canvas;
        DocumentSession* session = ctx->session;
        if (canvas) {
            canvas->EnqueueMainThreadTask([canvas, session, selectedPath, filename]() {
                canvas->m_pendingAttachPath    = selectedPath;
                canvas->m_pendingAttachName    = filename;
                canvas->m_pendingAttachSession = session;
                canvas->m_attachModalOpen      = true;
                LOG_INFO(CanvasEngine, "Attachment file selected (async): '" + filename + "' — awaiting embed/link decision.");
            });
        }
    }

    delete ctx;
}

void CanvasEngine::OpenAttachmentFileDialog(SDL_Window* parentWin, DocumentSession* session) {
    if (!session) return;

#if defined(_WIN32)
    SDL_Window* win = parentWin ? parentWin : sdlWindow;
    HWND hwnd = nullptr;
    if (win) {
        SDL_PropertiesID props = SDL_GetWindowProperties(win);
        hwnd = static_cast<HWND>(
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    }

    const wchar_t kFilter[] =
        L"All Files (*.*)\0*.*\0"
        L"Documents (*.pdf;*.docx;*.xlsx;*.pptx;*.txt;*.md)\0*.pdf;*.docx;*.xlsx;*.pptx;*.txt;*.md\0"
        L"Images (*.png;*.jpg;*.jpeg;*.webp)\0*.png;*.jpg;*.jpeg;*.webp\0"
        L"\0";

    wchar_t fileBuf[MAX_PATH] = {};

    OPENFILENAMEW ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = hwnd;
    ofn.lpstrFilter     = kFilter;
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = fileBuf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                          OFN_EXPLORER    | OFN_NOCHANGEDIR;
    ofn.lpstrTitle      = L"Select File to Attach";

    LOG_INFO(CanvasEngine, "Opening Win32 attachment file dialog (GetOpenFileNameW)...");

    if (::GetOpenFileNameW(&ofn)) {
        int utf8Len = WideCharToMultiByte(
            CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);

        std::string selectedPath;
        if (utf8Len > 0) {
            selectedPath.resize(static_cast<size_t>(utf8Len) - 1);
            WideCharToMultiByte(
                CP_UTF8, 0, fileBuf, -1,
                &selectedPath[0], utf8Len, nullptr, nullptr);
        }

        if (!selectedPath.empty()) {
            std::string filename =
                std::filesystem::path(selectedPath).filename().string();
            if (filename.empty()) filename = selectedPath;

            m_pendingAttachPath    = selectedPath;
            m_pendingAttachName    = filename;
            m_pendingAttachSession = session;
            m_attachModalOpen      = true;
            LOG_INFO(CanvasEngine, "Attachment file selected: '" + filename + "' — awaiting embed/link decision.");
        }
    } else {
        DWORD err = CommDlgExtendedError();
        if (err != 0) {
            LOG_INFO(CanvasEngine,
                "GetOpenFileNameW failed, CommDlgExtendedError=" +
                std::to_string(static_cast<unsigned long>(err)));
        }
    }
#else
    auto* ctx = new AttachmentFileDialogContext{ this, session };
    LOG_INFO(CanvasEngine, "Opening SDL attachment file dialog...");
    SDL_ShowOpenFileDialog(
        OnAttachmentFileSelected, ctx,
        parentWin ? parentWin : sdlWindow,
        nullptr, 0, nullptr, false);
#endif
}

/**
 * @brief Commits a selected attachment file, either copying into the package attachments sidecar
 * or retaining it as an external filesystem/URL link.
 *
 * Inputs:
 * - embed: If true, copies the file into <notebook>/attachments/ and records relative path.
 * - session: Pointer to active DocumentSession.
 */
void CanvasEngine::CommitAttachment(bool embed, DocumentSession* session) {
    if (m_pendingAttachPath.empty() || !session) {
        m_attachModalOpen = false;
        return;
    }

    std::string finalPath = m_pendingAttachPath;
    bool embeddedOk = false;

    if (embed) {
        auto activeNb = session->GetActiveNotebook();
        std::string notebookDir = (activeNb) ? activeNb->filePath : "";
        if (!notebookDir.empty()) {
            std::string attachDir = Folio::FileManager::JoinPath(notebookDir, "attachments");
            if (Folio::FileManager::CreateDirectories(attachDir)) {
                std::string safeFilename = GUIDGenerator::GenerateV4().substr(0, 8)
                                         + "_" + Folio::FileManager::SanitizeFileName(m_pendingAttachName);
                std::string destPath = Folio::FileManager::JoinPath(attachDir, safeFilename);

                if (Folio::FileManager::CopySingleFile(m_pendingAttachPath, destPath, true)) {
                    finalPath  = Folio::FileManager::JoinPath("attachments", safeFilename);
                    embeddedOk = true;
                    LOG_INFO(CanvasEngine, "Embedded attachment: copied '" + m_pendingAttachName +
                                          "' to sidecar as '" + safeFilename + "'");
                } else {
                    LOG_ERROR_CODE(CanvasEngine, Folio::FolioErrorCode::SysFileWriteFailed,
                        "Failed to copy attachment to sidecar: " + m_pendingAttachPath +
                        " — falling back to link mode.");
                    Folio::UI::UIOverlayHost::Instance().ShowErrorToast(
                        "Attachment Copy Failed", "System copy failed for attachment file. Falling back to link mode.");
                    finalPath  = m_pendingAttachPath;
                    embeddedOk = false;
                }
            } else {
                LOG_ERROR_CODE(CanvasEngine, Folio::FolioErrorCode::SysDirectoryCreateFailed,
                    "Failed to create attachments sidecar directory — falling back to link mode.");
                Folio::UI::UIOverlayHost::Instance().ShowErrorToast(
                    "Folder Creation Failed", "Could not create attachments folder in notebook package.");
            }
        } else {
            LOG_WARN(CanvasEngine,
                "Cannot embed attachment: notebook has no directory yet. Using link mode.");
        }
    }

    Point2D centerWorld = transform.ScreenToWorld(
        static_cast<float>(viewportW) * 0.5f,
        static_cast<float>(viewportH) * 0.5f);

    auto attachObj = std::make_shared<Folio::AttachmentObject>(
        finalPath, m_pendingAttachName, "", (embed && embeddedOk));
    attachObj->isFileValid = Folio::FileManager::Exists(finalPath);
    attachObj->worldX  = centerWorld.x - Folio::AttachmentObject::chipW * 0.5;
    attachObj->worldY  = centerWorld.y - Folio::AttachmentObject::chipH * 0.5;
    attachObj->guuid   = GUIDGenerator::GenerateV4();
    attachObj->uid     = UIDGenerator::Next();
    attachObj->UpdateBounds();

    auto activePage = session->GetActivePage();
    if (activePage) {
        activePage->AddObject(attachObj, true);
        session->RecordHistoryCommand(
            activePage,
            std::make_unique<Folio::AddObjectCommand>(attachObj));
        activePage->isModified = true;
        session->NotifyPageModified(activePage);
    }

    InvalidateLayer();

    LOG_INFO(CanvasEngine, "Committed attachment '" + m_pendingAttachName +
                           "' mode=" + std::string(embed && embeddedOk ? "embedded" : "link") +
                           " path='" + finalPath + "'");

    m_pendingAttachPath.clear();
    m_pendingAttachName.clear();
    m_pendingAttachSession = nullptr;
    m_attachModalOpen      = false;
}

/**
 * @brief Asynchronous callback invoked by SDL when a PDF file is picked by the user.
 * Dispatches PDF ingestion and canvas object creation to the main rendering thread.
 */
void SDLCALL CanvasEngine::OnPdfFileSelected(void* userdata, const char* const* filelist, int) {
    auto* ctx = static_cast<PdfFileDialogContext*>(userdata);
    if (!ctx) return;

    if (filelist && filelist[0] && filelist[0][0] != '\0') {
        std::string selectedPath = filelist[0];
        CanvasEngine* canvas = ctx->canvas;
        DocumentSession* session = ctx->session;
        Point2D insertPos = ctx->insertPosWorld;
        bool asBg = ctx->asBackground;
        Folio::PdfImportMode mode = ctx->importMode;

        if (canvas) {
            canvas->EnqueueMainThreadTask([canvas, session, selectedPath, insertPos, asBg, mode]() {
                if (canvas->onPdfImportRequested) {
                    canvas->onPdfImportRequested(selectedPath, session);
                } else {
                    Folio::PdfDocumentInfo docInfo;
                    if (Folio::PdfStorage::IngestPdf(selectedPath, session, mode, docInfo)) {
                        if (docInfo.isLongDocument) {
                            LOG_INFO(CanvasEngine, "[PDF Recommendation] " + docInfo.warningMessage);
                        }

                        auto activePage = session ? session->GetActivePage() : nullptr;
                        if (activePage) {
                            auto pdfObj = std::make_shared<Folio::PdfContainer>(
                                docInfo.packagePath, docInfo.originalFileName, 0, docInfo.pageCount,
                                insertPos.x - 105.0, insertPos.y - 148.5, 210.0, 297.0, asBg
                            );
                            pdfObj->resolvedDiskPath = docInfo.diskPath;
                            pdfObj->isExternalLink = docInfo.isExternal;
                            pdfObj->guuid = GUIDGenerator::GenerateV4();
                            pdfObj->uid = UIDGenerator::Next();
                            pdfObj->EnsurePageLoaded();
                            pdfObj->UpdateBounds();

                            activePage->AddObject(pdfObj, true);
                            session->RecordHistoryCommand(activePage, std::make_unique<Folio::AddObjectCommand>(pdfObj));
                            canvas->InvalidateLayer();
                        }
                    } else {
                        Folio::UI::UIOverlayHost::Instance().ShowErrorToast(
                            "PDF Import Failed", "Could not import or read PDF: " + selectedPath);
                    }
                }
            });
        }
    }

    delete ctx;
}

void CanvasEngine::OpenPdfFileDialog(SDL_Window* parentWin, DocumentSession* session, bool asBackground, Folio::PdfImportMode mode) {
    if (!session) return;
    Point2D centerWorld = transform.ScreenToWorld(static_cast<float>(viewportW) * 0.5f, static_cast<float>(viewportH) * 0.5f);

    auto* ctx = new PdfFileDialogContext{ this, session, centerWorld, asBackground, mode };

    static const SDL_DialogFileFilter pdfFilters[] = {
        { "PDF Documents (*.pdf)", "pdf" },
        { "All Files (*.*)", "*" }
    };

    LOG_INFO(CanvasEngine, "Opening native PDF file dialog...");
    SDL_ShowOpenFileDialog(OnPdfFileSelected, ctx, parentWin ? parentWin : sdlWindow, pdfFilters, 2, nullptr, false);
}

bool CanvasEngine::InsertImageFromClipboard(DocumentSession* session) {
    if (!session) return false;
    auto activePage = session->GetActivePage();
    if (!activePage) return false;

    const char* mimeTypes[] = { "image/png", "image/jpeg", "image/bmp", "image/gif", "image/webp", "image/svg+xml" };
    for (const char* mime : mimeTypes) {
        if (SDL_HasClipboardData(mime)) {
            size_t dataSize = 0;
            void* clipData = SDL_GetClipboardData(mime, &dataSize);
            if (clipData && dataSize > 0) {
                std::string relPath = DeduplicateAndSaveImage("", clipData, dataSize, session, ".png");

                const double screenDpi = transform.pixelsPerMm * 25.4;
                auto img = std::make_shared<Folio::ImageObject>(static_cast<const uint8_t*>(clipData), dataSize, relPath, screenDpi);
                if (img->isLoaded) {
                    Point2D centerWorld = transform.ScreenToWorld(static_cast<float>(viewportW) * 0.5f, static_cast<float>(viewportH) * 0.5f);
                    img->worldX = centerWorld.x - img->worldWidth * 0.5;
                    img->worldY = centerWorld.y - img->worldHeight * 0.5;
                    img->UpdateBounds();

                    session->AddImage(img);
                    InvalidateLayer();
                    SDL_free(clipData);
                    LOG_INFO(CanvasEngine, "Pasted image from clipboard (" + std::to_string(img->naturalWidth) + "x" + std::to_string(img->naturalHeight) + " px, " + Folio::ImageFormatToString(img->imageFormat).data() + ")");
                    return true;
                }
                SDL_free(clipData);
            }
        }
    }
    return false;
}

bool CanvasEngine::InsertVideoFromFile(const std::string& filePath, DocumentSession* session) {
    if (!session) return false;
    auto activePage = session->GetActivePage();
    if (!activePage) return false;

    std::filesystem::path fspath(filePath);
    if (!std::filesystem::exists(fspath)) {
        LOG_ERROR(CanvasEngine, "InsertVideoFromFile: File not found: " + filePath);
        return false;
    }

    std::string ext = fspath.extension().string();
    for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

    static const std::initializer_list<const char*> kSupportedVideoExts = {
        ".mp4", ".mkv", ".webm", ".mov", ".avi", ".ts", ".m2ts",
        ".mpg", ".mpeg", ".wmv", ".flv", ".3gp", ".m4v", ".ogv"
    };
    bool supported = false;
    for (const char* e : kSupportedVideoExts) {
        if (ext == e) { supported = true; break; }
    }
    if (!supported) {
        LOG_WARN(CanvasEngine, "InsertVideoFromFile: Unsupported video format: " + ext);
        return false;
    }

    auto& cfg = Folio::ObjectConfig::Get();
    std::error_code ec;
    auto fileBytes = std::filesystem::file_size(fspath, ec);
    if (!ec && fileBytes > cfg.maxVideoFileSizeBytes) {
        LOG_WARN(CanvasEngine, "InsertVideoFromFile: File too large (" +
                 std::to_string(fileBytes / (1024*1024)) + " MB)");
        return false;
    }

    std::string importedPath = filePath;
    try {
        auto activeNb = session->workspace.GetActiveNotebook();
        if (activeNb && !activeNb->filePath.empty()) {
            std::filesystem::path pkgPath(activeNb->filePath);
            std::filesystem::path importDir = pkgPath / "imports" / "videos";
            std::filesystem::create_directories(importDir, ec);
            if (!ec) {
                std::filesystem::path destPath = importDir / fspath.filename();
                if (!std::filesystem::exists(destPath)) {
                    std::filesystem::copy_file(fspath, destPath,
                                               std::filesystem::copy_options::overwrite_existing, ec);
                }
                if (!ec) {
                    importedPath = destPath.string();
                }
            }
        }
    } catch (...) {
        importedPath = filePath;
        LOG_WARN(CanvasEngine, "InsertVideoFromFile: Could not copy to sidecar; streaming from original path");
    }

    double w = cfg.defaultVideoWidthMm;
    double h = cfg.defaultVideoHeightMm;

    auto vid = std::make_shared<Folio::VideoObject>(importedPath, fspath.filename().string(), w, h);

    Point2D centerWorld = transform.ScreenToWorld(
        static_cast<float>(viewportW) * 0.5f,
        static_cast<float>(viewportH) * 0.5f
    );
    vid->worldX = centerWorld.x - w * 0.5;
    vid->worldY = centerWorld.y - h * 0.5;
    vid->UpdateBounds();

    vid->Play([this]() {
        InvalidateLayer();
    });
    vid->Pause();

    session->AddVideo(vid);
    InvalidateLayer();

    LOG_INFO(CanvasEngine, "Inserted VideoObject: " + fspath.filename().string() +
             " (" + std::to_string(static_cast<int>(w)) + "mm x " +
             std::to_string(static_cast<int>(h)) + "mm) from: " + importedPath);
    return true;
}

bool CanvasEngine::InsertWebEmbed(const std::string& url, const std::string& label, DocumentSession* session) {
    return InsertVideoFromUrl(url, label, session);
}

bool CanvasEngine::InsertVideoFromUrl(const std::string& url, const std::string& label, DocumentSession* session) {
    if (!session || url.empty()) return false;

    auto activePage = session->GetActivePage();
    if (!activePage) return false;

    auto& cfg = Folio::ObjectConfig::Get();
    double w = cfg.defaultVideoWidthMm;
    double h = cfg.defaultVideoHeightMm;

    auto vid = std::make_shared<Folio::VideoObject>(url, label.empty() ? url : label, w, h);

    Point2D centerWorld = transform.ScreenToWorld(
        static_cast<float>(viewportW) * 0.5f,
        static_cast<float>(viewportH) * 0.5f
    );
    vid->worldX = centerWorld.x - w * 0.5;
    vid->worldY = centerWorld.y - h * 0.5;
    vid->UpdateBounds();

    if (vid->IsYouTube()) {
        vid->FetchYouTubeThumbnailAsync([this]() {
            InvalidateLayer();
        });
    } else {
        vid->Play([this]() {
            InvalidateLayer();
        });
        vid->Pause();
    }

    session->AddVideo(vid);
    InvalidateLayer();

    LOG_INFO(CanvasEngine, "Inserted URL VideoObject: " + url);
    return true;
}

void SDLCALL CanvasEngine::OnVideoFileSelected(void* userdata, const char* const* filelist, int) {
    auto* ctx = static_cast<VideoFileDialogContext*>(userdata);
    if (!ctx) return;

    if (filelist && filelist[0] && filelist[0][0] != '\0') {
        std::string selectedPath = filelist[0];
        CanvasEngine* canvas = ctx->canvas;
        DocumentSession* session = ctx->session;
        if (canvas) {
            canvas->EnqueueMainThreadTask([canvas, selectedPath, session]() {
                if (!canvas->InsertVideoFromFile(selectedPath, session)) {
                    Folio::UI::UIOverlayHost::Instance().ShowErrorToast(
                        "Video Import Failed", "Could not load video file: " + selectedPath);
                }
            });
        }
    }

    delete ctx;
}

void CanvasEngine::OpenVideoFileDialog(SDL_Window* parentWin, DocumentSession* session) {
    if (!session) return;

    auto* ctx = new VideoFileDialogContext{ this, session };

    static const SDL_DialogFileFilter videoFilters[] = {
        { "Supported Videos (*.mp4;*.mkv;*.webm;*.mov;*.avi;*.ts;*.wmv;*.flv;*.m4v)", "mp4;mkv;webm;mov;avi;ts;m2ts;mpg;mpeg;wmv;flv;3gp;m4v;ogv" },
        { "MP4 Video (*.mp4)", "mp4" },
        { "Matroska Video (*.mkv)", "mkv" },
        { "WebM Video (*.webm)", "webm" },
        { "QuickTime Video (*.mov)", "mov" },
        { "All Files (*.*)", "*" }
    };

    LOG_INFO(CanvasEngine, "Opening native video file dialog...");
    SDL_ShowOpenFileDialog(OnVideoFileSelected, ctx, parentWin ? parentWin : sdlWindow, videoFilters, static_cast<int>(sizeof(videoFilters) / sizeof(videoFilters[0])), nullptr, false);
}

bool CanvasEngine::InsertAudioFromFile(const std::string& filePath, DocumentSession* session) {
    if (!session) return false;
    auto activePage = session->GetActivePage();
    if (!activePage) return false;

    std::filesystem::path fspath(filePath);
    if (!std::filesystem::exists(fspath)) {
        LOG_ERROR(CanvasEngine, "InsertAudioFromFile: File not found: " + filePath);
        return false;
    }

    std::string ext = fspath.extension().string();
    for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

    static const std::initializer_list<const char*> kSupportedAudioExts = {
        ".mp3", ".wav", ".m4a", ".flac", ".ogg", ".aac", ".opus", ".wma", ".aiff"
    };
    bool supported = false;
    for (const char* e : kSupportedAudioExts) {
        if (ext == e) { supported = true; break; }
    }
    if (!supported) {
        LOG_WARN(CanvasEngine, "InsertAudioFromFile: Unsupported audio format: " + ext);
        return false;
    }

    std::string importedPath = filePath;
    try {
        auto activeNb = session->workspace.GetActiveNotebook();
        if (activeNb && !activeNb->filePath.empty()) {
            std::error_code ec;
            std::filesystem::path pkgPath(activeNb->filePath);
            std::filesystem::path importDir = pkgPath / "imports" / "audio";
            std::filesystem::create_directories(importDir, ec);
            if (!ec) {
                std::filesystem::path destPath = importDir / fspath.filename();
                if (!std::filesystem::exists(destPath)) {
                    std::filesystem::copy_file(fspath, destPath,
                                               std::filesystem::copy_options::overwrite_existing, ec);
                }
                if (!ec) {
                    importedPath = destPath.string();
                }
            }
        }
    } catch (...) {
        importedPath = filePath;
    }

    auto audioObj = std::make_shared<Folio::AudioObject>(importedPath, fspath.filename().string());

    Point2D centerWorld = transform.ScreenToWorld(
        static_cast<float>(viewportW) * 0.5f,
        static_cast<float>(viewportH) * 0.5f
    );
    audioObj->worldX = centerWorld.x - Folio::AudioObject::chipW * 0.5;
    audioObj->worldY = centerWorld.y - Folio::AudioObject::chipH * 0.5;
    audioObj->UpdateBounds();

    session->AddObject(audioObj);
    InvalidateLayer();

    LOG_INFO(CanvasEngine, "Inserted AudioObject: " + importedPath);
    return true;
}

void SDLCALL CanvasEngine::OnAudioFileSelected(void* userdata, const char* const* filelist, int) {
    auto* ctx = static_cast<AudioFileDialogContext*>(userdata);
    if (!ctx) return;

    if (filelist && filelist[0] && filelist[0][0] != '\0') {
        std::string selectedPath = filelist[0];
        CanvasEngine* canvas = ctx->canvas;
        DocumentSession* session = ctx->session;
        if (canvas) {
            canvas->EnqueueMainThreadTask([canvas, selectedPath, session]() {
                if (!canvas->InsertAudioFromFile(selectedPath, session)) {
                    Folio::UI::UIOverlayHost::Instance().ShowErrorToast(
                        "Audio Import Failed", "Could not load audio file: " + selectedPath);
                }
            });
        }
    }

    delete ctx;
}

void CanvasEngine::OpenAudioFileDialog(SDL_Window* parentWin, DocumentSession* session) {
    if (!session) return;

    auto* ctx = new AudioFileDialogContext{ this, session };

    static const SDL_DialogFileFilter audioFilters[] = {
        { "Supported Audio Files (*.mp3;*.wav;*.m4a;*.flac;*.ogg;*.aac;*.opus;*.wma)", "mp3;wav;m4a;flac;ogg;aac;opus;wma;aiff" },
        { "MP3 Audio (*.mp3)", "mp3" },
        { "WAV Audio (*.wav)", "wav" },
        { "FLAC Audio (*.flac)", "flac" },
        { "M4A/AAC Audio (*.m4a;*.aac)", "m4a;aac" },
        { "All Files (*.*)", "*" }
    };

    LOG_INFO(CanvasEngine, "Opening native audio file dialog...");
    SDL_ShowOpenFileDialog(OnAudioFileSelected, ctx, parentWin ? parentWin : sdlWindow, audioFilters, static_cast<int>(sizeof(audioFilters) / sizeof(audioFilters[0])), nullptr, false);
}
