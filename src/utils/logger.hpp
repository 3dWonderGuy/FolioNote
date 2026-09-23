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

// REMOVE WINDOWS PRINTOUT
// ADD CLI SUPPORT FOR LIVE LOG TRACK

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

    // Windows stack-allocated timestamp generator: "HH:MM:SS.mmm"
    inline std::string GetCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt{};

        localtime_s(&bt, &timer);

        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                      bt.tm_hour, bt.tm_min, bt.tm_sec, static_cast<int>(ms.count()));
        return std::string(buf, 12);
    }

    inline std::mutex& GetConsoleLogMutex() {
        static std::mutex s_logMtx;
        return s_logMtx;
    }

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

        // Windows console output
        {
            std::lock_guard<std::mutex> lock(GetConsoleLogMutex());
            std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
            out << '[' << timestamp << "] [" << levelStr << "] [" << sourceStr << "] " << msg << '\n';
            if (level == LogLevel::Error) {
                out.flush();
            }
        }

        // Structured entry for disk and CLI streaming
        FolioLogEntry entry{timestamp, levelStr, sourceStr, msg};

        // Local file backup
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