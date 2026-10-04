#include "ui/views/md_editor_view.hpp"
#include "misc/cpp/imgui_stdlib.h"
#include "ui/imgui_theme.hpp"
#include "utils/logger.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Folio {

MdEditorView::MdEditorView() {
    s_activeInstance = this;
    InitGL();
}

MdEditorView::~MdEditorView() {
    CleanupGL();
    if (s_activeInstance == this) {
        s_activeInstance = nullptr;
    }
}

void MdEditorView::InitGL() {
    if (glTexture == 0) {
        glGenTextures(1, &glTexture);
        glBindTexture(GL_TEXTURE_2D, glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void MdEditorView::CleanupGL() {
    if (glTexture != 0) {
        glDeleteTextures(1, &glTexture);
        glTexture = 0;
    }
    previewSurface.reset();
    allocatedCapacityW = 0;
    allocatedCapacityH = 0;
    surfaceW = 0;
    surfaceH = 0;
}

void MdEditorView::MountPage(CanvasPage* page) {
    if (!page) return;

    if (activePageGuid != page->guid) {
        activePageGuid = page->guid;
        editorBuffer = page->dedicatedMdContent;
        if (editorBuffer.empty()) {
            editorBuffer = "# " + page->title + "\n\nStart writing here...\n";
            page->dedicatedMdContent = editorBuffer;
            page->isModified = true;
        }
        engine.LoadMarkdown(editorBuffer);
        needsRebake = true;
        previewScrollY = 0.0f;
    }
}

void MdEditorView::CommitToPage(CanvasPage* page) {
    if (!page) return;
    if (page->dedicatedMdContent != editorBuffer) {
        page->dedicatedMdContent = editorBuffer;
        page->isModified = true;
    }
}

void MdEditorView::Render(
    float canvasX, float canvasW,
    float screenH, float titleBarH, float ribbonH,
    DocumentSession& session,
    InputStateMachine& inputSM,
    ThemeManager& themeManager,
    bool inkColorInverted
) {
    auto activePg = session.GetActivePage();
    if (!activePg) return;

    MountPage(activePg.get());

    float contentY = titleBarH + ribbonH;
    float contentH = screenH - contentY;
    if (contentH <= 10.0f || canvasW <= 10.0f) return;

    inputSM.canvasOriginX = canvasX;
    inputSM.canvasOriginY = contentY;
    inputSM.isCanvasHovered = false;
    inputSM.isPdfCanvasHovered = false;

    ImGui::SetNextWindowPos(ImVec2(canvasX, contentY));
    ImGui::SetNextWindowSize(ImVec2(canvasW, contentH));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

    if (ImGui::Begin("##MdEditorWindow", nullptr, flags)) {
        // 1. Top Format & View Mode Toolbar
        RenderToolbar(themeManager);
        ImGui::Separator();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float paneH = avail.y;

        // 2. Render Selected View Mode
        switch (currentMode) {
            case ViewMode::EditorOnly: {
                RenderEditorPane(avail.x, paneH, themeManager, activePg.get());
                break;
            }

            case ViewMode::PreviewOnly: {
                RenderPreviewPane(avail.x, paneH, themeManager, inkColorInverted);
                break;
            }

            case ViewMode::SplitView:
            default: {
                float splitterWidth = 8.0f;
                float usableW = avail.x - splitterWidth;
                float leftW = std::clamp(usableW * splitRatio, 150.0f, usableW - 150.0f);
                float rightW = usableW - leftW;

                // Left Pane: Raw Markdown Editor
                ImGui::BeginChild("##MdLeftChild", ImVec2(leftW, paneH), false);
                RenderEditorPane(leftW, paneH, themeManager, activePg.get());
                ImGui::EndChild();

                ImGui::SameLine();

                // Interactive Splitter Bar
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.22f, 0.28f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.95f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.50f, 0.90f, 1.0f));
                ImGui::Button("##Splitter", ImVec2(splitterWidth, paneH));
                if (ImGui::IsItemActive()) {
                    splitRatio += ImGui::GetIO().MouseDelta.x / usableW;
                    splitRatio = std::clamp(splitRatio, 0.20f, 0.80f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine();

                // Right Pane: Physical Millimeter Paper Preview
                ImGui::BeginChild("##MdRightChild", ImVec2(rightW, paneH), false);
                RenderPreviewPane(rightW, paneH, themeManager, inkColorInverted);
                ImGui::EndChild();
                break;
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
}

void MdEditorView::RenderToolbar(ThemeManager& themeManager) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

    // View Mode Selector
    const char* modes[] = { "Split View", "Editor Only", "Paper Preview" };
    int cur = static_cast<int>(currentMode);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("##ViewModeCombo", &cur, modes, IM_ARRAYSIZE(modes))) {
        currentMode = static_cast<ViewMode>(cur);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Quick Formatting Buttons
    if (ImGui::Button("H1")) InsertFormattingPrefix("# ");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Heading 1");
    ImGui::SameLine();

    if (ImGui::Button("H2")) InsertFormattingPrefix("## ");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Heading 2");
    ImGui::SameLine();

    if (ImGui::Button("H3")) InsertFormattingPrefix("### ");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Heading 3");
    ImGui::SameLine();

    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ImGui::Button("B")) WrapSelectionWith("**", "**");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bold (**text**)");
    ImGui::SameLine();

    if (ImGui::Button("I")) WrapSelectionWith("*", "*");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Italic (*text*)");
    ImGui::SameLine();

    if (ImGui::Button("S")) WrapSelectionWith("~~", "~~");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strikethrough (~~text~~)");
    ImGui::SameLine();

    if (ImGui::Button("`Code`")) WrapSelectionWith("`", "`");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Inline Code (`code`)");
    ImGui::SameLine();

    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ImGui::Button("- List")) InsertFormattingPrefix("- ");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bullet List");
    ImGui::SameLine();

    if (ImGui::Button("1. List")) InsertFormattingPrefix("1. ");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Numbered List");
    ImGui::SameLine();

    if (ImGui::Button("[ ] Task")) InsertTaskItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Task Checkbox (- [ ] )");
    ImGui::SameLine();

    if (ImGui::Button("> Quote")) InsertFormattingPrefix("> ");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blockquote (> quote)");
    ImGui::SameLine();

    if (ImGui::Button("``` Code")) InsertCodeBlockTemplate();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Code Block (```cpp)");
    ImGui::SameLine();

    if (ImGui::Button("Table")) InsertTableTemplate();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Markdown Table");
    ImGui::SameLine();

    if (ImGui::Button("$$ Math")) InsertMathBlockTemplate();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("LaTeX Math Block ($$ ... $$)");
    ImGui::SameLine();

    if (ImGui::Button("--- HR")) InsertFormattingPrefix("\n---\n\n");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Horizontal Divider");

    // Right-aligned Document Telemetry (Words, Chars, Height)
    size_t wordCount = 0;
    bool inWord = false;
    for (char ch : editorBuffer) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            wordCount++;
        }
    }

    std::string statsStr = std::to_string(wordCount) + " words  |  " +
                           std::to_string(editorBuffer.size()) + " chars  |  " +
                           std::to_string(static_cast<int>(engine.totalDocumentHeightMm)) + " mm";
    float statsW = ImGui::CalcTextSize(statsStr.c_str()).x;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW > statsW + 10.0f) {
        ImGui::SameLine(ImGui::GetCursorPosX() + (availW - statsW));
        ImGui::TextDisabled("%s", statsStr.c_str());
    }

    ImGui::PopStyleVar(2);
}

void MdEditorView::RenderEditorPane(float width, float height, ThemeManager& /*themeManager*/, CanvasPage* activePage) {
    ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.24f, 0.32f, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 12.0f));

    ImFont* codeFont = FolioTheme::FontRegular;
    if (codeFont) ImGui::PushFont(codeFont);

    if (ImGui::InputTextMultiline("##MarkdownEditorBuffer", &editorBuffer, ImVec2(width, height - 10.0f), editFlags)) {
        engine.LoadMarkdown(editorBuffer);
        CommitToPage(activePage);
        needsRebake = true;
    }

    if (codeFont) ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void MdEditorView::RenderPreviewPane(float width, float height, ThemeManager& /*themeManager*/, bool invert) {
    if (width <= 20.0f || height <= 20.0f) return;

    // Handle Zoom and Panning Input inside the preview viewport
    ImVec2 screenOrigin = ImGui::GetCursorScreenPos();
    bool isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_RootWindow);

    if (isHovered) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            float zoomDelta = io.MouseWheel * 0.10f;
            previewZoom = std::clamp(previewZoom + zoomDelta, 0.30f, 3.50f);
            needsRebake = true;
        } else if (io.MouseWheel != 0.0f) {
            previewScrollY -= io.MouseWheel * 35.0f;
            previewScrollY = std::max(0.0f, previewScrollY);
            needsRebake = true;
        }

        // Middle mouse drag pan
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            isMiddlePanning = true;
            middlePanLastPos = io.MousePos;
        }
        if (isMiddlePanning && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            float deltaY = io.MousePos.y - middlePanLastPos.y;
            previewScrollY -= deltaY;
            previewScrollY = std::max(0.0f, previewScrollY);
            middlePanLastPos = io.MousePos;
            needsRebake = true;
        } else {
            isMiddlePanning = false;
        }
    }

    int reqW = static_cast<int>(width);
    int reqH = static_cast<int>(height);

    if (reqW != surfaceW || reqH != surfaceH || needsRebake || engine.isDirty) {
        RebakePreviewSurface(reqW, reqH, invert);
    }

    if (glTexture != 0 && surfaceW > 0 && surfaceH > 0) {
        ImVec2 uv1(1.0f, 1.0f);
        if (allocatedCapacityW > 0 && allocatedCapacityH > 0) {
            uv1.x = static_cast<float>(surfaceW) / allocatedCapacityW;
            uv1.y = static_cast<float>(surfaceH) / allocatedCapacityH;
        }
        ImGui::Image((ImTextureID)(intptr_t)glTexture, ImVec2(width, height), ImVec2(0, 0), uv1);
    }
}

void MdEditorView::RebakePreviewSurface(int width, int height, bool invert) {
    if (width <= 0 || height <= 0) return;

    InitGL();

    surfaceW = width;
    surfaceH = height;

    if (surfaceW > allocatedCapacityW || surfaceH > allocatedCapacityH) {
        allocatedCapacityW = std::max(allocatedCapacityW * 2, surfaceW);
        allocatedCapacityH = std::max(allocatedCapacityH * 2, surfaceH);

        previewSurface.create(allocatedCapacityW, allocatedCapacityH, BL_FORMAT_PRGB32);

        glBindTexture(GL_TEXTURE_2D, glTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedCapacityW, allocatedCapacityH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // Rasterize Paper via Blend2D
    BLContext ctx(previewSurface);
    ctx.clear_all();

    // Background backdrop
    BLRgba32 backdrop = invert ? BLRgba32(0x10, 0x12, 0x16, 0xFF) : BLRgba32(0xEB, 0xF0, 0xF5, 0xFF);
    ctx.fill_rect(BLRect(0, 0, surfaceW, surfaceH), backdrop);

    // Calculate Physical Paper placement in mm
    double pxPerMm = (96.0 / 25.4) * previewZoom;
    double pageWMm = engine.config.pageWidthMm;
    double pageWPx = pageWMm * pxPerMm;

    // Horizontally center paper in preview pane
    double originX_mm = ((surfaceW - pageWPx) * 0.5) / pxPerMm;
    originX_mm = std::max(originX_mm, 5.0 / pxPerMm);

    // Vertical millimeter offset from scroll
    double originY_mm = 10.0 - (previewScrollY / pxPerMm);

    ctx.save();
    ctx.scale(pxPerMm, pxPerMm);

    // Tier 1: Committed paper & typography
    engine.RenderToBlend2D(ctx, originX_mm, originY_mm, pageWMm, engine.totalDocumentHeightMm, invert);

    // Tier 2: Live interaction overlay (selection highlights & blinking caret)
    double currentSec = SDL_GetTicks() / 1000.0;
    engine.RenderLiveLayer(ctx, originX_mm, originY_mm, currentSec);

    ctx.restore();
    ctx.end();

    // Upload to OpenGL Texture
    BLImageData imgData;
    previewSurface.get_data(&imgData);
    glBindTexture(GL_TEXTURE_2D, glTexture);

#if defined(__ANDROID__)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, surfaceW, surfaceH, GL_RGBA, GL_UNSIGNED_BYTE, imgData.pixel_data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#else
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, surfaceW, surfaceH, GL_BGRA, GL_UNSIGNED_BYTE, imgData.pixel_data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#endif

    needsRebake = false;
}

void MdEditorView::InsertFormattingPrefix(const std::string& prefix) {
    editorBuffer += "\n" + prefix;
    engine.LoadMarkdown(editorBuffer);
    needsRebake = true;
}

void MdEditorView::WrapSelectionWith(const std::string& prefix, const std::string& suffix) {
    editorBuffer += prefix + "text" + suffix;
    engine.LoadMarkdown(editorBuffer);
    needsRebake = true;
}

void MdEditorView::InsertCodeBlockTemplate(const std::string& lang) {
    editorBuffer += "\n```" + lang + "\n// Write code here\n```\n";
    engine.LoadMarkdown(editorBuffer);
    needsRebake = true;
}

void MdEditorView::InsertTableTemplate(int /*rows*/, int /*cols*/) {
    editorBuffer += "\n| Header 1 | Header 2 | Header 3 |\n"
                    "| :--- | :---: | ---: |\n"
                    "| Data 1 | Data 2 | Data 3 |\n"
                    "| Data 4 | Data 5 | Data 6 |\n\n";
    engine.LoadMarkdown(editorBuffer);
    needsRebake = true;
}

void MdEditorView::InsertMathBlockTemplate() {
    editorBuffer += "\n$$\nE = mc^2\n$$\n\n";
    engine.LoadMarkdown(editorBuffer);
    needsRebake = true;
}

void MdEditorView::InsertTaskItem() {
    editorBuffer += "\n- [ ] New Task";
    engine.LoadMarkdown(editorBuffer);
    needsRebake = true;
}

} // namespace Folio
