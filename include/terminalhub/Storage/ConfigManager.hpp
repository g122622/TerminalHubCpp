#pragma once

#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Core/Types.hpp"

#include <filesystem>
#include <string>

namespace th {

/**
 * @brief Daemon configuration
 */
struct DaemonConfig {
    /** Named Pipe path */
    std::string socketPath;
    /** Log level: debug | info | warn | error */
    std::string logLevel;
};

/**
 * @brief Session configuration
 */
struct SessionConfig {
    /** Max output buffer lines */
    i32 outputBufferLines{1000};
    /** Max session title length */
    i32 titleMaxLength{50};
    /** Default shell: cmd | powershell | bash */
    std::string defaultShell{"powershell"};
};

/**
 * @brief Terminal configuration
 */
struct TerminalConfig {
    /** Default terminal columns */
    i32 cols{80};
    /** Default terminal rows */
    i32 rows{24};
};

/**
 * @brief Full configuration
 */
struct Config {
    std::string version{"1.0.0"};
    DaemonConfig daemon;
    SessionConfig session;
    TerminalConfig terminal;
};

/**
 * @brief TerminalHub common path constants
 */
struct Paths {
    /** ~/.terminalhub data directory */
    static std::filesystem::path terminalHubDir();
    /** Configuration file path */
    static std::filesystem::path configPath();
    /** Session metadata file path */
    static std::filesystem::path sessionsPath();
    /** Daemon PID file path */
    static std::filesystem::path daemonPidPath();
    /** Logs directory */
    static std::filesystem::path logsDir();
};

/**
 * @brief Configuration manager
 *
 * Principles:
 * 1. No hardcoded defaults - values must be read from the config file at runtime
 * 2. No silent fallbacks - missing config is an error
 * 3. No CLI config parameters - all config is managed via the config file only
 */
class ConfigManager {
public:
    /**
     * @brief Load the configuration file
     * @return Configuration, or error
     */
    [[nodiscard]] Result<Config> load();

    /**
     * @brief Get the loaded configuration
     * @return Config pointer, or nullptr if not loaded
     */
    [[nodiscard]] const Config* get() const;

    /**
     * @brief Initialize the configuration file
     *
     * Creates directory structure and default config file. Does not overwrite if config already exists.
     * @return Success or error
     */
    static Result<void> init();

private:
    /**
     * @brief Validate configuration completeness
     */
    [[nodiscard]] Result<void> validate(const Config& config) const;

    /**
     * @brief Get nested object value
     */
    [[nodiscard]] static std::string getNestedValue(const std::string& json, std::string_view path);

    bool m_loaded{false};
    Config m_config;
};

} // namespace th
