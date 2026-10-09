#pragma once

/**
 * =========================================================================================
 * @file io/file_logger.hpp
 * @brief Thread-safe file logger and in-memory ring cache for FolioNote
 * =========================================================================================
 */

#include <fstream>
#include <string>
#include <mutex>
#include <filesystem>
#include <vector>
#include <deque>
#include <sstream>
#include <chrono>
#include <iomanip>

#include "io/file_manager.hpp"

#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#endif

namespace Folio {

/**
 * @brief Represents a single structured log entry in the system.
 */
struct FolioLogEntry {
    std::string timestamp; // "HH:MM:SS.mmm"
    std::string level;     // "INFO", "WARN", "ERROR"
    std::string source;    // "AssetStream", "CanvasEngine", "RTree", etc.
    std::string message;   // Raw log text
};

/**
 * @class FileLogger
 * @brief Thread-safe file logger maintaining an active session file and a ring cache.
 */
class FileLogger {
private:
    std::ofstream logFile;
    std::mutex fileMutex;
    bool isInitialized = false;

    // Fixed-capacity ring cache for live UI debug overlays (O(1) pop_front)
    std::deque<FolioLogEntry> history;
    const size_t maxHistorySize = 300;

    // Default constructor does NOT perform filesystem I/O.
    // This allows safe logging during translation-unit static initialization
    // before SDL, platform directories, or Android sandboxes are initialized.
    FileLogger() = default;

    ~FileLogger() {
        std::lock_guard<std::mutex> lock(fileMutex);
        if (isInitialized && logFile.is_open()) {
            logFile << "=================================================\n";
            logFile << "        FolioNote Clean Engine Shutdown          \n";
            logFile << "=================================================\n";
            logFile.flush();
            logFile.close();
        }
    }

    // Internal helper: builds export string assuming fileMutex is already locked
    std::string ExportLogsToStringUnlocked() const {
        std::ostringstream ss;
        ss << "=================================================\n";
        ss << "        FolioNote Diagnostic Log Export          \n";
        ss << "        Total Cached Records: " << history.size() << "\n";
        ss << "=================================================\n";
        for (const auto& entry : history) {
            ss << "[" << entry.timestamp << "] [" << entry.level << "] [" << entry.source << "] " << entry.message << "\n";
        }
        return ss.str();
    }

public:
    static FileLogger& Instance() {
        static FileLogger s_fileLogger;
        return s_fileLogger;
    }

    /**
     * @brief Initializes the physical session log file on disk.
     * Safe to invoke after the application document/logs directory has been resolved.
     * Drains any prior log entries cached in memory to the physical file.
     */
    void Initialize(const std::string& customLogsDir = "") {
        std::lock_guard<std::mutex> lock(fileMutex);
        if (isInitialized && logFile.is_open()) return;

        std::string logsDir = customLogsDir.empty() ? FileManager::GetLogsDirectory() : customLogsDir;
        if (logsDir.empty()) return;

        if (!FileManager::CreateDirectories(logsDir)) {
            return;
        }

        std::string logFilePath = FileManager::JoinPath(logsDir, "folionote_session.log");
        logFile.open(std::filesystem::path(logFilePath), std::ios::out | std::ios::trunc);
        if (logFile.is_open()) {
            isInitialized = true;
            logFile << "=================================================\n";
            logFile << "           FolioNote Session Started             \n";
            logFile << "=================================================\n";

            // Drain pre-boot log records recorded in memory before file initialization
            for (const auto& entry : history) {
                logFile << "[" << entry.timestamp << "] [" << entry.level << "] [" << entry.source << "] " << entry.message << "\n";
            }
            logFile.flush();
        }
    }

    /**
     * @brief Writes a structured log entry to the in-memory cache and disk file if initialized.
     * Flushes immediately only on ERROR to preserve 60/120 FPS frame timing.
     */
    void WriteLog(const FolioLogEntry& entry) {
        std::lock_guard<std::mutex> lock(fileMutex);

        // 1. Always maintain bounded in-memory ring buffer
        history.push_back(entry);
        if (history.size() > maxHistorySize) {
            history.pop_front();
        }

        // 2. Stream formatted message to physical log if file handle is active
        if (isInitialized && logFile.is_open()) {
            logFile << "[" << entry.timestamp << "] [" << entry.level << "] [" << entry.source << "] " << entry.message << "\n";
            if (entry.level == "ERROR") {
                logFile.flush();
            }
        }
    }

    /**
     * @brief Returns a copy of the cached in-memory history for UI overlays.
     */
    std::vector<FolioLogEntry> GetHistory() {
        std::lock_guard<std::mutex> lock(fileMutex);
        return std::vector<FolioLogEntry>(history.begin(), history.end());
    }

    /**
     * @brief Clears the UI history cache.
     */
    void ClearHistory() {
        std::lock_guard<std::mutex> lock(fileMutex);
        history.clear();
    }

    /**
     * @brief Exports the in-memory log history into a single formatted string.
     */
    std::string ExportLogsToString() {
        std::lock_guard<std::mutex> lock(fileMutex);
        return ExportLogsToStringUnlocked();
    }

    /**
     * @brief Exports the log history to a designated or timestamped disk file.
     * @param customPath Target file path, or empty string to use default logs folder.
     * @param outPathUsed Absolute or relative path to the generated export.
     * @return true on success, false on failure.
     */
    bool ExportLogsToFile(const std::string& customPath, std::string& outPathUsed) {
        std::string exportText;
        {
            std::lock_guard<std::mutex> lock(fileMutex);
            exportText = ExportLogsToStringUnlocked();
        }

        std::string targetPath = customPath;
        if (targetPath.empty()) {
            auto now = std::chrono::system_clock::now();
            auto timer = std::chrono::system_clock::to_time_t(now);
            std::tm bt{};
#if defined(_WIN32)
            localtime_s(&bt, &timer);
#else
            localtime_r(&timer, &bt);
#endif
            std::ostringstream nameStream;
            nameStream << "folionote_export_" 
                       << std::put_time(&bt, "%Y%m%d_%H%M%S") 
                       << ".log";
            targetPath = FileManager::JoinPath(FileManager::GetLogsDirectory(), nameStream.str());
        }

        std::string parentDir = FileManager::GetParentPath(targetPath);
        if (!parentDir.empty()) {
            FileManager::CreateDirectories(parentDir);
        }

        bool ok = FileManager::WriteTextAtomic(targetPath, exportText);
        if (ok) {
            outPathUsed = targetPath;
        }
        return ok;
    }
};

} // namespace Folio