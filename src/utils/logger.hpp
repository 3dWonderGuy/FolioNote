#pragma once
#include <string>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "utils/file_logger.hpp"

namespace Folio {

    // Log levels to filter messages
    enum class LogLevel {
        Info,
        Warn,
        Error
    };

    // Standardized log sources for easy filtering and navigation in the UI
    enum class LogSource {
        General,
        FileLoader,
        FileSaver,
        CanvasEngine,
        RTree,
        DBManager,
        PageRepository,
        BinarySerializer,
        SearchIndex,
        InputManager,
        InputStateMachine,
        AABB,
        CanvasObject,
        Window
    };

    // Helper to convert LogSource enum values into strings
    inline const char* LogSourceToString(LogSource source) {
        switch (source) {
            case LogSource::FileLoader:        return "FileLoader";
            case LogSource::FileSaver:         return "FileSaver";
            case LogSource::CanvasEngine:      return "CanvasEngine";
            case LogSource::RTree:             return "RTree";
            case LogSource::DBManager:         return "DBManager";
            case LogSource::PageRepository:    return "PageRepository";
            case LogSource::BinarySerializer:  return "BinarySerializer";
            case LogSource::SearchIndex:       return "SearchIndex";
            case LogSource::InputManager:      return "InputManager";
            case LogSource::InputStateMachine: return "InputStateMachine";
            case LogSource::AABB:              return "AABB";
            case LogSource::CanvasObject:      return "CanvasObject";
            case LogSource::Window:            return "Window";
            default:                           return "General";
        }
    }

    // Returns a timestamp in the format "HH:MM:SS.mmm"
    inline std::string GetCurrentTimestamp() {
        // all the time pulling and recording c++ mess
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt{};
    
        // for multiplatform
    #if defined(_WIN32)
        localtime_s(&bt, &timer);
    #else
        localtime_r(&timer, &bt);
    #endif
        // Creates an in-memory string stream to assemble the formatted text. (I wish I knew what that ment)
        std::ostringstream oss;
        oss << std::put_time(&bt, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    /**
     * @brief Returns a reference to the static mutex used for synchronizing console output.
    * 
    * WHY WE NEED THIS:
    * On multithreaded platforms (Android, Windows, Linux), multiple threads might try 
    * to print to the console (std::cout/std::cerr) simultaneously. Without a lock, 
    * their messages would get interleaved, making the log impossible to read.
    * 
    * HOW IT WORKS:
    * 1. Creates a static std::mutex instance (which is initialized only once).
    * 2. Returns a reference to it.
    * 
    * Thread Safety: The std::mutex handles the locking mechanism internally, ensuring that
    * only one thread can hold the lock and write to the console at any given time.
     */
    inline std::mutex& GetConsoleLogMutex() {
        static std::mutex s_logMtx;
        return s_logMtx;
    }

    // Global flag for verbose info logging
    inline bool enableVerboseLogging = true;

    /**
    * @brief Writes a formatted log message to the console.
    * 
    * HOW IT WORKS:
    * 1. Acquires a lock on the console mutex (via GetConsoleLogMutex) to ensure 
    *    exclusive access to the output stream.
    * 2. Checks for the message log level.
    * 3. Prints the log level tag, a space, the message, and a newline character.
    * 4. Releases the lock automatically when the std::lock_guard goes out of scope.
    * 
    * Thread Safety: The use of std::lock_guard ensures that log messages from 
    * different threads do not get garbled or interleaved in the console output.
    */
    inline void LogConsole(LogLevel level, LogSource source, const std::string& msg) {
        if (level == LogLevel::Info && !enableVerboseLogging) {
            return;
        }

        std::string timestamp = GetCurrentTimestamp();
        
        // Convert the LogLevel enum to a readable string
        const char* levelStr = "INFO";
        if (level == LogLevel::Error) {
            levelStr = "ERROR";
        } else if (level == LogLevel::Warn) {
            levelStr = "WARN";
        }

        const char* sourceStr = LogSourceToString(source);

        // Package into a structured entry
        FolioLogEntry entry{timestamp, levelStr, sourceStr, msg};

        // Locks error logging to current thread
        {
            std::lock_guard<std::mutex> lock(GetConsoleLogMutex());
            std::string formattedLine = "[" + timestamp + "] [" + levelStr + "] [" + sourceStr + "] " + msg;
            if (level == LogLevel::Error) {
                std::cerr << formattedLine << std::endl;
            } else {
                std::cout << formattedLine << std::endl;
            }
        }

        // Thread-safe File Redirection and Memory caching
        ::Folio::FileLogger::Instance().WriteLog(entry);
    }

}

#ifndef LOG_INFO
#define LOG_INFO(source, msg)  ::Folio::LogConsole(::Folio::LogLevel::Info, ::Folio::LogSource::source, msg)
#endif
#ifndef LOG_WARN
#define LOG_WARN(source, msg)  ::Folio::LogConsole(::Folio::LogLevel::Warn, ::Folio::LogSource::source, msg)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(source, msg) ::Folio::LogConsole(::Folio::LogLevel::Error, ::Folio::LogSource::source, msg)
#endif