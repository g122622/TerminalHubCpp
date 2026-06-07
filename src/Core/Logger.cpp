#include "terminalhub/Core/Logger.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace th {

LogLevel Logger::s_level = LogLevel::Info;
std::shared_ptr<spdlog::logger> Logger::s_logger;

void Logger::init(LogLevel level, std::string_view prefix) {
    s_level = level;

    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    s_logger = std::make_shared<spdlog::logger>(std::string(prefix), sink);

    // Format: [timestamp] [prefix] [level] message
    s_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

    auto spdlogLevel = spdlog::level::info;
    switch (level) {
    case LogLevel::Debug: spdlogLevel = spdlog::level::debug; break;
    case LogLevel::Info: spdlogLevel = spdlog::level::info; break;
    case LogLevel::Warn: spdlogLevel = spdlog::level::warn; break;
    case LogLevel::Error: spdlogLevel = spdlog::level::err; break;
    }
    s_logger->set_level(spdlogLevel);

    spdlog::set_default_logger(s_logger);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    return s_logger;
}

void Logger::debug(std::string_view msg) {
    if (s_logger) {
        s_logger->debug(msg);
    }
}

void Logger::info(std::string_view msg) {
    if (s_logger) {
        s_logger->info(msg);
    }
}

void Logger::warn(std::string_view msg) {
    if (s_logger) {
        s_logger->warn(msg);
    }
}

void Logger::error(std::string_view msg) {
    if (s_logger) {
        s_logger->error(msg);
    }
}

void Logger::error(std::string_view msg, const std::exception& ex) {
    if (s_logger) {
        s_logger->error("{} | Exception: {}", msg, ex.what());
    }
}

} // namespace th
