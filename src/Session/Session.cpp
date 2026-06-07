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
    removeClientListeners(clientId);
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
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    for (auto& [clientId, callback] : m_onOutputListeners) {
        callback(data);
    }
    if (m_onOutputGlobal) {
        m_onOutputGlobal(data);
    }
}

void Session::broadcastExit(u32 exitCode) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    for (auto& [clientId, callback] : m_onExitListeners) {
        callback(exitCode);
    }
    if (m_onExitGlobal) {
        m_onExitGlobal(exitCode);
    }
}

void Session::onOutput(std::function<void(const std::string&)> callback) {
    m_onOutputGlobal = std::move(callback);
}

void Session::onExit(std::function<void(u32)> callback) {
    m_onExitGlobal = std::move(callback);
}

void Session::addOutputListener(u64 clientId, std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    m_onOutputListeners.emplace_back(clientId, std::move(callback));
}

void Session::addExitListener(u64 clientId, std::function<void(u32)> callback) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    m_onExitListeners.emplace_back(clientId, std::move(callback));
}

void Session::removeClientListeners(u64 clientId) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    m_onOutputListeners.erase(
        std::remove_if(m_onOutputListeners.begin(), m_onOutputListeners.end(),
            [clientId](const auto& pair) { return pair.first == clientId; }),
        m_onOutputListeners.end());
    m_onExitListeners.erase(
        std::remove_if(m_onExitListeners.begin(), m_onExitListeners.end(),
            [clientId](const auto& pair) { return pair.first == clientId; }),
        m_onExitListeners.end());
}

} // namespace th
