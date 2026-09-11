#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <sstream>
#include <cctype>
#include "imgui.h"
#include "core/storage/pdf_storage.hpp"
#include "core/objects/pdf_container.hpp"
#include "core/document/document_session.hpp"
#include "core/engine/canvas_engine.hpp"
#include "ui/imgui_theme.hpp"
#include "utils/uid_generator.hpp"
#include "utils/guid_generator.hpp"
#include "utils/logger.hpp"

namespace Folio {

enum class PdfPlacementMode {
    OnePagePerCanvas = 0,    // Creates N canvas pages in section, each with 1 PDF page
    AllPagesStacked = 1,     // Creates 1 canvas page with all selected PDF pages stacked vertically
    StandaloneViewer = 2,    // Creates a dedicated PDF document page (background locked by default)
    InsertIntoExistingPage = 3 // Inserts into selected existing page below last known stroke/content
};

class PdfImportModal {
public:
    bool isOpen = false;
    PdfDocumentInfo currentDoc;
    PdfImportMode importMode = PdfImportMode::LocalCopy;
    PdfPlacementMode placementMode = PdfPlacementMode::OnePagePerCanvas;

    // Page selection: 0 = All, 1 = Custom Range
    int pageSelectionType = 0;
    char customRangeBuf[128] = "1";

    bool setAsBackground = true;

    // Target Selection Hierarchy
    std::string targetNotebookGuid;
    std::string targetSectionGuid;
    std::string targetPageGuid;
    bool isTargetingExistingPage = false;
    int existingPagePlacement = 0; // 0 = Below existing content, 1 = Create new page(s) in section

    // Inline creators
    bool showNewSectionInput = false;
    char newSectionNameBuf[128] = "PDF Readings";
    bool showNewPageInput = false;
    char newPageNameBuf[128] = "PDF Notes";

    // Secondary confirmation for long document canvas breakout (>= 20 pages)
    bool confirmLongDocBreakout = false;

    // Default margin and layout constants (in world millimeters)
    static constexpr double DEFAULT_MARGIN_LEFT_MM = 25.0; // 25mm left margin
    static constexpr double DEFAULT_MARGIN_TOP_MM = 30.0;  // 30mm top margin
    static constexpr double DEFAULT_GAP_Y_MM = 15.0;       // 15mm vertical gap between elements

    /**
     * @brief Opens the import dialog for a chosen PDF file.
     */
    void Open(const std::string& filePath, DocumentSession* session) {
        if (!PdfStorage::InspectPdf(filePath, currentDoc)) {
            LOG_ERROR(PdfStorage, "Cannot open import modal: file inspection failed for " + filePath);
            return;
        }

        isOpen = true;
        importMode = PdfImportMode::LocalCopy;
        confirmLongDocBreakout = false;
        showNewSectionInput = false;
        showNewPageInput = false;

        // Long document default recommendation
        if (currentDoc.isLongDocument) {
            placementMode = PdfPlacementMode::StandaloneViewer;
            setAsBackground = true;
        } else {
            placementMode = PdfPlacementMode::OnePagePerCanvas;
            setAsBackground = true;
        }

        pageSelectionType = 0;
        if (currentDoc.pageCount > 1) {
            std::snprintf(customRangeBuf, sizeof(customRangeBuf), "1-%d", currentDoc.pageCount);
        } else {
            std::snprintf(customRangeBuf, sizeof(customRangeBuf), "1");
        }

        // Initialize target notebook, section, and page to active session state
        targetNotebookGuid.clear();
        targetSectionGuid.clear();
        targetPageGuid.clear();
        isTargetingExistingPage = false;
        existingPagePlacement = 0;

        if (session) {
            auto activeNb = session->workspace.GetActiveNotebook();
            if (activeNb) {
                targetNotebookGuid = activeNb->guid;
                auto sec = activeNb->GetActiveSection();
                if (sec) {
                    targetSectionGuid = sec->guid;
                    auto pg = sec->GetActivePage();
                    if (pg) {
                        targetPageGuid = pg->guid;
                        isTargetingExistingPage = true;
                    }
                } else if (!activeNb->sections.empty() && activeNb->sections[0]) {
                    targetSectionGuid = activeNb->sections[0]->guid;
                }
            }
        }
    }

    /**
     * @brief Parses page range strings (e.g., "1, 3-5, 20" or "3-25") into 0-indexed page indices.
     */
    static std::vector<int> ParsePageRange(const std::string& input, int totalPages) {
        std::vector<int> pages;
        if (totalPages <= 0) return pages;

        std::stringstream ss(input);
        std::string token;

        while (std::getline(ss, token, ',')) {
            // Trim whitespace
            size_t start = token.find_first_not_of(" \t\r\n");
            size_t end = token.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            std::string part = token.substr(start, end - start + 1);

            size_t dash = part.find('-');
            if (dash != std::string::npos) {
                std::string sStart = part.substr(0, dash);
                std::string sEnd = part.substr(dash + 1);
                try {
                    int p1 = std::clamp(std::stoi(sStart), 1, totalPages);
                    int p2 = std::clamp(std::stoi(sEnd), 1, totalPages);
                    if (p1 > p2) std::swap(p1, p2);
                    for (int p = p1; p <= p2; ++p) {
                        pages.push_back(p - 1);
                    }
                } catch (...) {}
            } else {
                try {
                    int p = std::clamp(std::stoi(part), 1, totalPages);
                    pages.push_back(p - 1);
                } catch (...) {}
            }
        }

        // Sort and remove duplicates
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
        return pages;
    }

    /**
     * @brief Renders the modal dialog if open.
     */
    void Render(DocumentSession& session, CanvasEngine& canvas, const ThemeManager& theme) {
        if (!isOpen) return;

        ImGui::OpenPopup("Import PDF Document##Modal");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.0f, 750.0f), ImGuiCond_Appearing);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        if (ImGui::BeginPopupModal("Import PDF Document##Modal", &isOpen, flags)) {
            // -------------------------------------------------------------
            // 1. HEADER CARD: FILE METADATA & ICON
            // -------------------------------------------------------------
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float cardW = ImGui::GetContentRegionAvail().x;
            float cardH = 58.0f;

            dl->AddRectFilled(p0, ImVec2(p0.x + cardW, p0.y + cardH), ImGui::GetColorU32(theme.colorSectionBg), 6.0f);
            dl->AddRect(p0, ImVec2(p0.x + cardW, p0.y + cardH), ImGui::GetColorU32(theme.colorBorder), 6.0f);

            // PDF Badge
            ImVec2 badgeMin(p0.x + 12.0f, p0.y + 12.0f);
            ImVec2 badgeMax(p0.x + 50.0f, p0.y + 46.0f);
            dl->AddRectFilled(badgeMin, badgeMax, IM_COL32(234, 67, 53, 240), 4.0f);
            dl->AddText(ImVec2(badgeMin.x + 7.0f, badgeMin.y + 8.0f), IM_COL32(255, 255, 255, 255), "PDF");

            // Title & Info
            std::string displayName = currentDoc.originalFileName;
            if (displayName.length() > 46) displayName = displayName.substr(0, 43) + "...";
            dl->AddText(ImVec2(p0.x + 58.0f, p0.y + 11.0f), ImGui::GetColorU32(theme.colorText), displayName.c_str());

            char metaStr[128];
            double szMb = static_cast<double>(currentDoc.fileSizeBytes) / (1024.0 * 1024.0);
            std::snprintf(metaStr, sizeof(metaStr), "Pages: %d  |  Size: %.2f MB  |  Hash: %s",
                          currentDoc.pageCount, szMb, currentDoc.contentHash.substr(0, 8).c_str());
            dl->AddText(ImVec2(p0.x + 58.0f, p0.y + 32.0f), ImGui::GetColorU32(theme.colorTextMuted), metaStr);

            ImGui::Dummy(ImVec2(cardW, cardH + 10.0f));

            // -------------------------------------------------------------
            // 2. LONG DOCUMENT DETECTION BANNER (>= 20 Pages)
            // -------------------------------------------------------------
            if (currentDoc.isLongDocument) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.95f, 0.60f, 0.10f, 0.15f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.60f, 0.10f, 0.60f));
                ImGui::BeginChild("##LongDocBanner", ImVec2(cardW, 50.0f), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.20f, 1.0f), "  Long Document Detected (%d Pages)", currentDoc.pageCount);
                ImGui::TextWrapped("Breaking 20+ pages into canvas sections can clutter your notebook. We recommend 'Standalone PDF Document Page' below.");
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::Spacing();
            }

            // -------------------------------------------------------------
            // 3. STORAGE INGESTION MODE
            // -------------------------------------------------------------
            ImGui::TextColored(theme.colorPrimary, "Storage & Portability:");
            int modeInt = static_cast<int>(importMode);
            if (ImGui::RadioButton("Make Local Copy in Notebook (Recommended)", &modeInt, 0)) {
                importMode = PdfImportMode::LocalCopy;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Link External Path Only", &modeInt, 1)) {
                importMode = PdfImportMode::ExternalLink;
            }

            if (importMode == PdfImportMode::ExternalLink) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.15f, 1.0f));
                ImGui::TextWrapped("  Warning: If this PDF is moved, renamed, or deleted from its folder, FolioNote will lose access to it.");
                ImGui::PopStyleColor();
            } else {
                ImGui::TextColored(theme.colorTextMuted, "  Copies file into notebook package. Fully portable and self-contained.");
            }
            ImGui::Separator();

            // -------------------------------------------------------------
            // 4. PAGE SELECTION RANGE
            // -------------------------------------------------------------
            ImGui::TextColored(theme.colorPrimary, "Pages to Import:");
            ImGui::RadioButton("All Pages", &pageSelectionType, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Custom Page Range", &pageSelectionType, 1);

            std::vector<int> selectedPages;
            if (pageSelectionType == 0) {
                for (int i = 0; i < currentDoc.pageCount; ++i) selectedPages.push_back(i);
                ImGui::TextColored(theme.colorTextMuted, "  Will import all %d pages (Pages 1 - %d).", currentDoc.pageCount, currentDoc.pageCount);
            } else {
                ImGui::TextUnformatted("  Range:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(240.0f);
                ImGui::InputText("##CustomRange", customRangeBuf, sizeof(customRangeBuf));
                ImGui::SameLine();
                ImGui::TextColored(theme.colorTextMuted, "(e.g., 1, 3-5, 20 or 3-25)");

                selectedPages = ParsePageRange(customRangeBuf, currentDoc.pageCount);
                ImGui::TextColored(theme.colorTextMuted, "  Parsed %zu page(s) selected.", selectedPages.size());
            }
            ImGui::Separator();

            // -------------------------------------------------------------
            // 5. TARGET SELECTION TREE (NOTEBOOKS -> SECTIONS -> PAGES)
            // -------------------------------------------------------------
            std::string targetInfo = "Target: ";
            if (isTargetingExistingPage) {
                targetInfo += "Existing Page (Guid: " + targetPageGuid.substr(0, 8) + "...)";
            } else if (!targetSectionGuid.empty()) {
                targetInfo += "Section (Guid: " + targetSectionGuid.substr(0, 8) + "...) -> New Page(s)";
            } else {
                targetInfo += "None Selected";
            }
            ImGui::TextColored(theme.colorPrimary, "Target Location (Notebooks -> Sections -> Pages):");

            ImGui::BeginChild("##HierarchyTreeChild", ImVec2(cardW, 140.0f), true);
            for (const auto& nb : session.workspace.notebooks) {
                if (!nb) continue;
                ImGuiTreeNodeFlags nbFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (nb->guid == targetNotebookGuid) nbFlags |= ImGuiTreeNodeFlags_DefaultOpen;

                std::string nbLabel = "Book: " + nb->name;
                bool nbOpen = ImGui::TreeNodeEx(nb->guid.c_str(), nbFlags, "%s", nbLabel.c_str());
                if (ImGui::IsItemClicked()) {
                    targetNotebookGuid = nb->guid;
                }

                if (nbOpen) {
                    // 1. Root sections
                    for (const auto& sec : nb->sections) {
                        RenderSectionInTree(sec, nb);
                    }

                    // 2. Section Groups
                    for (const auto& grp : nb->sectionGroups) {
                        if (!grp) continue;
                        std::string grpLabel = "Group: " + grp->name;
                        if (ImGui::TreeNodeEx(grp->guid.c_str(), ImGuiTreeNodeFlags_OpenOnArrow, "%s", grpLabel.c_str())) {
                            for (const auto& sec : grp->sections) {
                                RenderSectionInTree(sec, nb);
                            }
                            ImGui::TreePop();
                        }
                    }

                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();

            // Inline Section & Page Creator Buttons
            ImGui::Spacing();
            if (!showNewSectionInput && !showNewPageInput) {
                if (ImGui::SmallButton("+ Add New Section...")) {
                    showNewSectionInput = true;
                    showNewPageInput = false;
                }
                ImGui::SameLine();
                if (!targetSectionGuid.empty()) {
                    if (ImGui::SmallButton("+ Add New Page in Section...")) {
                        showNewPageInput = true;
                        showNewSectionInput = false;
                    }
                }
            } else if (showNewSectionInput) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
                ImGui::TextUnformatted("New Section Name:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("##NewSectionName", newSectionNameBuf, sizeof(newSectionNameBuf));
                ImGui::SameLine();
                if (ImGui::Button("Create##Sec")) {
                    auto activeNb = session.workspace.GetActiveNotebook();
                    if (activeNb && newSectionNameBuf[0] != '\0') {
                        auto newSec = std::make_shared<Section>(newSectionNameBuf);
                        activeNb->AddSection(newSec);
                        targetSectionGuid = newSec->guid;
                        isTargetingExistingPage = false;
                        targetPageGuid.clear();
                        showNewSectionInput = false;
                        LOG_INFO(PdfStorage, "Created new section: '" + std::string(newSectionNameBuf) + "'");
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##Sec")) {
                    showNewSectionInput = false;
                }
                ImGui::PopStyleVar();
            } else if (showNewPageInput) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
                ImGui::TextUnformatted("New Page Name:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("##NewPageName", newPageNameBuf, sizeof(newPageNameBuf));
                ImGui::SameLine();
                if (ImGui::Button("Create##Pg")) {
                    auto activeNb = session.workspace.GetActiveNotebook();
                    if (activeNb && newPageNameBuf[0] != '\0') {
                        auto targetSec = activeNb->FindSectionByGuid(targetSectionGuid);
                        if (targetSec) {
                            auto newPg = std::make_shared<CanvasPage>(newPageNameBuf);
                            targetSec->AddPage(newPg);
                            targetPageGuid = newPg->guid;
                            isTargetingExistingPage = true;
                            showNewPageInput = false;
                            LOG_INFO(PdfStorage, "Created new page: '" + std::string(newPageNameBuf) + "'");
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##Pg")) {
                    showNewPageInput = false;
                }
                ImGui::PopStyleVar();
            }
            ImGui::Separator();

            // -------------------------------------------------------------
            // 6. DESTINATION PLACEMENT MODE
            // -------------------------------------------------------------
            ImGui::TextColored(theme.colorPrimary, "Placement & Margins:");

            if (isTargetingExistingPage) {
                // Existing page options
                if (ImGui::RadioButton("Insert into Selected Page below last known stroke / content", &existingPagePlacement, 0)) {
                    placementMode = PdfPlacementMode::InsertIntoExistingPage;
                }
                ImGui::TextColored(theme.colorTextMuted, "    Layout: 25mm left margin, placed 15mm below existing canvas strokes/objects.");

                if (ImGui::RadioButton("Create New Canvas Page(s) in this Section instead", &existingPagePlacement, 1)) {
                    isTargetingExistingPage = false;
                    placementMode = PdfPlacementMode::OnePagePerCanvas;
                }
            } else {
                // Section options (new pages)
                int pMode = static_cast<int>(placementMode);
                if (ImGui::RadioButton("One Canvas Page per PDF Page (Printout)", &pMode, 0)) {
                    placementMode = PdfPlacementMode::OnePagePerCanvas;
                }
                if (ImGui::RadioButton("All Pages Stacked on a Single Canvas", &pMode, 1)) {
                    placementMode = PdfPlacementMode::AllPagesStacked;
                }
                if (ImGui::RadioButton("Standalone PDF Document Page (Dedicated Viewer)", &pMode, 2)) {
                    placementMode = PdfPlacementMode::StandaloneViewer;
                    setAsBackground = true; // Forced true for standalone viewer
                }
                ImGui::TextColored(theme.colorTextMuted, "    Layout: 25mm left margin, 30mm top margin with 15mm vertical gaps.");
            }

            // Background layer locking
            ImGui::Spacing();
            if (placementMode == PdfPlacementMode::StandaloneViewer) {
                setAsBackground = true;
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("Set as Background (Locked Layer)", &setAsBackground);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextColored(theme.colorTextMuted, "(Required for Standalone Document)");
            } else {
                ImGui::Checkbox("Set as Background (Locked Layer)", &setAsBackground);
                ImGui::SameLine();
                ImGui::TextColored(theme.colorTextMuted, "(Protects from accidental moving/eraser hits while inking)");
            }
            ImGui::Separator();

            // -------------------------------------------------------------
            // 7. SECONDARY CONFIRMATION FOR 20+ PAGES CANVAS BREAKOUT
            // -------------------------------------------------------------
            bool isBigCanvasBreakout = (selectedPages.size() >= 20 && placementMode != PdfPlacementMode::StandaloneViewer);
            if (isBigCanvasBreakout) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.40f, 0.40f, 1.0f));
                ImGui::Checkbox("Are you sure you want to break this PDF into 20+ canvas pages?", &confirmLongDocBreakout);
                ImGui::PopStyleColor();
            }

            // -------------------------------------------------------------
            // 8. ACTION BUTTONS
            // -------------------------------------------------------------
            ImGui::Spacing();
            bool canCommit = !selectedPages.empty() && (!targetSectionGuid.empty() || !targetPageGuid.empty());
            if (isBigCanvasBreakout && !confirmLongDocBreakout) {
                canCommit = false;
            }

            if (!canCommit) ImGui::BeginDisabled(true);

            char btnLabel[64];
            std::snprintf(btnLabel, sizeof(btnLabel), "Import (%zu Page%s)###BtnCommitPdf",
                          selectedPages.size(), selectedPages.size() == 1 ? "" : "s");

            if (ImGui::Button(btnLabel, ImVec2(180.0f, 36.0f))) {
                CommitImport(session, canvas, selectedPages);
                isOpen = false;
                ImGui::CloseCurrentPopup();
            }
            if (!canCommit) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 36.0f))) {
                isOpen = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

private:
    void RenderSectionInTree(const std::shared_ptr<Section>& sec, const std::shared_ptr<Notebook>& nb) {
        if (!sec) return;

        bool isSecSelected = (sec->guid == targetSectionGuid && !isTargetingExistingPage);
        ImGuiTreeNodeFlags secFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (isSecSelected) secFlags |= ImGuiTreeNodeFlags_Selected;
        if (sec->guid == targetSectionGuid) secFlags |= ImGuiTreeNodeFlags_DefaultOpen;

        std::string secLabel = "Section: " + sec->name + " (" + std::to_string(sec->pages.size()) + " pages)";
        bool secOpen = ImGui::TreeNodeEx(sec->guid.c_str(), secFlags, "%s", secLabel.c_str());
        if (ImGui::IsItemClicked()) {
            targetNotebookGuid = nb->guid;
            targetSectionGuid = sec->guid;
            targetPageGuid.clear();
            isTargetingExistingPage = false;
        }

        if (secOpen) {
            for (const auto& pg : sec->pages) {
                if (!pg) continue;
                bool isPgSelected = (pg->guid == targetPageGuid && isTargetingExistingPage);
                std::string pageTitle = pg->title.empty() ? "Untitled Page" : pg->title;
                std::string pgLabel = "  Page: " + pageTitle;

                if (ImGui::Selectable(pgLabel.c_str(), isPgSelected)) {
                    targetNotebookGuid = nb->guid;
                    targetSectionGuid = sec->guid;
                    targetPageGuid = pg->guid;
                    isTargetingExistingPage = true;
                }
            }
            ImGui::TreePop();
        }
    }

    void CommitImport(DocumentSession& session, CanvasEngine& canvas, const std::vector<int>& pagesToImport) {
        if (pagesToImport.empty()) return;

        PdfDocumentInfo docInfo;
        if (!PdfStorage::IngestPdf(currentDoc.originalPath, &session, importMode, docInfo)) {
            LOG_ERROR(PdfStorage, "CommitImport failed: ingestion failed for " + currentDoc.originalPath);
            return;
        }

        // Find target notebook
        std::shared_ptr<Notebook> targetNb = nullptr;
        for (const auto& nb : session.workspace.notebooks) {
            if (nb && nb->guid == targetNotebookGuid) {
                targetNb = nb;
                break;
            }
        }
        if (!targetNb) targetNb = session.workspace.GetActiveNotebook();
        if (!targetNb) return;

        // Set active notebook in workspace
        for (size_t i = 0; i < session.workspace.notebooks.size(); ++i) {
            if (session.workspace.notebooks[i] == targetNb) {
                session.workspace.activeNotebookIndex = i;
                break;
            }
        }

        auto targetSec = targetNb->FindSectionByGuid(targetSectionGuid);
        if (!targetSec) targetSec = targetNb->GetActiveSection();
        if (!targetSec) return;
        targetNb->SetActiveSection(targetSec);

        std::string baseDocTitle = docInfo.originalFileName;
        if (baseDocTitle.size() > 4 && baseDocTitle.substr(baseDocTitle.size() - 4) == ".pdf") {
            baseDocTitle = baseDocTitle.substr(0, baseDocTitle.size() - 4);
        }

        // =========================================================================
        // CASE A: INSERT INTO EXISTING PAGE BELOW LAST KNOWN STROKE / CONTENT
        // =========================================================================
        if (isTargetingExistingPage) {
            std::shared_ptr<CanvasPage> targetPg = nullptr;
            size_t pgIdx = 0;
            for (size_t i = 0; i < targetSec->pages.size(); ++i) {
                if (targetSec->pages[i] && targetSec->pages[i]->guid == targetPageGuid) {
                    targetPg = targetSec->pages[i];
                    pgIdx = i;
                    break;
                }
            }
            if (!targetPg) targetPg = targetSec->GetActivePage();

            if (targetPg) {
                targetSec->activePageIndex = pgIdx;

                // Detect bottom of existing content on this canvas
                double startY = DEFAULT_MARGIN_TOP_MM;
                bool hasContent = false;
                double maxBottom = 0.0;

                for (const auto& obj : targetPg->objects) {
                    if (!obj || !obj->isVisible) continue;
                    double b = obj->bounds.maxY;
                    if (!hasContent || b > maxBottom) {
                        maxBottom = b;
                    }
                    hasContent = true;
                }

                if (hasContent) {
                    startY = maxBottom + DEFAULT_GAP_Y_MM;
                }

                double curY = startY;
                for (size_t i = 0; i < pagesToImport.size(); ++i) {
                    int pIdx = pagesToImport[i];
                    auto pdfObj = std::make_shared<PdfContainer>(
                        docInfo.packagePath, docInfo.originalFileName, pIdx, docInfo.pageCount,
                        DEFAULT_MARGIN_LEFT_MM, curY, 210.0, 297.0, setAsBackground
                    );
                    pdfObj->resolvedDiskPath = docInfo.diskPath;
                    pdfObj->isExternalLink = docInfo.isExternal;
                    pdfObj->guuid = GUIDGenerator::GenerateV4();
                    pdfObj->uid = UIDGenerator::Next();
                    pdfObj->EnsurePageLoaded();
                    pdfObj->UpdateBounds();

                    targetPg->AddObject(pdfObj);
                    curY += (pdfObj->worldHeight + DEFAULT_GAP_Y_MM);
                }

                LOG_INFO(PdfStorage, "Inserted " + std::to_string(pagesToImport.size()) +
                         " PDF page(s) into existing page '" + targetPg->title + "' starting at Y=" +
                         std::to_string(static_cast<int>(startY)) + " mm");
            }
        }
        // =========================================================================
        // CASE B: ONE CANVAS PAGE PER PDF PAGE
        // =========================================================================
        else if (placementMode == PdfPlacementMode::OnePagePerCanvas) {
            size_t firstCreatedIdx = targetSec->pages.size();
            for (size_t i = 0; i < pagesToImport.size(); ++i) {
                int pIdx = pagesToImport[i];
                std::string pageTitle = baseDocTitle + " (Page " + std::to_string(pIdx + 1) + ")";

                auto newPage = std::make_shared<CanvasPage>(pageTitle);
                newPage->guid = GUIDGenerator::GenerateV4();

                auto pdfObj = std::make_shared<PdfContainer>(
                    docInfo.packagePath, docInfo.originalFileName, pIdx, docInfo.pageCount,
                    DEFAULT_MARGIN_LEFT_MM, DEFAULT_MARGIN_TOP_MM, 210.0, 297.0, setAsBackground
                );
                pdfObj->resolvedDiskPath = docInfo.diskPath;
                pdfObj->isExternalLink = docInfo.isExternal;
                pdfObj->guuid = GUIDGenerator::GenerateV4();
                pdfObj->uid = UIDGenerator::Next();
                pdfObj->EnsurePageLoaded();
                pdfObj->UpdateBounds();

                newPage->AddObject(pdfObj);
                targetSec->AddPage(newPage);
            }
            targetSec->activePageIndex = firstCreatedIdx;
        }
        // =========================================================================
        // CASE C: ALL PAGES STACKED ON SINGLE CANVAS
        // =========================================================================
        else if (placementMode == PdfPlacementMode::AllPagesStacked) {
            std::string pageTitle = baseDocTitle + " (" + std::to_string(pagesToImport.size()) + " Pages)";
            auto newPage = std::make_shared<CanvasPage>(pageTitle);
            newPage->guid = GUIDGenerator::GenerateV4();

            double curY = DEFAULT_MARGIN_TOP_MM;
            for (size_t i = 0; i < pagesToImport.size(); ++i) {
                int pIdx = pagesToImport[i];
                auto pdfObj = std::make_shared<PdfContainer>(
                    docInfo.packagePath, docInfo.originalFileName, pIdx, docInfo.pageCount,
                    DEFAULT_MARGIN_LEFT_MM, curY, 210.0, 297.0, setAsBackground
                );
                pdfObj->resolvedDiskPath = docInfo.diskPath;
                pdfObj->isExternalLink = docInfo.isExternal;
                pdfObj->guuid = GUIDGenerator::GenerateV4();
                pdfObj->uid = UIDGenerator::Next();
                pdfObj->EnsurePageLoaded();
                pdfObj->UpdateBounds();

                newPage->AddObject(pdfObj);
                curY += (pdfObj->worldHeight + DEFAULT_GAP_Y_MM);
            }

            targetSec->AddPage(newPage);
            targetSec->activePageIndex = targetSec->pages.size() - 1;
        }
        // =========================================================================
        // CASE D: STANDALONE PDF VIEWER PAGE
        // =========================================================================
        else if (placementMode == PdfPlacementMode::StandaloneViewer) {
            std::string pageTitle = "[PDF] " + baseDocTitle;
            auto newPage = std::make_shared<CanvasPage>(pageTitle);
            newPage->guid = GUIDGenerator::GenerateV4();
            newPage->isDedicatedPdf = true;
            // Store portable package-relative path for local copies, or absolute path for external links
            newPage->dedicatedPdfPath = docInfo.isExternal ? docInfo.diskPath : docInfo.packagePath;
            newPage->isModified = true;

            auto pdfObj = std::make_shared<PdfContainer>(
                docInfo.packagePath, docInfo.originalFileName, 0, docInfo.pageCount,
                DEFAULT_MARGIN_LEFT_MM, DEFAULT_MARGIN_TOP_MM, 210.0, 297.0, true // locked background
            );
            pdfObj->resolvedDiskPath = docInfo.diskPath;
            pdfObj->isExternalLink = docInfo.isExternal;
            pdfObj->guuid = GUIDGenerator::GenerateV4();
            pdfObj->uid = UIDGenerator::Next();
            pdfObj->EnsurePageLoaded();
            pdfObj->UpdateBounds();

            newPage->AddObject(pdfObj);
            targetSec->AddPage(newPage);
            targetSec->activePageIndex = targetSec->pages.size() - 1;
        }

        session.workspace.FlushActiveNotebookAsync();
        canvas.needsFullRebake = true;
        canvas.isDirty = true;
        LOG_INFO(PdfStorage, "Successfully imported PDF '" + docInfo.originalFileName +
                 "' with " + std::to_string(pagesToImport.size()) + " pages into section '" + targetSec->name + "'");
    }
};

} // namespace Folio
