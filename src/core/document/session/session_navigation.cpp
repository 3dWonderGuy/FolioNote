/**
 * =========================================================================================
 * @file session_navigation.cpp
 * @brief Implementation of DocumentSession page and section navigation, hierarchy mutation,
 *        and camera viewport continuity caching.
 * =========================================================================================
 *
 * ARCHITECTURAL PROCESS:
 * - Manages sequential and indexed page transitions across sections and section groups.
 * - Provides cross-notebook navigation fallback when resolving global hyperlinks.
 * - Caches in-memory camera pan and zoom transformations for seamless viewport continuity.
 */

#include "core/document/document_session.hpp"
#include "utils/logger.hpp"
#include <SDL3/SDL.h>

// -----------------------------------------------------------------------------
// Sequential Page Navigation
// -----------------------------------------------------------------------------

/**
 * @brief Advances to the next sequential CanvasPage within the active section.
 * Touches the newly activated page, lazy-loads it if evicted, and notifies observers.
 *
 * @return true if navigation succeeded, false if already at the last page or no section is active.
 */
bool DocumentSession::NextPage() {
    auto sec = GetActiveSection();
    if (!sec || sec->pages.empty()) return false;

    if (sec->activePageIndex + 1 < sec->pages.size()) {
        auto oldPage = GetActivePage();
        sec->activePageIndex++;
        auto newPage = GetActivePage();
        if (newPage) {
            newPage->Touch();
        }
        NotifyActivePageChanged(newPage, oldPage);
        NotifyHistoryChanged();
        LOG_INFO(DocumentSession, "Navigated forward to page: " + (newPage ? newPage->title : "null"));
        return true;
    }
    return false;
}

/**
 * @brief Steps backward to the previous sequential CanvasPage within the active section.
 * Touches the newly activated page, lazy-loads it if evicted, and notifies observers.
 *
 * @return true if navigation succeeded, false if already at the first page.
 */
bool DocumentSession::PreviousPage() {
    auto sec = GetActiveSection();
    if (!sec || sec->pages.empty()) return false;

    if (sec->activePageIndex > 0) {
        auto oldPage = GetActivePage();
        sec->activePageIndex--;
        auto newPage = GetActivePage();
        if (newPage) {
            newPage->Touch();
        }
        NotifyActivePageChanged(newPage, oldPage);
        NotifyHistoryChanged();
        LOG_INFO(DocumentSession, "Navigated backward to page: " + (newPage ? newPage->title : "null"));
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Direct Deep Navigation & Cross-Notebook Lookup
// -----------------------------------------------------------------------------

/**
 * @brief Navigates directly to any CanvasPage by its persistent GUID across the active notebook
 *        or resident workspace notebooks.
 * Traverses root sections, section groups, and sub-groups to resolve the target page,
 * switches the active section if necessary, lazy-loads page content into RAM, and notifies observers.
 *
 * @param pageGuid Persistent UUID v4 of the destination page.
 * @return true if page was located and activated; false if not found.
 */
bool DocumentSession::NavigateToPage(const std::string& pageGuid) {
    if (pageGuid.empty()) return false;
    auto nb = GetActiveNotebook();
    if (!nb) return false;

    auto oldPage = GetActivePage();
    auto oldSec = GetActiveSection();

    auto searchSection = [&](const std::shared_ptr<Section>& sec) -> bool {
        if (!sec) return false;
        for (size_t i = 0; i < sec->pages.size(); ++i) {
            if (sec->pages[i] && sec->pages[i]->guid == pageGuid) {
                if (sec != oldSec) {
                    nb->SetActiveSection(sec);
                    NotifyActiveSectionChanged(sec, oldSec);
                }
                sec->activePageIndex = i;
                auto newPage = GetActivePage();
                if (newPage) {
                    newPage->Touch();
                }
                NotifyActivePageChanged(newPage, oldPage);
                NotifyHistoryChanged();
                LOG_INFO(DocumentSession, "NavigateToPage: Activated page '" + (newPage ? newPage->title : "") + "' [" + pageGuid + "]");
                return true;
            }
        }
        return false;
    };

    // 1. Scan root sections in active notebook
    for (const auto& sec : nb->sections) {
        if (searchSection(sec)) return true;
    }

    // 2. Scan section groups and nested sub-groups in active notebook
    for (const auto& grp : nb->sectionGroups) {
        if (!grp) continue;
        for (const auto& sec : grp->sections) {
            if (searchSection(sec)) return true;
        }
        for (const auto& sub : grp->subGroups) {
            if (!sub) continue;
            for (const auto& sec : sub->sections) {
                if (searchSection(sec)) return true;
            }
        }
    }

    // 3. Fallback: Search all other resident notebooks in the workspace for cross-notebook navigation
    for (const auto& otherNb : workspace.notebooks) {
        if (!otherNb || otherNb == nb) continue;
        bool foundInOther = false;
        auto checkSection = [&](const std::shared_ptr<Section>& sec) -> bool {
            if (!sec) return false;
            for (const auto& p : sec->pages) {
                if (p && p->guid == pageGuid) return true;
            }
            return false;
        };
        for (const auto& sec : otherNb->sections) {
            if (checkSection(sec)) { foundInOther = true; break; }
        }
        if (!foundInOther) {
            for (const auto& grp : otherNb->sectionGroups) {
                if (!grp) continue;
                for (const auto& sec : grp->sections) {
                    if (checkSection(sec)) { foundInOther = true; break; }
                }
                if (foundInOther) break;
                for (const auto& sub : grp->subGroups) {
                    if (!sub) continue;
                    for (const auto& sec : sub->sections) {
                        if (checkSection(sec)) { foundInOther = true; break; }
                    }
                    if (foundInOther) break;
                }
            }
        }
        if (foundInOther) {
            LOG_INFO(DocumentSession, "NavigateToPage: Found page [" + pageGuid + "] in notebook '" + otherNb->name + "'. Switching notebook.");
            if (OpenNotebook(otherNb->guid)) {
                return NavigateToPage(pageGuid);
            }
        }
    }

    LOG_WARN(DocumentSession, "NavigateToPage failed: Page GUID not found in workspace: " + pageGuid);
    return false;
}

/**
 * @brief Navigates directly to a Section by its persistent GUID in the active notebook.
 * Activates the section, lazy-loads its active page, and emits notification events.
 *
 * @param sectionGuid Persistent UUID v4 of the target section.
 * @return true if section was found and activated; false otherwise.
 */
bool DocumentSession::NavigateToSection(const std::string& sectionGuid) {
    if (sectionGuid.empty()) return false;
    auto nb = GetActiveNotebook();
    if (!nb) return false;

    auto targetSec = nb->FindSectionByGuid(sectionGuid);
    if (!targetSec) {
        LOG_WARN(DocumentSession, "NavigateToSection failed: Section GUID not found: " + sectionGuid);
        return false;
    }

    auto oldSec = GetActiveSection();
    auto oldPage = GetActivePage();

    nb->SetActiveSection(targetSec);
    auto newPage = GetActivePage();
    if (newPage) {
        newPage->Touch();
    }

    NotifyActiveSectionChanged(targetSec, oldSec);
    if (newPage != oldPage) {
        NotifyActivePageChanged(newPage, oldPage);
        NotifyHistoryChanged();
    }
    LOG_INFO(DocumentSession, "NavigateToSection: Activated section '" + targetSec->name + "' [" + sectionGuid + "]");
    return true;
}

// -----------------------------------------------------------------------------
// Page Creation, Deletion, Duplication & Reordering
// -----------------------------------------------------------------------------

/**
 * @brief Factory method: Instantiates and appends a new CanvasPage to the active section.
 * Automatically sets the newly created page as active, marks it modified, registers it
 * for persistence, and broadcasts creation and active page events to observers.
 *
 * @param title Display title for the new page (defaults to "New Untitled").
 * @param parentGuid Optional parent page GUID for hierarchical subpage nesting.
 * @param level Nesting hierarchy level (0 = top-level page, 1 = subpage, 2 = sub-subpage).
 * @return std::shared_ptr<CanvasPage> Pointer to newly created and activated CanvasPage.
 */
std::shared_ptr<CanvasPage> DocumentSession::CreateNewPage(std::string title,
                                                          std::string parentGuid,
                                                          int32_t level) {
    auto sec = GetActiveSection();
    if (!sec) {
        LOG_ERROR(DocumentSession, "CreateNewPage rejected: No active section present.");
        return nullptr;
    }

    auto oldPage = GetActivePage();
    auto newPage = sec->CreatePage(std::move(title), std::move(parentGuid), level);
    if (!newPage) return nullptr;

    sec->activePageIndex = sec->pages.size() - 1;
    newPage->Touch();
    newPage->isModified = true;

    // Schedule async persistence for initial page record
    workspace.repository.SavePageAsync(newPage, sec->guid, newPage->sortOrder);

    NotifyPageCreated(newPage);
    NotifyActivePageChanged(newPage, oldPage);
    NotifyHistoryChanged();
    LOG_INFO(DocumentSession, "CreateNewPage: Created and activated '" + newPage->title + "' [" + newPage->guid + "]");
    return newPage;
}

/**
 * @brief Deletes or soft-deletes the currently active CanvasPage.
 * Guarantees section safety: if the active page is the only page in the section,
 * a clean blank "Untitled page" is automatically created first so the section is never empty.
 *
 * @param moveToTrash If true, moves to recycle bin in SQLite (30-day retention window).
 * @return true if active page was successfully removed.
 */
bool DocumentSession::DeleteActivePage(bool moveToTrash) {
    auto sec = GetActiveSection();
    if (!sec || sec->pages.empty()) return false;

    auto oldPage = GetActivePage();
    if (!oldPage) return false;

    std::string deletedGuid = oldPage->guid;

    // Invariant: Section must always contain at least one valid page.
    // If deleting the only remaining page, generate a replacement blank page first.
    if (sec->pages.size() <= 1) {
        sec->CreatePage("Untitled page");
    }

    if (moveToTrash) {
        workspace.repository.SoftDeletePageAsync(deletedGuid);
        LOG_INFO(DocumentSession, "DeleteActivePage: Soft-deleted page [" + deletedGuid + "] to recycle bin");
    }

    sec->RemovePage(deletedGuid);
    auto newPage = GetActivePage();
    if (newPage) {
        newPage->Touch();
    }

    NotifyPageDeleted(deletedGuid);
    NotifyActivePageChanged(newPage, oldPage);
    NotifyHistoryChanged();
    return true;
}

/**
 * @brief Creates a deep duplicate of the active CanvasPage with cloned objects and fresh UUIDs.
 * Inserts the clone immediately after the active page, sets it active, and persists it.
 *
 * @return std::shared_ptr<CanvasPage> Cloned and activated page pointer.
 */
std::shared_ptr<CanvasPage> DocumentSession::DuplicateActivePage() {
    auto sec = GetActiveSection();
    if (!sec) return nullptr;

    auto oldPage = GetActivePage();
    if (!oldPage) return nullptr;

    auto clone = oldPage->Clone();
    if (!clone) return nullptr;

    size_t insertIdx = sec->activePageIndex + 1;
    sec->InsertPage(insertIdx, clone);
    sec->activePageIndex = insertIdx;
    clone->Touch();

    workspace.repository.SavePageAsync(clone, sec->guid, clone->sortOrder);

    NotifyPageCreated(clone);
    NotifyActivePageChanged(clone, oldPage);
    NotifyHistoryChanged();
    LOG_INFO(DocumentSession, "DuplicateActivePage: Cloned '" + oldPage->title + "' into new page [" + clone->guid + "]");
    return clone;
}

/**
 * @brief Moves the active page forward (+1) or backward (-1) in display sequence.
 * @param delta Direction offset (-1 for up/earlier, +1 for down/later).
 * @return true if page moved successfully; false if already at boundary.
 */
bool DocumentSession::MoveActivePage(int delta) {
    auto sec = GetActiveSection();
    if (!sec || sec->pages.size() <= 1) return false;

    int newIdx = static_cast<int>(sec->activePageIndex) + delta;
    if (newIdx < 0 || newIdx >= static_cast<int>(sec->pages.size())) {
        return false;
    }

    bool ok = sec->MovePage(sec->activePageIndex, static_cast<size_t>(newIdx));
    if (ok) {
        auto page = GetActivePage();
        NotifyActivePageChanged(page, page);
    }
    return ok;
}

// -----------------------------------------------------------------------------
// Viewport Continuity & Spatial Query
// -----------------------------------------------------------------------------

/**
 * @brief Caches the camera pan and zoom viewport state for a specific page in RAM.
 *
 * MATHEMATICAL PROJECTION:
 *   P_screen = (P_world + panMm) * (DPI / 25.4 * zoom)
 *   P_world  = (P_screen / (DPI / 25.4 * zoom)) - panMm
 *
 * Persists in RAM throughout the active application run without time-based eviction,
 * ensuring that users return to their exact viewport position when switching pages.
 * Starts clean (homed at 0, 0, 1.0x) upon application reboot.
 *
 * @param pageGuid Target CanvasPage persistent GUID.
 * @param panX World X pan coordinate in millimeters.
 * @param panY World Y pan coordinate in millimeters.
 * @param zoom Camera magnification scale factor (e.g. 1.0 = 100%).
 */
void DocumentSession::SavePageViewport(const std::string& pageGuid, double panX, double panY, double zoom) {
    auto page = workspace.FindPageByGuid(pageGuid);
    if (page) {
        page->inMemoryViewport.panXMm = panX;
        page->inMemoryViewport.panYMm = panY;
        page->inMemoryViewport.zoom = zoom;
        page->inMemoryViewport.hasCustomViewport = true;
        page->inMemoryViewport.lastViewportAccessMs = SDL_GetTicks();
    }
}

/**
 * @brief Retrieves the cached in-memory camera viewport for a page.
 * @param pageGuid Target CanvasPage persistent GUID.
 * @param outPanX Output world X pan in mm.
 * @param outPanY Output world Y pan in mm.
 * @param outZoom Output camera magnification factor.
 * @return true if a custom in-memory viewport was recorded; false if page is default/unvisited.
 */
bool DocumentSession::GetPageViewport(const std::string& pageGuid, double& outPanX, double& outPanY, double& outZoom) const {
    auto page = workspace.FindPageByGuid(pageGuid);
    if (page && page->inMemoryViewport.hasCustomViewport) {
        outPanX = page->inMemoryViewport.panXMm;
        outPanY = page->inMemoryViewport.panYMm;
        outZoom = page->inMemoryViewport.zoom;
        return true;
    }
    return false;
}

/**
 * @brief Queries all objects on the active page that intersect the camera viewport.
 * @param viewport Current camera viewport (frustum bounds and zoom).
 * @return Vector of visible canvas objects to be rendered by Blend2D.
 */
std::vector<std::shared_ptr<CanvasObject>> DocumentSession::QueryVisible(const Viewport& viewport) const {
    auto activePage = GetActivePage();
    if (!activePage) return {};
    return activePage->QueryVisible(viewport);
}

// -----------------------------------------------------------------------------
// Dynamic Canvas Deep Linking & Targeted Navigation
// -----------------------------------------------------------------------------

/**
 * @brief Navigates to a specific canvas coordinate on any page, centering the viewport on that point.
 *
 * MATHEMATICAL CENTERING DERIVATION:
 * The world-to-screen camera projection is:
 *   P_screen = (P_world + panMm) * (DPI / 25.4 * zoom)
 * Setting P_screen to the center of the viewport (W_screen / 2, H_screen / 2):
 *   (targetWorldXMm + panXMm) * scale = W_screen * 0.5
 *   (targetWorldYMm + panYMm) * scale = H_screen * 0.5
 *
 * Solving for camera pan offsets:
 *   panXMm = (W_screen * 0.5 / scale) - targetWorldXMm
 *   panYMm = (H_screen * 0.5 / scale) - targetWorldYMm
 * where scale = pixelsPerMm * zoom.
 *
 * @param pageGuid Target CanvasPage GUID.
 * @param targetWorldXMm Destination X coordinate in world millimeters.
 * @param targetWorldYMm Destination Y coordinate in world millimeters.
 * @param screenWidthPx Current viewport rendering width in pixels.
 * @param screenHeightPx Current viewport rendering height in pixels.
 * @param pixelsPerMm Physical display density (e.g. 96.0 / 25.4).
 * @param zoom Target camera zoom scale (defaults to 1.0x).
 * @return true if navigation succeeded and viewport was homed to target point.
 */
bool DocumentSession::NavigateToCanvasLocation(const std::string& pageGuid,
                                              double targetWorldXMm, double targetWorldYMm,
                                              double screenWidthPx, double screenHeightPx,
                                              double pixelsPerMm, double zoom) {
    if (pageGuid.empty()) return false;
    double scale = pixelsPerMm * zoom;
    if (scale <= 1e-4) scale = 1.0;

    double panX = (screenWidthPx * 0.5 / scale) - targetWorldXMm;
    double panY = (screenHeightPx * 0.5 / scale) - targetWorldYMm;

    // Cache pre-computed centered viewport coordinates
    SavePageViewport(pageGuid, panX, panY, zoom);

    // Switch to target page
    return NavigateToPage(pageGuid);
}

/**
 * @brief Navigates to a specific CanvasObject by UID on any page, centering the viewport on its
 *        bounding box centroid and optionally selecting the object.
 *
 * MATHEMATICAL DERIVATION:
 * Evaluates the Axis-Aligned Bounding Box (AABB) of the target entity:
 *   Center_X = (bounds.minX + bounds.maxX) * 0.5
 *   Center_Y = (bounds.minY + bounds.maxY) * 0.5
 * Then delegates to NavigateToCanvasLocation(pageGuid, Center_X, Center_Y, ...).
 *
 * @param pageGuid Target CanvasPage persistent GUID.
 * @param objectUid Unique runtime ID of the target object.
 * @param screenWidthPx Current screen width in pixels.
 * @param screenHeightPx Current screen height in pixels.
 * @param pixelsPerMm Physical display density.
 * @param selectObject If true, clears active selection and selects the target entity.
 * @param zoom Target camera magnification scale.
 * @return true if object was found and centered in viewport.
 */
bool DocumentSession::NavigateToObject(const std::string& pageGuid, uint32_t objectUid,
                                      double screenWidthPx, double screenHeightPx,
                                      double pixelsPerMm, bool selectObject,
                                      double zoom) {
    if (pageGuid.empty() || objectUid == 0) return false;

    // Ensure target page is navigated/loaded
    if (!NavigateToPage(pageGuid)) return false;

    auto activePage = GetActivePage();
    if (!activePage) return false;

    std::shared_ptr<CanvasObject> targetObj = nullptr;
    for (const auto& obj : activePage->objects) {
        if (obj && obj->uid == objectUid) {
            targetObj = obj;
            break;
        }
    }

    if (!targetObj) {
        LOG_WARN(DocumentSession, "NavigateToObject: Object UID " + std::to_string(objectUid) +
                 " not found on page " + pageGuid);
        return false;
    }

    const auto& bounds = targetObj->bounds;
    double cx = (bounds.minX + bounds.maxX) * 0.5;
    double cy = (bounds.minY + bounds.maxY) * 0.5;

    double scale = pixelsPerMm * zoom;
    if (scale <= 1e-4) scale = 1.0;

    double panX = (screenWidthPx * 0.5 / scale) - cx;
    double panY = (screenHeightPx * 0.5 / scale) - cy;

    SavePageViewport(pageGuid, panX, panY, zoom);

    if (selectObject) {
        DeselectAll();
        targetObj->isSelected = 1;
        NotifyPageModified(activePage);
    }

    LOG_INFO(DocumentSession, "NavigateToObject: Centered on object UID " + std::to_string(objectUid) +
             " at world (" + std::to_string(cx) + ", " + std::to_string(cy) + ") mm");
    return true;
}

/**
 * @brief Parses a dynamic deep-link URI into a structured CanvasDeepLink target.
 *
 * Supported URI Formats:
 * 1. folionote://page/<guid>
 * 2. folionote://page/<guid>?x=120.5&y=450.2&zoom=1.5
 * 3. folionote://page/<guid>#obj=<uid> or folionote://page/<guid>?obj=<uid>
 * 4. folionote://page/<guid>?pos=120.5,450.2,1.5
 * 5. <guid> (direct GUID string)
 *
 * @param uri Raw URI string.
 * @return std::optional<CanvasDeepLink> containing parsed target fields if valid.
 */
std::optional<DocumentSession::CanvasDeepLink> DocumentSession::ParseDeepLinkUri(const std::string& uri) {
    if (uri.empty()) return std::nullopt;

    CanvasDeepLink link;
    std::string clean = uri;

    // Strip scheme if present
    const std::string scheme = "folionote://";
    if (clean.rfind(scheme, 0) == 0) {
        clean = clean.substr(scheme.length());
    }
    const std::string pagePrefix = "page/";
    if (clean.rfind(pagePrefix, 0) == 0) {
        clean = clean.substr(pagePrefix.length());
    }

    // Split query and fragment
    std::string queryStr;
    std::string fragmentStr;

    size_t hashPos = clean.find('#');
    if (hashPos != std::string::npos) {
        fragmentStr = clean.substr(hashPos + 1);
        clean = clean.substr(0, hashPos);
    }
    size_t qPos = clean.find('?');
    if (qPos != std::string::npos) {
        queryStr = clean.substr(qPos + 1);
        clean = clean.substr(0, qPos);
    }

    link.pageGuid = clean;
    if (link.pageGuid.empty()) return std::nullopt;

    // Parse query params (e.g. x=10&y=20&zoom=1.5 or obj=42)
    auto parseParams = [&](const std::string& s) {
        if (s.empty()) return;
        size_t start = 0;
        while (start < s.length()) {
            size_t end = s.find('&', start);
            if (end == std::string::npos) end = s.length();
            std::string pair = s.substr(start, end - start);
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                std::string key = pair.substr(0, eq);
                std::string val = pair.substr(eq + 1);
                try {
                    if (key == "x") {
                        link.worldXMm = std::stod(val);
                        link.hasTargetCoords = true;
                    } else if (key == "y") {
                        link.worldYMm = std::stod(val);
                        link.hasTargetCoords = true;
                    } else if (key == "zoom") {
                        link.zoom = std::stod(val);
                    } else if (key == "obj" || key == "object") {
                        link.objectUid = static_cast<uint32_t>(std::stoul(val));
                        link.hasTargetObject = true;
                    } else if (key == "pos") {
                        // Comma separated x,y or x,y,zoom
                        size_t c1 = val.find(',');
                        if (c1 != std::string::npos) {
                            link.worldXMm = std::stod(val.substr(0, c1));
                            size_t c2 = val.find(',', c1 + 1);
                            if (c2 != std::string::npos) {
                                link.worldYMm = std::stod(val.substr(c1 + 1, c2 - c1 - 1));
                                link.zoom = std::stod(val.substr(c2 + 1));
                            } else {
                                link.worldYMm = std::stod(val.substr(c1 + 1));
                            }
                            link.hasTargetCoords = true;
                        }
                    }
                } catch (...) {
                    // Ignore malformed numeric values
                }
            } else if (pair.rfind("obj_", 0) == 0) {
                // Format: #obj_123
                try {
                    link.objectUid = static_cast<uint32_t>(std::stoul(pair.substr(4)));
                    link.hasTargetObject = true;
                } catch (...) {}
            }
            start = end + 1;
        }
    };

    parseParams(queryStr);
    parseParams(fragmentStr);

    return link;
}

/**
 * @brief Navigates to a parsed dynamic canvas deep link.
 */
bool DocumentSession::NavigateToDeepLink(const CanvasDeepLink& link,
                                        double screenWidthPx, double screenHeightPx,
                                        double pixelsPerMm, bool selectObject) {
    if (link.pageGuid.empty()) return false;

    if (link.hasTargetObject && link.objectUid != 0) {
        return NavigateToObject(link.pageGuid, link.objectUid,
                                screenWidthPx, screenHeightPx,
                                pixelsPerMm, selectObject, link.zoom);
    }

    if (link.hasTargetCoords) {
        return NavigateToCanvasLocation(link.pageGuid, link.worldXMm, link.worldYMm,
                                        screenWidthPx, screenHeightPx,
                                        pixelsPerMm, link.zoom);
    }

    return NavigateToPage(link.pageGuid);
}

/**
 * @brief Parses and navigates directly to any canvas deep-link URI.
 */
bool DocumentSession::NavigateToUri(const std::string& uri,
                                    double screenWidthPx, double screenHeightPx,
                                    double pixelsPerMm, bool selectObject) {
    auto parsed = ParseDeepLinkUri(uri);
    if (!parsed) return false;
    return NavigateToDeepLink(*parsed, screenWidthPx, screenHeightPx, pixelsPerMm, selectObject);
}
