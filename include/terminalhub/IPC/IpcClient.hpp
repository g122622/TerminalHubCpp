#pragma once

#include "terminalhub/IPC/IpcMessage.hpp"
#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Core/Types.hpp"

#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace th::ipc {

/**
 * @brief Named Pipe IPC client
 *
 * Used for CLI-to-Daemon process communication.
 * Uses newline-delimited JSON protocol.
 */
class IpcClient {
public:
    IpcClient();
    ~IpcClient();

    // Non-copyable
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    /**
     * @brief Connect to the Daemon
     * @param pipePath Named Pipe path (e.g. \\.\pipe\terminalhub)
     */
    Result<void> connect(const std::string& pipePath);

    /**
     * @brief Disconnect
     */
    void disconnect();

    /**
     * @brief Send a request and wait for a response
     * @param command Command type
     * @param payload Request payload
     * @param timeoutMs Timeout in milliseconds (0 = wait indefinitely)
     * @return Response data, or error on failure
     */
    Result<nlohmann::json> request(CommandType command,
                                    const nlohmann::json& payload = {},
                                    u32 timeoutMs = 5000);

    /**
     * @brief Check if connected
     */
    [[nodiscard]] bool isConnected() const;

    /**
     * @brief Set event callback
     */
    void onEvent(std::function<void(const IpcEvent&)> callback);

    /**
     * @brief Set disconnect callback (called when connection to daemon is lost)
     */
    void onDisconnect(std::function<void()> callback);

    /**
     * @brief Check if the Daemon is running
     * @param pipePath Named Pipe path
     */
    static bool isDaemonRunning(const std::string& pipePath);

private:
    /**
     * @brief Read thread
     */
    void _readThreadFunc();

    /**
     * @brief Handle received data
     */
    void _handleData(const std::string& data);

    /**
     * @brief Send raw data
     */
    void _sendRaw(const std::string& data);

    HANDLE m_hPipe{INVALID_HANDLE_VALUE};
    std::string m_lineBuffer;

    // Request-response correlation
    struct PendingRequest {
        std::promise<nlohmann::json> promise;
    };
    mutable std::mutex m_pendingMutex;
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>> m_pendingRequests;

    // Event callback
    std::function<void(const IpcEvent&)> m_onEvent;

    // Disconnect callback
    std::function<void()> m_onDisconnect;

    // Read thread
    std::thread m_readThread;
    volatile bool m_running{false};
};

} // namespace th::ipc
