#pragma once

#include <memory>
#include <string>
#include <mutex>

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/args.h>

namespace Atlas {
    enum class LogLevel {
        Trace = 0, // White
        Info, // Purple
        Warn, // Yellow
        Error, // Red
        Fatal // Bright Red
    };

    class Log {
    public:
        static void init();

        static std::shared_ptr<Log> &getCoreLogger();

        void setLogLevel(LogLevel level) {
            logLevel = level;
        }

        void platformLog(LogLevel level, const std::string &prefix, const std::string &message);

        void log(LogLevel level, const std::string &message);

        template<typename... Args>
        void log(LogLevel level, const std::string &format, Args &&... args) {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            (store.push_back(std::forward<Args>(args)), ...);
            auto message = fmt::vformat(format, store);
            log(level, message);
        }

    private:
        static std::shared_ptr<Log> coreLogger;
        LogLevel logLevel = LogLevel::Trace;
        std::mutex logMutex;

    private:
        std::string getLogPrefix(LogLevel level);

        fmt::color getLogColor(LogLevel level);
    };
}

#define AT_FATAL(...) { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Fatal, __VA_ARGS__); exit(1);}
#define AT_ERROR(...) { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Error, __VA_ARGS__); }
#define AT_WARN(...)  { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Warn, __VA_ARGS__); }
#define AT_INFO(...)  { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Info, __VA_ARGS__); }
#define AT_TRACE(...) { Atlas::Log::getCoreLogger()->log(Atlas::LogLevel::Trace, __VA_ARGS__); }
