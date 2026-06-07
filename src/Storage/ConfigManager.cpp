#include "terminalhub/Storage/ConfigManager.hpp"
#include "terminalhub/Storage/JsonStorage.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace th {

// Paths 实现

std::filesystem::path Paths::terminalHubDir() {
    // Windows: %USERPROFILE%\.terminalhub
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile != nullptr) {
        return std::filesystem::path(userProfile) / ".terminalhub";
    }
    // 回退到 HOME
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

// ConfigManager 实现

Result<Config> ConfigManager::load() {
    auto configPath = Paths::configPath();

    if (!std::filesystem::exists(configPath)) {
        return Result<Config>::err(Error::configError(
            "配置文件不存在: " + configPath.string() + "\n请运行 'th init' 初始化配置",
            "ConfigManager::load"));
    }

    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            return Result<Config>::err(Error::ioError(
                "无法打开配置文件: " + configPath.string(),
                "ConfigManager::load"));
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        auto j = nlohmann::json::parse(content);

        Config config;
        config.version = j.value("version", "1.0.0");

        // daemon
        if (j.contains("daemon")) {
            auto& d = j["daemon"];
            config.daemon.socketPath = d.value("socketPath", "");
            config.daemon.logLevel = d.value("logLevel", "info");
        }

        // session
        if (j.contains("session")) {
            auto& s = j["session"];
            config.session.outputBufferLines = s.value("outputBufferLines", 1000);
            config.session.titleMaxLength = s.value("titleMaxLength", 50);
            config.session.defaultShell = s.value("defaultShell", "powershell");
        }

        // terminal
        if (j.contains("terminal")) {
            auto& t = j["terminal"];
            config.terminal.cols = t.value("cols", 80);
            config.terminal.rows = t.value("rows", 24);
        }

        // 验证必填字段
        auto validateResult = validate(config);
        if (!validateResult.success()) {
            return Result<Config>::err(std::move(validateResult.error()));
        }

        m_config = std::move(config);
        m_loaded = true;
        return Result<Config>::ok(m_config);

    } catch (const nlohmann::json::parse_error& e) {
        return Result<Config>::err(Error::configError(
            "配置文件格式错误: " + configPath.string() + "\n" + e.what(),
            "ConfigManager::load"));
    } catch (const std::exception& e) {
        return Result<Config>::err(Error::ioError(
            "读取配置文件失败: " + configPath.string() + "\n" + e.what(),
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
        // 创建目录
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }

        if (!std::filesystem::exists(logsDir)) {
            std::filesystem::create_directories(logsDir);
        }

        // 配置文件已存在则不覆盖
        if (std::filesystem::exists(configPath)) {
            return Result<void>::ok();
        }

        // 生成默认配置
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
                "无法创建配置文件: " + configPath.string(),
                "ConfigManager::init"));
        }

        file << j.dump(2);
        return Result<void>::ok();

    } catch (const std::exception& e) {
        return Result<void>::err(Error::ioError(
            "初始化失败: " + std::string(e.what()),
            "ConfigManager::init"));
    }
}

Result<void> ConfigManager::validate(const Config& config) const {
    // 必填字段验证
    if (config.daemon.socketPath.empty()) {
        return Result<void>::err(Error::configError(
            "配置项缺失: daemon.socketPath",
            "ConfigManager::validate"));
    }

    if (config.daemon.logLevel.empty()) {
        return Result<void>::err(Error::configError(
            "配置项缺失: daemon.logLevel",
            "ConfigManager::validate"));
    }

    // 验证 logLevel 值
    if (config.daemon.logLevel != "debug" && config.daemon.logLevel != "info"
        && config.daemon.logLevel != "warn" && config.daemon.logLevel != "error") {
        return Result<void>::err(Error::configError(
            "配置项无效: daemon.logLevel 必须是 debug|info|warn|error",
            "ConfigManager::validate"));
    }

    if (config.session.outputBufferLines <= 0) {
        return Result<void>::err(Error::configError(
            "配置项无效: session.outputBufferLines 必须是正整数",
            "ConfigManager::validate"));
    }

    if (config.session.titleMaxLength <= 0) {
        return Result<void>::err(Error::configError(
            "配置项无效: session.titleMaxLength 必须是正整数",
            "ConfigManager::validate"));
    }

    if (config.session.defaultShell.empty()) {
        return Result<void>::err(Error::configError(
            "配置项缺失: session.defaultShell",
            "ConfigManager::validate"));
    }

    if (config.terminal.cols <= 0) {
        return Result<void>::err(Error::configError(
            "配置项无效: terminal.cols 必须是正整数",
            "ConfigManager::validate"));
    }

    if (config.terminal.rows <= 0) {
        return Result<void>::err(Error::configError(
            "配置项无效: terminal.rows 必须是正整数",
            "ConfigManager::validate"));
    }

    return Result<void>::ok();
}

std::string ConfigManager::getNestedValue(const std::string& json, std::string_view path) {
    // 辅助方法，用于从 JSON 字符串中获取嵌套值
    // 目前未使用，保留供将来扩展
    (void)json;
    (void)path;
    return {};
}

} // namespace th
