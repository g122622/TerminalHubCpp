#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace spdlog {
class logger;
} // namespace spdlog

namespace th {

/**
 * @brief Log level
 */
enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

/**
 * @brief Logger, wraps spdlog
 */
class Logger {
public:
    /**
     * @brief Initialize the logging system
     * @param level Log level
     * @param prefix Log prefix (default "TerminalHub")
     */
    static void init(LogLevel level, std::string_view prefix = "TerminalHub");

    /**
     * @brief Get the underlying spdlog logger
     */
    static std::shared_ptr<spdlog::logger> get();

    static void debug(std::string_view msg);
    static void info(std::string_view msg);
    static void warn(std::string_view msg);
    static void error(std::string_view msg);

private:
    static LogLevel s_level;
    static std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace th
