#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace Folio {

struct CanvasUsageData {
    double activeDrawingTimeSec = 0.0;
    double canvasSessionTimeSec = 0.0;
    uint64_t strokesCount = 0;
    double inkingDistanceMm = 0.0;
    uint64_t eraserActionsCount = 0;
    uint64_t objectsCreatedCount = 0;
    uint64_t panGesturesCount = 0;
    uint64_t zoomGesturesCount = 0;
};

struct UIUsageData {
    double totalAppTimeSec = 0.0;
    double timeInHubSec = 0.0;
    double timeInSettingsSec = 0.0;
    uint64_t pageSwitchesCount = 0;
    uint64_t sectionSwitchesCount = 0;
    uint64_t notebookSwitchesCount = 0;
    uint64_t dialogsOpenedCount = 0;
    uint64_t totalClicksCount = 0;
    uint64_t sidebarInteractionsCount = 0;
};

class UsageTracker {
public:
    static UsageTracker& Instance() {
        static UsageTracker instance;
        return instance;
    }

    bool isTrackingEnabled = true;

    // --- Ingestion API ---
    void RecordDrawingTime(double dt) {
        if (!isTrackingEnabled || dt <= 0.0 || dt > 1.0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.activeDrawingTimeSec += dt;
    }

    void RecordCanvasTime(double dt) {
        if (!isTrackingEnabled || dt <= 0.0 || dt > 1.0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.canvasSessionTimeSec += dt;
        uiData_.totalAppTimeSec += dt;
    }

    void RecordHubTime(double dt) {
        if (!isTrackingEnabled || dt <= 0.0 || dt > 1.0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.timeInHubSec += dt;
        uiData_.totalAppTimeSec += dt;
    }

    void RecordSettingsTime(double dt) {
        if (!isTrackingEnabled || dt <= 0.0 || dt > 1.0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.timeInSettingsSec += dt;
    }

    void RecordStrokeCommitted() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.strokesCount++;
    }

    void RecordInkingDistance(double distMm) {
        if (!isTrackingEnabled || distMm <= 0.0 || std::isnan(distMm) || distMm > 10000.0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.inkingDistanceMm += distMm;
    }

    void RecordEraserAction() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.eraserActionsCount++;
    }

    void RecordObjectCreated() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.objectsCreatedCount++;
    }

    void RecordPanGesture() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.panGesturesCount++;
    }

    void RecordZoomGesture() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_.zoomGesturesCount++;
    }

    void RecordPageSwitch() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.pageSwitchesCount++;
    }

    void RecordSectionSwitch() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.sectionSwitchesCount++;
    }

    void RecordNotebookSwitch() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.notebookSwitchesCount++;
    }

    void RecordDialogOpened() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.dialogsOpenedCount++;
    }

    void RecordClick() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.totalClicksCount++;
    }

    void RecordSidebarInteraction() {
        if (!isTrackingEnabled) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uiData_.sidebarInteractionsCount++;
    }

    // --- Query API ---
    CanvasUsageData GetCanvasData() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return canvasData_;
    }

    UIUsageData GetUIData() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return uiData_;
    }

    void ResetStats() {
        std::lock_guard<std::mutex> lock(mutex_);
        canvasData_ = CanvasUsageData{};
        uiData_ = UIUsageData{};
        SaveToJsonLocked("config/usage_stats.json");
    }

    // --- Formatting Helpers ---
    static std::string FormatDuration(double sec) {
        if (sec < 0.0) sec = 0.0;
        int totalSec = static_cast<int>(sec);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int s = totalSec % 60;
        std::ostringstream oss;
        if (hours > 0) {
            oss << hours << "h " << mins << "m " << s << "s";
        } else if (mins > 0) {
            oss << mins << "m " << s << "s";
        } else {
            oss << s << "s";
        }
        return oss.str();
    }

    static std::string FormatDistance(double distMm) {
        if (distMm < 0.0) distMm = 0.0;
        double meters = distMm / 1000.0;
        std::ostringstream oss;
        if (meters >= 1000.0) {
            oss << std::fixed << std::setprecision(2) << (meters / 1000.0) << " km";
        } else if (meters >= 1.0) {
            oss << std::fixed << std::setprecision(2) << meters << " m";
        } else {
            oss << std::fixed << std::setprecision(1) << (distMm / 10.0) << " cm";
        }
        return oss.str();
    }

    double GetStrokesPerMinute() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (canvasData_.activeDrawingTimeSec < 1.0) return 0.0;
        return (static_cast<double>(canvasData_.strokesCount) / (canvasData_.activeDrawingTimeSec / 60.0));
    }

    // --- Persistence ---
    bool SaveToJson(const std::string& filepath = "config/usage_stats.json") {
        std::lock_guard<std::mutex> lock(mutex_);
        return SaveToJsonLocked(filepath);
    }

    bool LoadFromJson(const std::string& filepath = "config/usage_stats.json") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec) || ec) return false;
        std::ifstream in(filepath);
        if (!in.is_open()) return false;

        std::string line;
        auto parseDoubleVal = [](const std::string& l) -> double {
            size_t colon = l.find(':');
            if (colon == std::string::npos) return 0.0;
            try { return std::stod(l.substr(colon + 1)); } catch (...) { return 0.0; }
        };
        auto parseUint64Val = [](const std::string& l) -> uint64_t {
            size_t colon = l.find(':');
            if (colon == std::string::npos) return 0;
            try { return std::stoull(l.substr(colon + 1)); } catch (...) { return 0; }
        };
        auto parseBoolVal = [](const std::string& l) -> bool {
            return (l.find("true") != std::string::npos || l.find("1") != std::string::npos);
        };

        while (std::getline(in, line)) {
            if (line.find("\"isTrackingEnabled\"") != std::string::npos) isTrackingEnabled = parseBoolVal(line);
            else if (line.find("\"activeDrawingTimeSec\"") != std::string::npos) canvasData_.activeDrawingTimeSec = parseDoubleVal(line);
            else if (line.find("\"canvasSessionTimeSec\"") != std::string::npos) canvasData_.canvasSessionTimeSec = parseDoubleVal(line);
            else if (line.find("\"strokesCount\"") != std::string::npos) canvasData_.strokesCount = parseUint64Val(line);
            else if (line.find("\"inkingDistanceMm\"") != std::string::npos) canvasData_.inkingDistanceMm = parseDoubleVal(line);
            else if (line.find("\"eraserActionsCount\"") != std::string::npos) canvasData_.eraserActionsCount = parseUint64Val(line);
            else if (line.find("\"objectsCreatedCount\"") != std::string::npos) canvasData_.objectsCreatedCount = parseUint64Val(line);
            else if (line.find("\"panGesturesCount\"") != std::string::npos) canvasData_.panGesturesCount = parseUint64Val(line);
            else if (line.find("\"zoomGesturesCount\"") != std::string::npos) canvasData_.zoomGesturesCount = parseUint64Val(line);
            else if (line.find("\"totalAppTimeSec\"") != std::string::npos) uiData_.totalAppTimeSec = parseDoubleVal(line);
            else if (line.find("\"timeInHubSec\"") != std::string::npos) uiData_.timeInHubSec = parseDoubleVal(line);
            else if (line.find("\"timeInSettingsSec\"") != std::string::npos) uiData_.timeInSettingsSec = parseDoubleVal(line);
            else if (line.find("\"pageSwitchesCount\"") != std::string::npos) uiData_.pageSwitchesCount = parseUint64Val(line);
            else if (line.find("\"sectionSwitchesCount\"") != std::string::npos) uiData_.sectionSwitchesCount = parseUint64Val(line);
            else if (line.find("\"notebookSwitchesCount\"") != std::string::npos) uiData_.notebookSwitchesCount = parseUint64Val(line);
            else if (line.find("\"dialogsOpenedCount\"") != std::string::npos) uiData_.dialogsOpenedCount = parseUint64Val(line);
            else if (line.find("\"totalClicksCount\"") != std::string::npos) uiData_.totalClicksCount = parseUint64Val(line);
            else if (line.find("\"sidebarInteractionsCount\"") != std::string::npos) uiData_.sidebarInteractionsCount = parseUint64Val(line);
        }
        return true;
    }

    std::string ExportSummaryToString() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "========================================================\n";
        oss << "               FOLIONOTE USAGE REPORT                   \n";
        oss << "========================================================\n\n";

        oss << "--- CANVAS USAGE ---\n";
        oss << "Active Inking Time    : " << FormatDuration(canvasData_.activeDrawingTimeSec) << "\n";
        oss << "Canvas View Time      : " << FormatDuration(canvasData_.canvasSessionTimeSec) << "\n";
        oss << "Total Strokes Drawn   : " << canvasData_.strokesCount << "\n";
        oss << "Total Distance Inked  : " << FormatDistance(canvasData_.inkingDistanceMm) << "\n";
        double spm = (canvasData_.activeDrawingTimeSec >= 1.0) ? 
                     (static_cast<double>(canvasData_.strokesCount) / (canvasData_.activeDrawingTimeSec / 60.0)) : 0.0;
        oss << "Inking Cadence        : " << std::fixed << std::setprecision(1) << spm << " strokes/min\n";
        oss << "Objects Created       : " << canvasData_.objectsCreatedCount << "\n";
        oss << "Eraser Operations     : " << canvasData_.eraserActionsCount << "\n";
        oss << "Pan Gestures          : " << canvasData_.panGesturesCount << "\n";
        oss << "Zoom Gestures         : " << canvasData_.zoomGesturesCount << "\n\n";

        oss << "--- USER INTERFACE & NAVIGATION ---\n";
        oss << "Total App Session Time: " << FormatDuration(uiData_.totalAppTimeSec) << "\n";
        oss << "Time in Notebook Hub  : " << FormatDuration(uiData_.timeInHubSec) << "\n";
        oss << "Time in Settings      : " << FormatDuration(uiData_.timeInSettingsSec) << "\n";
        oss << "Page Switches         : " << uiData_.pageSwitchesCount << "\n";
        oss << "Section Switches      : " << uiData_.sectionSwitchesCount << "\n";
        oss << "Notebook Switches     : " << uiData_.notebookSwitchesCount << "\n";
        oss << "Modal Dialogs Opened  : " << uiData_.dialogsOpenedCount << "\n";
        oss << "Sidebar Interactions  : " << uiData_.sidebarInteractionsCount << "\n";
        oss << "Total UI Clicks       : " << uiData_.totalClicksCount << "\n";
        oss << "========================================================\n";
        return oss.str();
    }

    bool ExportSummaryToFile(const std::string& customPath, std::string& outPathUsed) {
        std::string exportDir = "exports";
        std::error_code ec;
        if (!std::filesystem::exists(exportDir, ec)) {
            std::filesystem::create_directories(exportDir, ec);
        }

        std::string path = customPath;
        if (path.empty()) {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tmBuffer;
#if defined(_WIN32)
            localtime_s(&tmBuffer, &in_time_t);
#else
            localtime_r(&in_time_t, &tmBuffer);
#endif
            std::ostringstream ss;
            ss << exportDir << "/usage_report_" 
               << std::put_time(&tmBuffer, "%Y%m%d_%H%M%S") << ".json";
            path = ss.str();
        }

        std::ofstream out(path);
        if (!out.is_open()) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        out << "{\n";
        out << "  \"isTrackingEnabled\": " << (isTrackingEnabled ? "true" : "false") << ",\n";
        out << "  \"canvasUsage\": {\n";
        out << "    \"activeDrawingTimeSec\": " << canvasData_.activeDrawingTimeSec << ",\n";
        out << "    \"canvasSessionTimeSec\": " << canvasData_.canvasSessionTimeSec << ",\n";
        out << "    \"strokesCount\": " << canvasData_.strokesCount << ",\n";
        out << "    \"inkingDistanceMm\": " << canvasData_.inkingDistanceMm << ",\n";
        out << "    \"eraserActionsCount\": " << canvasData_.eraserActionsCount << ",\n";
        out << "    \"objectsCreatedCount\": " << canvasData_.objectsCreatedCount << ",\n";
        out << "    \"panGesturesCount\": " << canvasData_.panGesturesCount << ",\n";
        out << "    \"zoomGesturesCount\": " << canvasData_.zoomGesturesCount << "\n";
        out << "  },\n";
        out << "  \"uiUsage\": {\n";
        out << "    \"totalAppTimeSec\": " << uiData_.totalAppTimeSec << ",\n";
        out << "    \"timeInHubSec\": " << uiData_.timeInHubSec << ",\n";
        out << "    \"timeInSettingsSec\": " << uiData_.timeInSettingsSec << ",\n";
        out << "    \"pageSwitchesCount\": " << uiData_.pageSwitchesCount << ",\n";
        out << "    \"sectionSwitchesCount\": " << uiData_.sectionSwitchesCount << ",\n";
        out << "    \"notebookSwitchesCount\": " << uiData_.notebookSwitchesCount << ",\n";
        out << "    \"dialogsOpenedCount\": " << uiData_.dialogsOpenedCount << ",\n";
        out << "    \"totalClicksCount\": " << uiData_.totalClicksCount << ",\n";
        out << "    \"sidebarInteractionsCount\": " << uiData_.sidebarInteractionsCount << "\n";
        out << "  }\n";
        out << "}\n";

        outPathUsed = path;
        return true;
    }

private:
    UsageTracker() = default;
    ~UsageTracker() = default;
    UsageTracker(const UsageTracker&) = delete;
    UsageTracker& operator=(const UsageTracker&) = delete;

    bool SaveToJsonLocked(const std::string& filepath) {
        std::filesystem::path dir = std::filesystem::path(filepath).parent_path();
        std::error_code ec;
        if (!dir.empty() && !std::filesystem::exists(dir, ec)) {
            std::filesystem::create_directories(dir, ec);
        }

        std::ofstream out(filepath);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"isTrackingEnabled\": " << (isTrackingEnabled ? "true" : "false") << ",\n";
        out << "  \"activeDrawingTimeSec\": " << canvasData_.activeDrawingTimeSec << ",\n";
        out << "  \"canvasSessionTimeSec\": " << canvasData_.canvasSessionTimeSec << ",\n";
        out << "  \"strokesCount\": " << canvasData_.strokesCount << ",\n";
        out << "  \"inkingDistanceMm\": " << canvasData_.inkingDistanceMm << ",\n";
        out << "  \"eraserActionsCount\": " << canvasData_.eraserActionsCount << ",\n";
        out << "  \"objectsCreatedCount\": " << canvasData_.objectsCreatedCount << ",\n";
        out << "  \"panGesturesCount\": " << canvasData_.panGesturesCount << ",\n";
        out << "  \"zoomGesturesCount\": " << canvasData_.zoomGesturesCount << ",\n";
        out << "  \"totalAppTimeSec\": " << uiData_.totalAppTimeSec << ",\n";
        out << "  \"timeInHubSec\": " << uiData_.timeInHubSec << ",\n";
        out << "  \"timeInSettingsSec\": " << uiData_.timeInSettingsSec << ",\n";
        out << "  \"pageSwitchesCount\": " << uiData_.pageSwitchesCount << ",\n";
        out << "  \"sectionSwitchesCount\": " << uiData_.sectionSwitchesCount << ",\n";
        out << "  \"notebookSwitchesCount\": " << uiData_.notebookSwitchesCount << ",\n";
        out << "  \"dialogsOpenedCount\": " << uiData_.dialogsOpenedCount << ",\n";
        out << "  \"totalClicksCount\": " << uiData_.totalClicksCount << ",\n";
        out << "  \"sidebarInteractionsCount\": " << uiData_.sidebarInteractionsCount << "\n";
        out << "}\n";
        return true;
    }

    mutable std::mutex mutex_;
    CanvasUsageData canvasData_;
    UIUsageData uiData_;
};

} // namespace Folio
