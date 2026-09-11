#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <iostream>
#include "imgui.h"
#include "input/pen_palette.hpp"
#include "utils/file_loader.hpp"

#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#endif

struct RibbonSectionSetting {
    std::string id;
    std::string title;
    bool isVisible = true;
    std::vector<std::string> buttons;
};

struct PenPreset {
    std::string id;
    std::string name;
    PenType type = PenType::Pen;
    ImVec4 color = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    float thicknessMm = 0.5f;
    float opacity = 1.0f;
    bool isCustomPreset = false;
    StrokePattern strokePattern = StrokePattern::Solid;
};

using PenPresetSetting = PenPreset;

class SettingsManager {
public:
    static SettingsManager& Instance() {
        static SettingsManager instance;
        return instance;
    }

    // Inking and Pen Tool State
    std::string activePresetId = "pen_black";
    bool drawWithTouch = false;
    bool rulerEnabled = false;
    bool autoShapesEnabled = false;
    bool isStrokeEraser = true;
    float eraserSizeMm = 6.0f;
    std::vector<PenPreset> inkingPresets;

    // ==========================================================================
    // DEFAULT DEVICE TOOL SETTINGS  (placeholder — UI not yet wired)
    // ==========================================================================
    // These store the user-configured default tool for each input device.
    // They are persisted to disk but the settings UI page that exposes them
    // has not been built yet — they will be populated by a future settings panel.
    //
    // At startup, app.hpp reads these and applies them to the InputStateMachine
    // via SetToolForDevice() so the device defaults are restored across sessions.
    //
    // Valid string values: "Inking", "Eraser", "Selecting", "Panning", "Idle"
    // ==========================================================================
    std::string defaultStylusTool = "Inking";    // Stylus default: pen inking mode
    std::string defaultTouchTool  = "Panning";   // Touch default: navigation/pan mode
    std::string defaultMouseTool  = "Selecting"; // Mouse default: box selection / object hit-test

    // Ribbon Layout State
    std::string ribbonDisplayMode = "FullRibbon";
    std::string ribbonActiveTab = "Draw";
    std::vector<RibbonSectionSetting> ribbonSections;

    // Appearance State
    bool isDarkMode = true;
    bool isCanvasInverted = false;

    // PDF & Virtual Printer Ingestion State
    std::string pdfSpoolFolderPath = "";
    bool autoIngestPrintedPdfs = true;
    int defaultPdfImportMode = 0; // 0 = LocalCopy, 1 = ExternalLink

    // Has settings been loaded from disk
    bool isLoaded = false;

    // -------------------------------------------------------------------------
    // String <-> Enum Converters
    // -------------------------------------------------------------------------
    static std::string PenTypeToString(PenType type) {
        switch (type) {
            case PenType::Pen:          return "Pen";
            case PenType::Fountain:     return "Fountain";
            case PenType::Pencil:       return "Pencil";
            case PenType::Brush:        return "Brush";
            case PenType::Highlighter:  return "Highlighter";
            case PenType::LaserPointer: return "LaserPointer";
            default:                    return "Pen";
        }
    }

    static PenType StringToPenType(const std::string& str) {
        if (str == "Fountain")     return PenType::Fountain;
        if (str == "Pencil")       return PenType::Pencil;
        if (str == "Brush")        return PenType::Brush;
        if (str == "Highlighter")  return PenType::Highlighter;
        if (str == "LaserPointer") return PenType::LaserPointer;
        return PenType::Pen;
    }

    static std::string StrokePatternToString(StrokePattern pattern) {
        switch (pattern) {
            case StrokePattern::Solid:          return "Solid";
            case StrokePattern::Dashed:         return "Dashed";
            case StrokePattern::Dotted:         return "Dotted";
            case StrokePattern::TexturedPencil: return "TexturedPencil";
            default:                            return "Solid";
        }
    }

    static StrokePattern StringToStrokePattern(const std::string& str) {
        if (str == "Dashed")         return StrokePattern::Dashed;
        if (str == "Dotted")         return StrokePattern::Dotted;
        if (str == "TexturedPencil") return StrokePattern::TexturedPencil;
        return StrokePattern::Solid;
    }

    // -------------------------------------------------------------------------
    // Default Fallbacks
    // -------------------------------------------------------------------------
    static std::vector<RibbonSectionSetting> GetDefaultRibbonSections() {
        return {
            { "sec_history",   "History",        true, { "Undo", "Redo" } },
            { "sec_selection", "Selection",      true, { "Select", "Lasso" } },
            { "sec_tools",     "Drawing Tools",  true, { "Eraser", "Pens & Nibs", "+ Add" } },
            { "sec_input",     "Input Mode",     true, { "Draw with Touch" } },
            { "sec_stencils",  "Stencils",       true, { "Ruler" } },
            { "sec_edit",      "Edit",           true, { "Insert Space" } },
            { "sec_shapes",    "Shapes",         true, { "Shapes Picker", "Automatic Shapes" } },
            { "sec_math",      "Math",           true, { "Ink to Math" } },
            { "sec_mode",      "Mode",           true, { "Full Page View" } }
        };
    }

    static std::vector<PenPreset> GetDefaultPenPresets() {
        return {
            { "pen_black",     "Black Pen",           PenType::Pen,          ImVec4(0.09f, 0.10f, 0.13f, 1.0f), 0.5f, 1.0f,  false, StrokePattern::Solid },
            { "pen_blue",      "Blue Pen",            PenType::Pen,          ImVec4(0.10f, 0.34f, 0.86f, 1.0f), 0.5f, 1.0f,  false, StrokePattern::Solid },
            { "pen_red",       "Red Pen",             PenType::Pen,          ImVec4(0.86f, 0.15f, 0.15f, 1.0f), 0.5f, 1.0f,  false, StrokePattern::Solid },
            { "pencil_2b",     "2B Pencil",           PenType::Pencil,       ImVec4(0.24f, 0.25f, 0.29f, 1.0f), 0.7f, 0.90f, false, StrokePattern::TexturedPencil },
            { "cal_fountain",  "Fountain Pen",        PenType::Fountain,     ImVec4(0.10f, 0.10f, 0.12f, 1.0f), 1.5f, 1.0f,  false, StrokePattern::Solid },
            { "brush_purple",  "Watercolor Brush",    PenType::Brush,        ImVec4(0.49f, 0.23f, 0.93f, 1.0f), 2.5f, 0.85f, false, StrokePattern::Solid },
            { "high_yellow",   "Yellow Highlighter",  PenType::Highlighter,  ImVec4(1.0f,  0.90f, 0.0f,  1.0f), 4.5f, 0.55f, false, StrokePattern::Solid },
            { "high_green",    "Mint Highlighter",    PenType::Highlighter,  ImVec4(0.06f, 0.73f, 0.51f, 1.0f), 4.5f, 0.55f, false, StrokePattern::Solid },
            { "laser_pointer", "Laser Pointer",       PenType::LaserPointer, ImVec4(1.0f,  0.15f, 0.25f, 1.0f), 3.0f, 0.95f, false, StrokePattern::Solid }
        };
    }

    bool IsSectionVisible(const std::string& id, bool defaultVal = true) const {
        for (const auto& sec : ribbonSections) {
            if (sec.id == id) return sec.isVisible;
        }
        return defaultVal;
    }

    const RibbonSectionSetting* FindSection(const std::string& id) const {
        for (const auto& sec : ribbonSections) {
            if (sec.id == id) return &sec;
        }
        return nullptr;
    }

    std::string GetConfigPath(const std::string& customPath = "") const {
        if (!customPath.empty()) return customPath;

#if defined(__ANDROID__)
        const char* prefPath = SDL_GetPrefPath("UniversalFramework", "FolioNote");
        if (prefPath) {
            return std::string(prefPath) + "config/settings.json";
        }
#endif
        return "config/settings.json";
    }

    // -------------------------------------------------------------------------
    // Persistence: Load & Save
    // -------------------------------------------------------------------------
    bool Load(const std::string& customPath = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string filepath = GetConfigPath(customPath);

        if (!FileLoader::Exists(filepath)) {
            if (ribbonSections.empty()) {
                ribbonSections = GetDefaultRibbonSections();
            }
            if (inkingPresets.empty()) {
                inkingPresets = GetDefaultPenPresets();
            }
            return false;
        }

        std::string jsonContent;
        if (!FileLoader::ReadToString(filepath, jsonContent)) {
            return false;
        }

        try {
            nlohmann::json j = nlohmann::json::parse(jsonContent);

            // 1. Inking Settings
            if (j.contains("inking") && j["inking"].is_object()) {
                const auto& jInking = j["inking"];
                if (jInking.contains("activePresetId")) activePresetId = jInking["activePresetId"].get<std::string>();
                if (jInking.contains("drawWithTouch")) drawWithTouch = jInking["drawWithTouch"].get<bool>();
                if (jInking.contains("rulerEnabled")) rulerEnabled = jInking["rulerEnabled"].get<bool>();
                if (jInking.contains("autoShapesEnabled")) autoShapesEnabled = jInking["autoShapesEnabled"].get<bool>();
                if (jInking.contains("isStrokeEraser")) isStrokeEraser = jInking["isStrokeEraser"].get<bool>();
                if (jInking.contains("eraserSizeMm")) eraserSizeMm = jInking["eraserSizeMm"].get<float>();
                // Device default tools — placeholder, settings UI not yet built
                if (jInking.contains("defaultStylusTool")) defaultStylusTool = jInking["defaultStylusTool"].get<std::string>();
                if (jInking.contains("defaultTouchTool"))  defaultTouchTool  = jInking["defaultTouchTool"].get<std::string>();
                if (jInking.contains("defaultMouseTool")) {
                    defaultMouseTool  = jInking["defaultMouseTool"].get<std::string>();
                    if (defaultMouseTool == "Idle") defaultMouseTool = "Selecting";
                }

                if (jInking.contains("presets") && jInking["presets"].is_array()) {
                    inkingPresets.clear();
                    for (const auto& jp : jInking["presets"]) {
                        PenPreset p;
                        if (jp.contains("id")) p.id = jp["id"].get<std::string>();
                        if (jp.contains("name")) p.name = jp["name"].get<std::string>();
                        if (jp.contains("type")) p.type = StringToPenType(jp["type"].get<std::string>());
                        if (jp.contains("color") && jp["color"].is_array() && jp["color"].size() >= 4) {
                            p.color = ImVec4(
                                jp["color"][0].get<float>(),
                                jp["color"][1].get<float>(),
                                jp["color"][2].get<float>(),
                                jp["color"][3].get<float>()
                            );
                        }
                        if (jp.contains("thicknessMm")) p.thicknessMm = jp["thicknessMm"].get<float>();
                        if (jp.contains("opacity")) p.opacity = jp["opacity"].get<float>();
                        if (jp.contains("isCustomPreset")) p.isCustomPreset = jp["isCustomPreset"].get<bool>();
                        if (jp.contains("strokePattern")) p.strokePattern = StringToStrokePattern(jp["strokePattern"].get<std::string>());
                        inkingPresets.push_back(p);
                    }
                }
            }

            if (inkingPresets.empty()) {
                inkingPresets = GetDefaultPenPresets();
            }

            // 2. Ribbon Settings
            if (j.contains("ribbon") && j["ribbon"].is_object()) {
                const auto& jRibbon = j["ribbon"];
                if (jRibbon.contains("displayMode")) ribbonDisplayMode = jRibbon["displayMode"].get<std::string>();
                if (jRibbon.contains("activeTab")) ribbonActiveTab = jRibbon["activeTab"].get<std::string>();

                if (jRibbon.contains("sections") && jRibbon["sections"].is_array()) {
                    ribbonSections.clear();
                    for (const auto& jSec : jRibbon["sections"]) {
                        RibbonSectionSetting sec;
                        if (jSec.contains("id")) sec.id = jSec["id"].get<std::string>();
                        if (jSec.contains("title")) sec.title = jSec["title"].get<std::string>();
                        if (jSec.contains("isVisible")) sec.isVisible = jSec["isVisible"].get<bool>();
                        if (jSec.contains("buttons") && jSec["buttons"].is_array()) {
                            for (const auto& b : jSec["buttons"]) {
                                sec.buttons.push_back(b.get<std::string>());
                            }
                        }
                        ribbonSections.push_back(sec);
                    }
                }
            }

            if (ribbonSections.empty()) {
                ribbonSections = GetDefaultRibbonSections();
            }

            // 3. Appearance Settings
            if (j.contains("appearance") && j["appearance"].is_object()) {
                const auto& jApp = j["appearance"];
                if (jApp.contains("isDarkMode")) isDarkMode = jApp["isDarkMode"].get<bool>();
                if (jApp.contains("isCanvasInverted")) isCanvasInverted = jApp["isCanvasInverted"].get<bool>();
            }

            // 4. PDF Ingestion Settings
            if (j.contains("pdf") && j["pdf"].is_object()) {
                const auto& jPdf = j["pdf"];
                if (jPdf.contains("spoolFolderPath")) pdfSpoolFolderPath = jPdf["spoolFolderPath"].get<std::string>();
                if (jPdf.contains("autoIngestPrintedPdfs")) autoIngestPrintedPdfs = jPdf["autoIngestPrintedPdfs"].get<bool>();
                if (jPdf.contains("defaultImportMode")) defaultPdfImportMode = jPdf["defaultImportMode"].get<int>();
            }

            isLoaded = true;
            return true;
        } catch (const std::exception& ex) {
            std::cerr << "[SettingsManager] JSON load error: " << ex.what() << "\n";
            return false;
        }
    }

    bool Save(const std::string& customPath = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string filepath = GetConfigPath(customPath);

        std::error_code ec;
        std::filesystem::path dirPath = std::filesystem::path(filepath).parent_path();
        if (!dirPath.empty() && !std::filesystem::exists(dirPath, ec)) {
            std::filesystem::create_directories(dirPath, ec);
        }

        try {
            nlohmann::json j;
            j["version"] = 1;

            // Inking presets
            nlohmann::json jPresets = nlohmann::json::array();
            for (const auto& p : inkingPresets) {
                jPresets.push_back({
                    { "id", p.id },
                    { "name", p.name },
                    { "type", PenTypeToString(p.type) },
                    { "color", { p.color.x, p.color.y, p.color.z, p.color.w } },
                    { "thicknessMm", p.thicknessMm },
                    { "opacity", p.opacity },
                    { "isCustomPreset", p.isCustomPreset },
                    { "strokePattern", StrokePatternToString(p.strokePattern) }
                });
            }

            // Inking section
            j["inking"] = {
                { "activePresetId", activePresetId },
                { "drawWithTouch", drawWithTouch },
                { "rulerEnabled", rulerEnabled },
                { "autoShapesEnabled", autoShapesEnabled },
                { "isStrokeEraser", isStrokeEraser },
                { "eraserSizeMm", eraserSizeMm },
                // Device default tools — placeholder, settings UI not yet built
                { "defaultStylusTool", defaultStylusTool },
                { "defaultTouchTool",  defaultTouchTool  },
                { "defaultMouseTool",  defaultMouseTool  },
                { "presets", jPresets }
            };

            // Ribbon section
            nlohmann::json jSections = nlohmann::json::array();
            for (const auto& sec : ribbonSections) {
                jSections.push_back({
                    { "id", sec.id },
                    { "title", sec.title },
                    { "isVisible", sec.isVisible },
                    { "buttons", sec.buttons }
                });
            }

            j["ribbon"] = {
                { "displayMode", ribbonDisplayMode },
                { "activeTab", ribbonActiveTab },
                { "sections", jSections }
            };

            // Appearance section
            j["appearance"] = {
                { "isDarkMode", isDarkMode },
                { "isCanvasInverted", isCanvasInverted }
            };

            // PDF Ingestion section
            j["pdf"] = {
                { "spoolFolderPath", pdfSpoolFolderPath },
                { "autoIngestPrintedPdfs", autoIngestPrintedPdfs },
                { "defaultImportMode", defaultPdfImportMode }
            };

            return FileLoader::WriteString(filepath, j.dump(2));
        } catch (const std::exception& ex) {
            LOG_ERROR(SettingsManager, std::string("JSON save error: ") + ex.what());
            return false;
        }
    }

private:
    SettingsManager() {
        ribbonSections = GetDefaultRibbonSections();
        inkingPresets = GetDefaultPenPresets();
        Load();
    }
    std::mutex mutex_;
};
