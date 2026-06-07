#include "terminalhub/Session/SessionManager.hpp"
#include "terminalhub/PTY/ConPty.hpp"
#include "terminalhub/IPC/IpcMessage.hpp"
#include "terminalhub/Core/Logger.hpp"

#include <chrono>
#include <nlohmann/json.hpp>

namespace th {

SessionManager::SessionManager(const Config& config)
    : m_config(config) {
}

void SessionManager::initialize() {
    // 清理无效会话
    auto removed = m_registry.cleanup();
    if (!removed.empty()) {
        Logger::info("清理了 " + std::to_string(removed.size()) + " 个无效会话");
    }
}

Result<Session*> SessionManager::createSession(const CreateSessionOptions& options) {
    std::string id = ipc::generateSessionId();
    std::string shell = options.shell.value_or(getDefaultShell(m_config.session.defaultShell));
    i32 cols = options.cols.value_or(m_config.terminal.cols);
    i32 rows = options.rows.value_or(m_config.terminal.rows);

    // 创建 PTY 进程
    ConPtyOptions ptyOpts;
    ptyOpts.shell = shell;
    ptyOpts.cols = cols;
    ptyOpts.rows = rows;
    if (options.cwd) {
        ptyOpts.cwd = *options.cwd;
    }

    auto ptyResult = ConPty::create(ptyOpts);
    if (!ptyResult.success()) {
        return Result<Session*>::err(std::move(ptyResult.error()));
    }

    auto pty = std::move(ptyResult.value());

    // 创建输出缓冲区
    i32 bufferSize = m_config.session.outputBufferLines;

    // 创建会话元数据
    SessionMetadata metadata;
    metadata.id = id;
    metadata.title = options.title.value_or("Session " + id);
    metadata.shell = shell;
    metadata.cwd = options.cwd.value_or("");
    metadata.pid = pty->pid();
    auto now = std::chrono::system_clock::now().time_since_epoch();
    metadata.createdAt =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    metadata.lastActivityAt = metadata.createdAt;
    metadata.connectedClients = 0;

    // 创建会话
    auto session = std::make_unique<Session>(std::move(metadata), bufferSize);
    Session* rawPtr = session.get();

    // 保存 PTY 指针
    session->ptyProcess = std::move(pty);

    // 监听输出
    session->ptyProcess->onOutput([rawPtr](std::string_view data) {
        rawPtr->outputBuffer.write(data);
        rawPtr->broadcastOutput(std::string(data));
    });

    // 监听退出
    std::string sessionId = id; // 捕获用
    session->ptyProcess->onExit([this, sessionId](u32 exitCode) {
        Logger::info("会话 " + sessionId + " 退出: code=" + std::to_string(exitCode));
        _handleSessionExit(sessionId);
    });

    // 持久化
    m_registry.save(session->metadata);

    // 存入内存
    m_sessions[id] = std::move(session);

    Logger::info("创建会话: " + id + " (PID: " + std::to_string(rawPtr->metadata.pid) + ")");

    return Result<Session*>::ok(rawPtr);
}

Session* SessionManager::getSession(const std::string& sessionId) {
    auto it = m_sessions.find(sessionId);
    if (it != m_sessions.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<SessionListItem> SessionManager::listSessions() {
    auto persistedItems = m_registry.list();
    std::vector<SessionListItem> result;

    for (auto& item : persistedItems) {
        auto* session = getSession(item.id);
        if (session) {
            // 使用内存中的实时数据
            item.connectedClients = session->metadata.connectedClients;
        }
        result.push_back(item);
    }

    return result;
}

bool SessionManager::killSession(const std::string& sessionId) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || !it->second->ptyProcess) {
        return false;
    }

    it->second->ptyProcess->kill();
    m_sessions.erase(it);
    m_registry.remove(sessionId);

    Logger::info("终止会话: " + sessionId);
    return true;
}

bool SessionManager::renameSession(const std::string& sessionId,
                                     const std::string& newTitle) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) {
        return false;
    }

    it->second->metadata.title = newTitle;
    m_registry.save(it->second->metadata);

    Logger::info("重命名会话: " + sessionId + " -> " + newTitle);
    return true;
}

void SessionManager::_handleSessionExit(const std::string& sessionId) {
    auto it = m_sessions.find(sessionId);
    if (it != m_sessions.end()) {
        it->second->ptyProcess.reset();
        it->second->broadcastExit(0);
        m_registry.remove(sessionId);
    }
}

} // namespace th
