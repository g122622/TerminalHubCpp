#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace spdlog {
class logger;
} // namespace spdlog

namespace th {

/**
 * @brief 日志级别
 */
enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

/**
 * @brief 日志器，封装 spdlog
 */
class Logger {
public:
    /**
     * @brief 初始化日志系统
     * @param level 日志级别
     * @param prefix 日志前缀（默认 "TerminalHub"）
     */
    static void init(LogLevel level, std::string_view prefix = "TerminalHub");

    /**
     * @brief 获取底层 spdlog logger
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
