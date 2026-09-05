// #pragma once
// #include "imgui.h"
// #include "imgui_internal.h"
// #include "core/engine/canvas_engine.hpp"

// class TextContainerView {
// public:
//     static void RenderContainers(CanvasEngine& canvas, const ImVec2& canvasOrigin) {
//         ImDrawList* drawList = ImGui::GetWindowDrawList();
//         float zoom = static_cast<float>(canvas.transform.zoom);

//         for (auto& tb : canvas.textBoxes) {
//             Point2D screenPos = canvas.transform.WorldToScreen(tb.wordBoxX, tb.wordBoxY);
//             float sx = canvasOrigin.x + static_cast<float>(screenPos.x);
//             float sy = canvasOrigin.y + static_cast<float>(screenPos.y);
//             float sw = static_cast<float>(tb.wordBoxWidth) * zoom;
//             float sh = static_cast<float>(tb.wordBoxHeight) * zoom;

//             const float HANDLE_H = 18.0f;
//             ImVec2 containerMin(sx, sy);
//             ImVec2 containerMax(sx + sw, sy + sh + HANDLE_H);

//             bool isFocused = (canvas.activeTextBoxId == tb.id);
//             bool isHovered = ImGui::IsMouseHoveringRect(containerMin, containerMax);

//             // 1. Draw OneNote Top Handle Bar (Visible on Hover or Active)
//             if (isFocused || isHovered) {
//                 ImVec2 handleMin(sx, sy);
//                 ImVec2 handleMax(sx + sw, sy + HANDLE_H);

//                 // Handle background
//                 drawList->AddRectFilled(handleMin, handleMax, IM_COL32(210, 212, 216, 220), 2.0f);

//                 // Top Grip dots ("••••")
//                 float centerX = sx + (sw * 0.5f);
//                 float dotY = sy + (HANDLE_H * 0.5f);
//                 for (int d = -2; d <= 1; ++d) {
//                     drawList->AddCircleFilled(ImVec2(centerX + (d * 6.0f) + 3.0f, dotY), 1.8f, IM_COL32(90, 95, 105, 255));
//                 }

//                 // Right horizontal size arrows ("◀ ▶")
//                 float arrowX = sx + sw - 14.0f;
//                 drawList->AddTriangleFilled(ImVec2(arrowX - 4, dotY), ImVec2(arrowX - 1, dotY - 3), ImVec2(arrowX - 1, dotY + 3), IM_COL32(90, 95, 105, 255));
//                 drawList->AddTriangleFilled(ImVec2(arrowX + 4, dotY), ImVec2(arrowX + 1, dotY - 3), ImVec2(arrowX + 1, dotY + 3), IM_COL32(90, 95, 105, 255));

//                 // Container Border
//                 drawList->AddRect(handleMin, containerMax, IM_COL32(180, 185, 195, 180), 2.0f);

//                 // Drag Logic on Handle
//                 ImGui::SetCursorScreenPos(handleMin);
//                 ImGui::InvisibleButton((std::string("##Handle_") + std::to_string(tb.id)).c_str(), ImVec2(sw - 20.0f, HANDLE_H));
//                 if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
//                     tb.wordBoxX += ImGui::GetIO().MouseDelta.x / zoom;
//                     tb.wordBoxY += ImGui::GetIO().MouseDelta.y / zoom;
//                     canvas.isDirty = true;
//                 }

//                 // Resize Grip Logic
//                 ImGui::SetCursorScreenPos(ImVec2(sx + sw - 20.0f, sy));
//                 ImGui::InvisibleButton((std::string("##Resize_") + std::to_string(tb.id)).c_str(), ImVec2(20.0f, HANDLE_H));
//                 if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
//                     tb.wordBoxWidth = std::max(120.0, tb.wordBoxWidth + (ImGui::GetIO().MouseDelta.x / zoom));
//                     canvas.isDirty = true;
//                 }
//             }

//             // 2. Editable Text Region
//             ImVec2 textPos(sx + 6.0f, sy + HANDLE_H + 4.0f);
//             ImGui::SetCursorScreenPos(textPos);
//             ImGui::SetWindowFontScale(zoom);

//             ImGui::PushItemWidth(sw - 12.0f);
//             ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
//             ImGui::PushStyleColor(ImGuiCol_Text, tb.textColor);

//             char buffer[2048];
//             strncpy_s(buffer, sizeof(buffer), tb.text.c_str(), _TRUNCATE);

//             std::string inputId = "##TextInput_" + std::to_string(tb.id);
//             if (ImGui::InputTextMultiline(inputId.c_str(), buffer, sizeof(buffer), 
//                 ImVec2(sw - 12.0f, std::max(30.0f, sh - 8.0f)), 
//                 ImGuiInputTextFlags_AllowTabInput)) {
//                 tb.text = buffer;
//                 canvas.isDirty = true;
//             }

//             if (ImGui::IsItemActive()) {
//                 canvas.activeTextBoxId = tb.id;
//             }

//             ImGui::PopStyleColor(2);
//             ImGui::PopItemWidth();
//             ImGui::SetWindowFontScale(1.0f);
//         }
//     }
// };