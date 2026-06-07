#pragma once

#include "terminalhub/IPC/IpcMessage.hpp"
#include "terminalhub/Core/Types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace th::ipc {

/**
 * @brief Command handler type
 *
 * @param payload Request payload (JSON object)
 * @param clientId Client identifier (for sendToClient)
 * @return Response data (JSON), or the server constructs an error response on exception
 */
using CommandHandler = std::function<nlohmann::json(const nlohmann::json& payload, u64 clientId)>;

/**
 * @brief IOCP Named Pipe server
 *
 * Runs in the Daemon process, handles CLI client requests.
 * Uses newline-delimited JSON protocol.
 */
class IpcServer {
public:
    explicit IpcServer(const std::string& pipeName);
    ~IpcServer();

    // Non-copyable
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    /**
     * @brief Register a command handler
     */
    void onCommand(CommandType command, CommandHandler handler);

    /**
     * @brief Start the server
     */
    bool start();

    /**
     * @brief Stop the server
     */
    void stop();

    /**
     * @brief Broadcast an event to all clients
     */
    void broadcast(EventType eventType, const nlohmann::json& data,
                   const std::string& sessionId = "");

    /**
     * @brief Send an event to a specific client
     */
    void sendToClient(u64 clientId, EventType eventType, const nlohmann::json& data,
                      const std::string& sessionId = "");

    /**
     * @brief Get the number of connected clients
     */
    size_t getClientCount() const;

    /**
     * @brief Set client connect callback
     */
    void onClientConnect(std::function<void(u64 clientId)> callback);

    /**
     * @brief Set client disconnect callback
     */
    void onClientDisconnect(std::function<void(u64 clientId)> callback);

    /**
     * @brief Set error callback (called on JSON parse errors, connection errors, etc.)
     */
    void onError(std::function<void(const std::string& message)> callback);

private:
    /**
     * @brief IOCP worker thread
     */
    void _workerThread();

    /**
     * @brief Accept a new connection
     */
    void _acceptConnection();

    /**
     * @brief Handle completion port notifications
     */
    void _handleCompletion(DWORD bytesTransferred, ULONG_PTR completionKey,
                           OVERLAPPED* overlapped);

    /**
     * @brief Post an async read operation
     */
    void _postRead(u64 clientId);

    /**
     * @brief Handle received data (framed by \n)
     */
    void _handleData(u64 clientId, const std::string& data);

    /**
     * @brief Handle a request
     */
    void _handleRequest(u64 clientId, const IpcRequest& request);

    /**
     * @brief Send raw data to a client
     */
    void _sendRaw(u64 clientId, const std::string& data);

    /**
     * @brief Clean up client resources
     */
    void _cleanupClient(u64 clientId);

    std::string m_pipeName;  // Named Pipe name (e.g. \\.\pipe\terminalhub)

    // IOCP
    HANDLE m_hCompletionPort{nullptr};
    HANDLE m_hStopEvent{nullptr};

    // Client info
    struct ClientContext {
        HANDLE hPipe{INVALID_HANDLE_VALUE};
        OVERLAPPED overlapped{};
        char readBuffer[4096]{};
        std::string lineBuffer;  // Incomplete line buffer
        bool connected{false};
    };

    mutable std::mutex m_clientsMutex;
    std::unordered_map<u64, std::unique_ptr<ClientContext>> m_clients;
    u64 m_nextClientId{1};

    // Command handlers
    std::unordered_map<CommandType, CommandHandler> m_handlers;

    // Callbacks
    std::function<void(u64)> m_onClientConnect;
    std::function<void(u64)> m_onClientDisconnect;
    std::function<void(const std::string&)> m_onError;

    // Worker threads
    std::vector<std::thread> m_workers;
    volatile bool m_running{false};
};

} // namespace th::ipc
