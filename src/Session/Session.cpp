#include "terminalhub/Session/Session.hpp"
#include "terminalhub/PTY/ConPty.hpp"

namespace th {

Session::Session(SessionMetadata metadata, i32 outputBufferSize)
    : metadata(std::move(metadata))
    , outputBuffer(outputBufferSize) {
}

void Session::addClient(u64 clientId) {
    m_clients.insert(clientId);
    metadata.connectedClients = static_cast<i32>(m_clients.size());
}

void Session::removeClient(u64 clientId) {
    m_clients.erase(clientId);
    metadata.connectedClients = static_cast<i32>(m_clients.size());
}

const std::unordered_set<u64>& Session::clients() const {
    return m_clients;
}

void Session::touch() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    metadata.lastActivityAt =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool Session::isAlive() const {
    return ptyProcess && ptyProcess->isAlive();
}

void Session::broadcastOutput(const std::string& data) {
    if (m_onOutput) {
        m_onOutput(data);
    }
}

void Session::broadcastExit(u32 exitCode) {
    if (m_onExit) {
        m_onExit(exitCode);
    }
}

void Session::onOutput(std::function<void(const std::string&)> callback) {
    m_onOutput = std::move(callback);
}

void Session::onExit(std::function<void(u32)> callback) {
    m_onExit = std::move(callback);
}

} // namespace th
