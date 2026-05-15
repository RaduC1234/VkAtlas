#include "Log.hpp"

#include <iostream>

#if defined(__ANDROID__)
#include <android/log.h>
#elif defined(_WIN32)
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

    fmt::color Log::getLogColor(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return fmt::color::white;
            case LogLevel::Info:  return fmt::color::magenta;
            case LogLevel::Warn:  return fmt::color::yellow;
            case LogLevel::Error: return fmt::color::red;
            case LogLevel::Fatal: return fmt::color::crimson;
            default:              return fmt::color::white;
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
#endif

    void Log::platformLog(LogLevel level, const std::string& prefix, const std::string& message) {
#if defined(__ANDROID__)
        android_LogPriority priority;
        switch (level) {
            case LogLevel::Trace: priority = ANDROID_LOG_VERBOSE; break;
            case LogLevel::Info:  priority = ANDROID_LOG_INFO;    break;
            case LogLevel::Warn:  priority = ANDROID_LOG_WARN;    break;
            case LogLevel::Error: priority = ANDROID_LOG_ERROR;   break;
            case LogLevel::Fatal: priority = ANDROID_LOG_FATAL;   break;
            default:              priority = ANDROID_LOG_DEBUG;   break;
        }
        __android_log_print(priority, "Atlas", "%s: %s", prefix.c_str(), message.c_str());

#elif defined(_WIN32)
        setConsoleColor(level);
        std::cout << prefix << " " << message << std::endl;
        setConsoleColor(LogLevel::Trace); // Reset

#else
        fmt::print(fg(getLogColor(level)), "{} {}\n", prefix, message);
#endif
    }

    void Log::log(LogLevel level, const std::string& message) {
        if (level < logLevel) return;

        std::lock_guard<std::mutex> lock(logMutex);
        std::string prefix = getLogPrefix(level);
        platformLog(level, prefix, message);
    }

}
