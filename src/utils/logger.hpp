#pragma once
#include <string>
#include <iostream>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>
#include "io/file_logger.hpp"
#include "utils/error_codes.hpp"


#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#endif

namespace Folio {

    enum class LogLevel {
        Info,
        Warn,
        Error
    };

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
        Window,
        Notebook,
        NavPanel,
        SettingsManager,
        PdfStorage,
        LibraryManager,
        FileManager,
        SectionGroup,
        Section,
        CanvasPage,
        DocumentSession,
        Workspace
    };

    inline const char* LogSourceToString(LogSource source) noexcept {
        switch (source) {
            case LogSource::FileManager:       return "FileManager";
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
            case LogSource::Notebook:          return "Notebook";
            case LogSource::NavPanel:          return "NavPanel";
            case LogSource::SettingsManager:   return "SettingsManager";
            case LogSource::PdfStorage:        return "PdfStorage";
            case LogSource::LibraryManager:    return "LibraryManager";
            case LogSource::SectionGroup:      return "SectionGroup";
            case LogSource::Section:           return "Section";
            case LogSource::CanvasPage:        return "CanvasPage";
            case LogSource::DocumentSession:   return "DocumentSession";
            case LogSource::Workspace:         return "Workspace";
            default:                           return "General";
        }
    }

    /**
     * @brief Multiplatform stack-allocated timestamp generator formatted as "HH:MM:SS.mmm".
     * Uses thread-safe platform variants (localtime_s on Windows, localtime_r on Linux/macOS/Android).
     * 
     * @return Formatted timestamp string by value.
     */
    inline std::string GetCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt{};

#if defined(_WIN32)
        localtime_s(&bt, &timer);
#else
        localtime_r(&timer, &bt);
#endif

        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                      bt.tm_hour, bt.tm_min, bt.tm_sec, static_cast<int>(ms.count()));
        return std::string(buf, 12);
    }

    inline std::mutex& GetConsoleLogMutex() {
        static std::mutex s_logMtx;
        return s_logMtx;
    }

    /// When true, all INFO logs are streamed to console, disk, and listeners (like Klipper verbose logging)
    inline bool enableVerboseLogging = true;

    // --- CLI / External Streamer Hook ---
    using LogStreamListener = std::function<void(const FolioLogEntry&)>;

    inline std::vector<LogStreamListener>& GetStreamListeners() {
        static std::vector<LogStreamListener> s_listeners;
        return s_listeners;
    }

    inline std::mutex& GetListenerMutex() {
        static std::mutex s_listenerMtx;
        return s_listenerMtx;
    }

    /**
     * @brief Registers a callback (CLI IPC server, socket stream, or custom viewer).
     * Invoked whenever a log entry is produced.
     */
    inline void RegisterLogListener(LogStreamListener listener) {
        std::lock_guard<std::mutex> lock(GetListenerMutex());
        GetStreamListeners().push_back(std::move(listener));
    }

    /**
     * @brief Central multiplatform logging dispatch function.
     * 
     * Working Process:
     *   1. Checks verbose filter: if level is INFO and verbose logging is disabled, drops immediately.
     *   2. Formats timestamp and severity string ("INFO", "WARN", "ERROR").
     *   3. Console / Terminal Output:
     *      - On Windows/Linux/macOS: Writes to std::cout or std::cerr (flushed on error).
     *      - On Android: Forwarded to SDL_Log for native LogCat capture.
     *   4. Disk File Persistence (FileLogger):
     *      - Every entry (including INFO) is written to `folionote_session.log`.
     *      - Buffered I/O: INFO writes to an OS memory buffer (~1-2 microseconds) without blocking the thread.
     *      - ERROR entries trigger an immediate physical disk flush to guarantee persistence across crashes.
     *   5. Real-Time Streaming: Broadcasts structured entry to any registered CLI/IPC listeners.
     */
    inline void LogConsole(LogLevel level, LogSource source, const std::string& msg) {
        if (level == LogLevel::Info && !enableVerboseLogging) {
            return;
        }

        std::string timestamp = GetCurrentTimestamp();
        
        const char* levelStr = "INFO";
        if (level == LogLevel::Error) {
            levelStr = "ERROR";
        } else if (level == LogLevel::Warn) {
            levelStr = "WARN";
        }

        const char* sourceStr = LogSourceToString(source);

        // Terminal / Console output
        {
            std::lock_guard<std::mutex> lock(GetConsoleLogMutex());
#if defined(__ANDROID__)
            // On Android, std::cout is redirected to /dev/null by default; route to logcat via SDL
            if (level == LogLevel::Error) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[%s] [%s] %s", sourceStr, levelStr, msg.c_str());
            } else if (level == LogLevel::Warn) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[%s] [%s] %s", sourceStr, levelStr, msg.c_str());
            } else {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%s] [%s] %s", sourceStr, levelStr, msg.c_str());
            }
#else
            std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
            out << '[' << timestamp << "] [" << levelStr << "] [" << sourceStr << "] " << msg << '\n';
            if (level == LogLevel::Error) {
                out.flush();
            }
#endif
        }

        // Structured entry for disk and CLI streaming
        FolioLogEntry entry{timestamp, levelStr, sourceStr, msg};

        // Local file backup (writes INFO, WARN, and ERROR to folionote_session.log)
        ::Folio::FileLogger::Instance().WriteLog(entry);

        // Broadcast to CLI listener if registered
        {
            std::lock_guard<std::mutex> lock(GetListenerMutex());
            for (const auto& listener : GetStreamListeners()) {
                if (listener) {
                    listener(entry);
                }
            }
        }
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
#ifndef LOG_ERROR_CODE
#define LOG_ERROR_CODE(source, code, msg) ::Folio::LogConsole(::Folio::LogLevel::Error, ::Folio::LogSource::source, ::Folio::FormatError(code, msg))
#endif
#ifndef LOG_WARN_CODE
#define LOG_WARN_CODE(source, code, msg)  ::Folio::LogConsole(::Folio::LogLevel::Warn, ::Folio::LogSource::source, ::Folio::FormatError(code, msg))
#endif