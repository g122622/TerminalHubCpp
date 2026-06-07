#pragma once

#include "terminalhub/Session/Session.hpp"
#include "terminalhub/Session/SessionRegistry.hpp"
#include "terminalhub/Storage/ConfigManager.hpp"
#include "terminalhub/Core/Result.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace th {

/**
 * @brief 创建会话选项
 */
struct CreateSessionOptions {
    std::optional<std::string> title;
    std::optional<std::string> cwd;
    std::optional<std::string> shell;
    std::optional<i32> cols;
    std::optional<i32> rows;
};

/**
 * @brief 会话管理器
 *
 * 管理 PTY 会话的生命周期：创建、查找、终止、重命名。
 */
class SessionManager {
public:
    explicit SessionManager(const Config& config);

    /**
     * @brief 初始化：从持久化加载会话，清理已死亡的
     */
    void initialize();

    /**
     * @brief 创建新会话
     */
    Result<Session*> createSession(const CreateSessionOptions& options);

    /**
     * @brief 获取会话
     */
    Session* getSession(const std::string& sessionId);

    /**
     * @brief 列出所有会话
     */
    std::vector<SessionListItem> listSessions();

    /**
     * @brief 终止会话
     */
    bool killSession(const std::string& sessionId);

    /**
     * @brief 重命名会话
     */
    bool renameSession(const std::string& sessionId, const std::string& newTitle);

private:
    /**
     * @brief 处理会话退出
     */
    void _handleSessionExit(const std::string& sessionId);

    Config m_config;
    SessionRegistry m_registry;
    std::unordered_map<std::string, std::unique_ptr<Session>> m_sessions;
};

} // namespace th
