#include "terminalhub/Storage/ConfigManager.hpp"
#include "terminalhub/Storage/JsonStorage.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace th {

// Paths implementation

std::filesystem::path Paths::terminalHubDir() {
    // Windows: %USERPROFILE%\.terminalhub
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile != nullptr) {
        return std::filesystem::path(userProfile) / ".terminalhub";
    }
    // Fallback to HOME
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        return std::filesystem::path(home) / ".terminalhub";
    }
    return std::filesystem::path(".terminalhub");
}

std::filesystem::path Paths::configPath() {
    return terminalHubDir() / "config.json";
}

std::filesystem::path Paths::sessionsPath() {
    return terminalHubDir() / "sessions.json";
}

std::filesystem::path Paths::daemonPidPath() {
    return terminalHubDir() / "daemon.pid";
}

std::filesystem::path Paths::logsDir() {
    return terminalHubDir() / "logs";
}

// ConfigManager implementation

Result<Config> ConfigManager::load() {
    auto configPath = Paths::configPath();

    if (!std::filesystem::exists(configPath)) {
        return Result<Config>::err(Error::configError(
            "Config file not found: " + configPath.string() + "\nRun 'th init' to initialize configuration",
            "ConfigManager::load"));
    }

    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            return Result<Config>::err(Error::ioError(
                "Unable to open config file: " + configPath.string(),
                "ConfigManager::load"));
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        auto j = nlohmann::json::parse(content);

        Config config;
        config.version = j.value("version", "1.0.0");

        // daemon - required fields, no defaults
        if (!j.contains("daemon") || !j["daemon"].is_object()) {
            return Result<Config>::err(Error::configError(
                "Missing config section: daemon",
                "ConfigManager::load"));
        }
        auto& d = j["daemon"];
        if (!d.contains("socketPath")) {
            return Result<Config>::err(Error::configError(
                "Missing config: daemon.socketPath",
                "ConfigManager::load"));
        }
        config.daemon.socketPath = d["socketPath"].get<std::string>();
        if (!d.contains("logLevel")) {
            return Result<Config>::err(Error::configError(
                "Missing config: daemon.logLevel",
                "ConfigManager::load"));
        }
        config.daemon.logLevel = d["logLevel"].get<std::string>();

        // session - required fields, no defaults
        if (!j.contains("session") || !j["session"].is_object()) {
            return Result<Config>::err(Error::configError(
                "Missing config section: session",
                "ConfigManager::load"));
        }
        auto& s = j["session"];
        if (!s.contains("outputBufferLines")) {
            return Result<Config>::err(Error::configError(
                "Missing config: session.outputBufferLines",
                "ConfigManager::load"));
        }
        config.session.outputBufferLines = s["outputBufferLines"].get<i32>();
        if (!s.contains("titleMaxLength")) {
            return Result<Config>::err(Error::configError(
                "Missing config: session.titleMaxLength",
                "ConfigManager::load"));
        }
        config.session.titleMaxLength = s["titleMaxLength"].get<i32>();
        if (!s.contains("defaultShell")) {
            return Result<Config>::err(Error::configError(
                "Missing config: session.defaultShell",
                "ConfigManager::load"));
        }
        config.session.defaultShell = s["defaultShell"].get<std::string>();

        // terminal - required fields, no defaults
        if (!j.contains("terminal") || !j["terminal"].is_object()) {
            return Result<Config>::err(Error::configError(
                "Missing config section: terminal",
                "ConfigManager::load"));
        }
        auto& t = j["terminal"];
        if (!t.contains("cols")) {
            return Result<Config>::err(Error::configError(
                "Missing config: terminal.cols",
                "ConfigManager::load"));
        }
        config.terminal.cols = t["cols"].get<i32>();
        if (!t.contains("rows")) {
            return Result<Config>::err(Error::configError(
                "Missing config: terminal.rows",
                "ConfigManager::load"));
        }
        config.terminal.rows = t["rows"].get<i32>();

        // Validate required fields
        auto validateResult = validate(config);
        if (!validateResult.success()) {
            return Result<Config>::err(std::move(validateResult.error()));
        }

        m_config = std::move(config);
        m_loaded = true;
        return Result<Config>::ok(m_config);

    } catch (const nlohmann::json::parse_error& e) {
        return Result<Config>::err(Error::configError(
            "Invalid config file format: " + configPath.string() + "\n" + e.what(),
            "ConfigManager::load"));
    } catch (const std::exception& e) {
        return Result<Config>::err(Error::ioError(
            "Failed to read config file: " + configPath.string() + "\n" + e.what(),
            "ConfigManager::load"));
    }
}

const Config* ConfigManager::get() const {
    if (!m_loaded) {
        return nullptr;
    }
    return &m_config;
}

Result<void> ConfigManager::init() {
    auto dir = Paths::terminalHubDir();
    auto logsDir = Paths::logsDir();
    auto configPath = Paths::configPath();

    try {
        // Create directories
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }

        if (!std::filesystem::exists(logsDir)) {
            std::filesystem::create_directories(logsDir);
        }

        // Don't overwrite existing config
        if (std::filesystem::exists(configPath)) {
            return Result<void>::ok();
        }

        // Generate default config
        Config defaultConfig;
        defaultConfig.daemon.socketPath = (dir / "daemon.sock").string();

        nlohmann::json j;
        j["version"] = defaultConfig.version;
        j["daemon"]["socketPath"] = defaultConfig.daemon.socketPath;
        j["daemon"]["logLevel"] = defaultConfig.daemon.logLevel;
        j["session"]["outputBufferLines"] = defaultConfig.session.outputBufferLines;
        j["session"]["titleMaxLength"] = defaultConfig.session.titleMaxLength;
        j["session"]["defaultShell"] = defaultConfig.session.defaultShell;
        j["terminal"]["cols"] = defaultConfig.terminal.cols;
        j["terminal"]["rows"] = defaultConfig.terminal.rows;

        std::ofstream file(configPath);
        if (!file.is_open()) {
            return Result<void>::err(Error::ioError(
                "Failed to create config file: " + configPath.string(),
                "ConfigManager::init"));
        }

        file << j.dump(2);
        return Result<void>::ok();

    } catch (const std::exception& e) {
        return Result<void>::err(Error::ioError(
            "Init failed: " + std::string(e.what()),
            "ConfigManager::init"));
    }
}

Result<void> ConfigManager::validate(const Config& config) const {
    // Validate logLevel value
    if (config.daemon.logLevel != "debug" && config.daemon.logLevel != "info"
        && config.daemon.logLevel != "warn" && config.daemon.logLevel != "error") {
        return Result<void>::err(Error::configError(
            "Invalid config: daemon.logLevel must be debug|info|warn|error",
            "ConfigManager::validate"));
    }

    // Validate defaultShell value
    if (config.session.defaultShell != "cmd" && config.session.defaultShell != "powershell"
        && config.session.defaultShell != "bash") {
        return Result<void>::err(Error::configError(
            "Invalid config: session.defaultShell must be cmd|powershell|bash",
            "ConfigManager::validate"));
    }

    if (config.session.outputBufferLines <= 0) {
        return Result<void>::err(Error::configError(
            "Invalid config: session.outputBufferLines must be positive",
            "ConfigManager::validate"));
    }

    if (config.session.titleMaxLength <= 0) {
        return Result<void>::err(Error::configError(
            "Invalid config: session.titleMaxLength must be positive",
            "ConfigManager::validate"));
    }

    if (config.terminal.cols <= 0) {
        return Result<void>::err(Error::configError(
            "Invalid config: terminal.cols must be positive",
            "ConfigManager::validate"));
    }

    if (config.terminal.rows <= 0) {
        return Result<void>::err(Error::configError(
            "Invalid config: terminal.rows must be positive",
            "ConfigManager::validate"));
    }

    return Result<void>::ok();
}

std::string ConfigManager::getNestedValue(const std::string& json, std::string_view path) {
    // Helper method for getting nested values from JSON string
    // Currently unused, kept for future extension
    (void)json;
    (void)path;
    return {};
}

} // namespace th
