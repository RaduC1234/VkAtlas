#include "Log.hpp"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Atlas {

    std::shared_ptr<Log> Log::coreLogger = nullptr;

    void Log::init() {
        if (!coreLogger) {
            coreLogger = std::make_shared<Log>();
        }
    }

    std::shared_ptr<Log> &Log::getCoreLogger() {
        if (!coreLogger) {
            init();
        }

        return coreLogger;
    }

    std::string Log::getLogPrefix(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "[TRACE]";
            case LogLevel::Info:  return "[INFO]";
            case LogLevel::Warn:  return "[WARN]";
            case LogLevel::Error: return "[ERROR]";
            case LogLevel::Fatal: return "[FATAL]";
            default:              return "[LOG]";
        }
    }

#if defined(_WIN32)
    static void setConsoleColor(LogLevel level) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD color = 7; // Default white

        switch (level) {
            case LogLevel::Trace: color = 7; break;
            case LogLevel::Info:  color = 13; break; // Magenta
            case LogLevel::Warn:  color = 14; break; // Yellow
            case LogLevel::Error: color = 12; break; // Red
            case LogLevel::Fatal: color = 12 | FOREGROUND_INTENSITY; break; // Bright Red
            default:              color = 7; break;
        }
        SetConsoleTextAttribute(hConsole, color);
    }
#else
    static const char* getAnsiColorCode(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "\x1b[37m";
            case LogLevel::Info:  return "\x1b[35m";
            case LogLevel::Warn:  return "\x1b[33m";
            case LogLevel::Error: return "\x1b[31m";
            case LogLevel::Fatal: return "\x1b[91m";
            default:              return "\x1b[37m";
        }
    }
#endif

    void Log::platformLog(LogLevel level, const std::string& prefix, const std::string& message) {
#if defined(ATLAS_PLATFORM_WINDOWS)
        setConsoleColor(level);
        std::cout << prefix << " " << message << std::endl;
        setConsoleColor(LogLevel::Trace); // Reset

#else
        std::cout << getAnsiColorCode(level) << prefix << " " << message << "\x1b[0m" << std::endl;
#endif
    }

    void Log::log(LogLevel level, const std::string& message) {
        if (level < logLevel) return;

        std::lock_guard<std::mutex> lock(logMutex);
        std::string prefix = getLogPrefix(level);
        platformLog(level, prefix, message);
    }

}
