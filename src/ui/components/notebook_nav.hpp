#pragma once
#include "imgui.h"
#include <vector>
#include <string>
#include <memory>
#include "core/document/document_session.hpp"
#include "core/engine/canvas_engine.hpp"

struct NotebookNav {
    void Render(float x, float y, float totalWidth, float height, DocumentSession& session, CanvasEngine& canvas) {
        auto& ws = session.workspace;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (totalWidth <= 0.0f) totalWidth = avail.x;
        if (height <= 0.0f) height = avail.y;

        float notebookColWidth = totalWidth * 0.52f;

        // 1. Notebooks & Sections Column
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
        ImGui::BeginChild("##NotebookCol", ImVec2(notebookColWidth, 0.0f), true);

        if (ImGui::Button("+ Section", ImVec2(-1, 32))) {
            auto nb = ws.GetActiveNotebook();
            if (nb) {
                nb->sections.push_back(std::make_shared<Section>("New Section"));
                nb->activeSectionIndex = nb->sections.size() - 1;
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            }
        }
        ImGui::Separator();

        for (size_t n = 0; n < ws.notebooks.size(); ++n) {
            auto& nb = ws.notebooks[n];
            if (!nb) continue;

            ImGui::PushID(static_cast<int>(n));
            ImGui::PushStyleColor(ImGuiCol_Text, nb->colorTag);
            bool open = ImGui::TreeNodeEx(nb->name.c_str(), nb->isOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            ImGui::PopStyleColor();

            if (open) {
                for (size_t s = 0; s < nb->sections.size(); ++s) {
                    auto& sec = nb->sections[s];
                    if (!sec) continue;

                    ImGui::PushID(static_cast<int>(s));
                    bool isSecSelected = (ws.activeNotebookIndex == n && nb->activeSectionIndex == s);
                    if (ImGui::Selectable(sec->name.c_str(), isSecSelected)) {
                        ws.activeNotebookIndex = n;
                        nb->activeSectionIndex = s;
                        sec->activePageIndex = 0;
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 4.0f);

        // 2. Pages Column
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.14f, 0.16f, 1.0f));
        ImGui::BeginChild("##PageCol", ImVec2(0.0f, 0.0f), true);

        if (ImGui::Button("+ Add Page", ImVec2(-1, 32))) {
            auto sec = ws.GetActiveNotebook() ? ws.GetActiveNotebook()->GetActiveSection() : nullptr;
            if (sec) {
                sec->pages.push_back(std::make_shared<CanvasPage>("Untitled page"));
                sec->activePageIndex = sec->pages.size() - 1;
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            }
        }
        ImGui::Separator();

        auto activeNb = ws.GetActiveNotebook();
        auto activeSec = activeNb ? activeNb->GetActiveSection() : nullptr;
        if (activeSec) {
            for (size_t p = 0; p < activeSec->pages.size(); ++p) {
                auto& page = activeSec->pages[p];
                if (!page) continue;

                ImGui::PushID(static_cast<int>(p));
                bool isPageSelected = (activeSec->activePageIndex == p);
                if (ImGui::Selectable(page->title.c_str(), isPageSelected)) {
                    activeSec->activePageIndex = p;
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
};