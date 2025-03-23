#pragma once

#include <memory>
#include <string>
#include <mutex>

#include <fmt/color.h>

namespace Atlas {
    enum class LogLevel {
        Trace = 0, // White
        Info, // Purple
        Warn, // Yellow
        Error, // Red
        Fatal // Bright Red
    };

    class Log {
    private:
        static std::shared_ptr<Log> coreLogger;
        LogLevel logLevel = LogLevel::Trace;
        std::mutex logMutex;

        std::string getLogPrefix(LogLevel level);

        fmt::color getLogColor(LogLevel level);

    public:
        static void init();

        static std::shared_ptr<Log>& getCoreLogger() {
            return coreLogger;
        }

        void setLogLevel(LogLevel level) {
            logLevel = level;
        }

        void platformLog(LogLevel level, const std::string &prefix, const std::string &message);

        void log(LogLevel level, const std::string &message);

        template <typename... Args>
        void log(LogLevel level, const std::string& format, Args&&... args) {
            log(level, fmt::format(format, std::forward<Args>(args)...));
        }
    };
}

// Logging Macros
#define AT_FATAL(...) { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Fatal, __VA_ARGS__); }
#define AT_ERROR(...) { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Error, __VA_ARGS__); }
#define AT_WARN(...)  { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Warn, __VA_ARGS__); }
#define AT_INFO(...)  { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Info, __VA_ARGS__); }
#define AT_TRACE(...) { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Trace, __VA_ARGS__); }