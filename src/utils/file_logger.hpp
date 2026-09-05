#pragma once
#include <fstream>
#include <string>
#include <mutex>
#include <filesystem>
#include <vector>
// ANDROID: SDL.h is needed for SDL_GetPrefPath() which gives us a writable
// directory path. Android's filesystem root "/" is read-only — we cannot
// create "logs/" relative to it like we do on desktop.
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

/*
@class FileLogger
@brief A thread-safe file logger that writes log messages to a file and keeps a memory cache.
@note This class is a singleton and should not be instantiated directly.
*/
class FileLogger {
private:
    std::ofstream logFile;
    std::mutex fileMutex;
    bool isInitialized = false;
    
    // In-memory history for live debugging in the UI overlay
    std::vector<FolioLogEntry> history;
    const size_t maxHistorySize = 300;

    // runs printout once programs starts, and starts logging
    FileLogger() {
#if defined(__ANDROID__)
        // ANDROID: The working directory on Android is the filesystem root "/" which
        // is read-only. Creating "logs" relative to it throws a filesystem_error and
        // crashes the app. SDL_GetPrefPath() gives a writable private app directory:
        //   e.g. /data/user/0/org.libsdl.app/files/
        // This directory is created by the OS automatically and is app-sandboxed.
        const char* prefPath = SDL_GetPrefPath("UniversalFramework", "FolioNote");
        std::string logDir = prefPath ? std::string(prefPath) + "logs" : "";
        std::string logPath = prefPath ? std::string(prefPath) + "logs/folionote_session.log" : "";
        if (!logDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(logDir, ec); // non-throwing
        }
        if (!logPath.empty()) {
            logFile.open(logPath, std::ios::out | std::ios::trunc);
        }
#else
        // DESKTOP: Use non-throwing error_code overloads so that permission errors
        // (e.g. read-only mount) gracefully disable logging instead of crashing.
        std::error_code ec;
        if (!std::filesystem::exists("logs", ec) && !ec) {
            std::filesystem::create_directory("logs", ec);
        }
        logFile.open("logs/folionote_session.log", std::ios::out | std::ios::trunc);
#endif
        if (logFile.is_open()) {
            isInitialized = true;
            logFile << "=================================================\n";
            logFile << "           FolioNote Session Started             \n";
            logFile << "=================================================\n";
            logFile.flush();
        }
    }

    // gets called automatically when the program ends, and prints final messages
    ~FileLogger() {
        std::lock_guard<std::mutex> lock(fileMutex);
        if (isInitialized && logFile.is_open()) {
            logFile << "=================================================\n";
            logFile << "        FolioNote Clean Engine Shutdown          \n";
            logFile << "=================================================\n";
            logFile.close();
        }
    }

public:
    // this allows us to access the file logger from other classes
    static FileLogger& Instance() {
        static FileLogger s_fileLogger;
        return s_fileLogger;
    }

    /**
     * @brief Writes a structured log entry to the file, and caches it in memory.
     */
    void WriteLog(const FolioLogEntry& entry) {
        if (!isInitialized) return;
        std::lock_guard<std::mutex> lock(fileMutex);

        // 1. Write to the physical file on disk
        logFile << "[" << entry.timestamp << "] [" << entry.level << "] [" << entry.source << "] " << entry.message << "\n";
        logFile.flush(); // Instant flush for crash debugging

        // 2. Cache in memory for the Debug Overlay
        history.push_back(entry);
        if (history.size() > maxHistorySize) {
            history.erase(history.begin());
        }
    }

    /**
     * @brief Returns a copy of the in-memory log history for the UI overlay.
     */
    std::vector<FolioLogEntry> GetHistory() {
        std::lock_guard<std::mutex> lock(fileMutex);
        return history;
    }
};

}