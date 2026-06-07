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
 * @brief Create session options
 */
struct CreateSessionOptions {
    std::optional<std::string> title;
    std::optional<std::string> cwd;
    std::optional<std::string> shell;
    std::optional<i32> cols;
    std::optional<i32> rows;
};

/**
 * @brief Session manager
 *
 * Manages PTY session lifecycle: creation, lookup, termination, renaming.
 */
class SessionManager {
public:
    explicit SessionManager(const Config& config);

    /**
     * @brief Initialize: load sessions from persistence, clean up dead ones
     */
    void initialize();

    /**
     * @brief Create a new session
     */
    Result<Session*> createSession(const CreateSessionOptions& options);

    /**
     * @brief Get a session
     */
    Session* getSession(const std::string& sessionId);

    /**
     * @brief List all sessions
     */
    std::vector<SessionListItem> listSessions();

    /**
     * @brief Kill a session
     */
    bool killSession(const std::string& sessionId);

    /**
     * @brief Rename a session
     */
    bool renameSession(const std::string& sessionId, const std::string& newTitle);

private:
    /**
     * @brief Handle session exit
     */
    void _handleSessionExit(const std::string& sessionId, u32 exitCode);

    Config m_config;
    SessionRegistry m_registry;
    std::unordered_map<std::string, std::unique_ptr<Session>> m_sessions;
};

} // namespace th
