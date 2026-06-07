#pragma once

#include "terminalhub/Session/OutputBuffer.hpp"
#include "terminalhub/Core/Types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>
#include <mutex>

namespace th {

// Forward declaration
class ConPty;

/**
 * @brief Session metadata
 */
struct SessionMetadata {
    std::string id;              // th_{timestamp}_{random}
    std::string title;
    std::string shell;
    std::string cwd;
    DWORD pid = 0;
    i64 createdAt = 0;           // Unix milliseconds
    i64 lastActivityAt = 0;
    i32 connectedClients = 0;
};

/**
 * @brief Session list item (for th list display)
 */
struct SessionListItem {
    std::string id;
    std::string title;
    std::string shell;
    DWORD pid = 0;
    i64 createdAt = 0;
    i64 lastActivityAt = 0;
    i32 connectedClients = 0;
    bool alive = false;
};

/**
 * @brief Session entity
 *
 * Manages a PTY session with output buffer, client connections and event notifications.
 * Supports multiple output/exit listeners (one per attached client).
 */
class Session {
public:
    Session(SessionMetadata metadata, i32 outputBufferSize);

    // Client management
    void addClient(u64 clientId);
    void removeClient(u64 clientId);
    [[nodiscard]] const std::unordered_set<u64>& clients() const;

    /**
     * @brief Update last activity time
     */
    void touch();

    /**
     * @brief Check if PTY process is alive
     */
    [[nodiscard]] bool isAlive() const;

    // Event notification
    void broadcastOutput(const std::string& data);
    void broadcastExit(u32 exitCode);

    // Callback registration (supports multiple listeners)
    void onOutput(std::function<void(const std::string&)> callback);
    void onExit(std::function<void(u32)> callback);

    // Per-client listener registration (used by daemon for IPC forwarding)
    void addOutputListener(u64 clientId, std::function<void(const std::string&)> callback);
    void addExitListener(u64 clientId, std::function<void(u32)> callback);

    // Remove specific output/exit listeners by client ID
    void removeClientListeners(u64 clientId);

    // Data members
    SessionMetadata metadata;
    OutputBuffer outputBuffer;
    std::unique_ptr<ConPty> ptyProcess;

private:
    std::unordered_set<u64> m_clients;
    std::mutex m_listenersMutex;
    std::vector<std::pair<u64, std::function<void(const std::string&)>>> m_onOutputListeners;
    std::vector<std::pair<u64, std::function<void(u32)>>> m_onExitListeners;
    // Legacy single-callback support (used by SessionManager for logging)
    std::function<void(const std::string&)> m_onOutputGlobal;
    std::function<void(u32)> m_onExitGlobal;
};

} // namespace th
