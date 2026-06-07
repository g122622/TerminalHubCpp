#pragma once

#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Core/Types.hpp"

#include <filesystem>
#include <string>

namespace th {

/**
 * @brief 守护进程配置
 */
struct DaemonConfig {
    /** Named Pipe 路径 */
    std::string socketPath;
    /** 日志级别: debug | info | warn | error */
    std::string logLevel;
};

/**
 * @brief 会话配置
 */
struct SessionConfig {
    /** 输出缓冲区最大行数 */
    i32 outputBufferLines{1000};
    /** 会话标题最大长度 */
    i32 titleMaxLength{50};
    /** 默认 shell: cmd | powershell | bash */
    std::string defaultShell{"powershell"};
};

/**
 * @brief 终端配置
 */
struct TerminalConfig {
    /** 终端默认列数 */
    i32 cols{80};
    /** 终端默认行数 */
    i32 rows{24};
};

/**
 * @brief 完整配置
 */
struct Config {
    std::string version{"1.0.0"};
    DaemonConfig daemon;
    SessionConfig session;
    TerminalConfig terminal;
};

/**
 * @brief TerminalHub 常用路径常量
 */
struct Paths {
    /** ~/.terminalhub 数据目录 */
    static std::filesystem::path terminalHubDir();
    /** 配置文件路径 */
    static std::filesystem::path configPath();
    /** 会话元数据文件路径 */
    static std::filesystem::path sessionsPath();
    /** 守护进程 PID 文件路径 */
    static std::filesystem::path daemonPidPath();
    /** 日志目录 */
    static std::filesystem::path logsDir();
};

/**
 * @brief 配置管理器
 *
 * 原则:
 * 1. 禁止硬编码默认值 - 运行时必须从配置文件读取
 * 2. 禁止静默回退 - 配置缺失直接报错
 * 3. 禁止命令行配置参数 - 所有配置仅通过配置文件管理
 */
class ConfigManager {
public:
    /**
     * @brief 加载配置文件
     * @return 配置，或错误
     */
    [[nodiscard]] Result<Config> load();

    /**
     * @brief 获取已加载的配置
     * @return 配置指针，未加载时返回 nullptr
     */
    [[nodiscard]] const Config* get() const;

    /**
     * @brief 初始化配置文件
     *
     * 创建目录结构和默认配置文件。如果配置文件已存在则不覆盖。
     * @return 成功或错误
     */
    static Result<void> init();

private:
    /**
     * @brief 验证配置完整性
     */
    [[nodiscard]] Result<void> validate(const Config& config) const;

    /**
     * @brief 获取嵌套对象的值
     */
    [[nodiscard]] static std::string getNestedValue(const std::string& json, std::string_view path);

    bool m_loaded{false};
    Config m_config;
};

} // namespace th
