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
    // Clean up stale sessions
    auto removed = m_registry.cleanup();
    if (!removed.empty()) {
        Logger::info("Cleaned up " + std::to_string(removed.size()) + " stale sessions");
    }
}

Result<Session*> SessionManager::createSession(const CreateSessionOptions& options) {
    std::string id = ipc::generateSessionId();
    std::string shell = options.shell.value_or(getDefaultShell(m_config.session.defaultShell));
    i32 cols = options.cols.value_or(m_config.terminal.cols);
    i32 rows = options.rows.value_or(m_config.terminal.rows);

    // Create PTY process
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

    // Create output buffer
    i32 bufferSize = m_config.session.outputBufferLines;

    // Create session metadata
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

    // Create session
    auto session = std::make_unique<Session>(std::move(metadata), bufferSize);
    Session* rawPtr = session.get();

    // Store PTY pointer
    session->ptyProcess = std::move(pty);

    // Listen for output
    session->ptyProcess->onOutput([rawPtr](std::string_view data) {
        rawPtr->outputBuffer.write(data);
        rawPtr->broadcastOutput(std::string(data));
    });

    // Listen for exit
    std::string sessionId = id;
    session->ptyProcess->onExit([this, sessionId](u32 exitCode) {
        Logger::info("Session " + sessionId + " exited: code=" + std::to_string(exitCode));
        _handleSessionExit(sessionId, exitCode);
    });

    // Persist to registry
    m_registry.save(session->metadata);

    // Store in memory
    m_sessions[id] = std::move(session);

    Logger::info("Session created: " + id + " (PID: " + std::to_string(rawPtr->metadata.pid) + ")");

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
            // Use real-time data from memory
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

    Logger::info("Session killed: " + sessionId);
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

    Logger::info("Session renamed: " + sessionId + " -> " + newTitle);
    return true;
}

void SessionManager::_handleSessionExit(const std::string& sessionId, u32 exitCode) {
    auto it = m_sessions.find(sessionId);
    if (it != m_sessions.end()) {
        it->second->ptyProcess.reset();
        it->second->broadcastExit(exitCode);
        m_registry.remove(sessionId);
    }
}

} // namespace th
