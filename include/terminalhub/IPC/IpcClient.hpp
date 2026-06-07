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
 * @brief Named Pipe IPC 客户端
 *
 * 用于 CLI 与 Daemon 进程通信。
 * 使用换行符分隔的 JSON 协议。
 */
class IpcClient {
public:
    IpcClient();
    ~IpcClient();

    // 禁止拷贝
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    /**
     * @brief 连接到 Daemon
     * @param pipePath Named Pipe 路径（如 \\.\pipe\terminalhub）
     */
    Result<void> connect(const std::string& pipePath);

    /**
     * @brief 断开连接
     */
    void disconnect();

    /**
     * @brief 发送请求并等待响应
     * @param command 命令类型
     * @param payload 请求 payload
     * @param timeoutMs 超时毫秒数（0=无限等待）
     * @return 响应数据，失败返回错误
     */
    Result<nlohmann::json> request(CommandType command,
                                    const nlohmann::json& payload = {},
                                    u32 timeoutMs = 5000);

    /**
     * @brief 检查是否已连接
     */
    [[nodiscard]] bool isConnected() const;

    /**
     * @brief 设置事件回调
     */
    void onEvent(std::function<void(const IpcEvent&)> callback);

    /**
     * @brief 检查 Daemon 是否运行
     * @param pipePath Named Pipe 路径
     */
    static bool isDaemonRunning(const std::string& pipePath);

private:
    /**
     * @brief 读取线程
     */
    void _readThreadFunc();

    /**
     * @brief 处理接收到的数据
     */
    void _handleData(const std::string& data);

    /**
     * @brief 发送原始数据
     */
    void _sendRaw(const std::string& data);

    HANDLE m_hPipe{INVALID_HANDLE_VALUE};
    std::string m_lineBuffer;

    // 请求-响应关联
    struct PendingRequest {
        std::promise<nlohmann::json> promise;
    };
    mutable std::mutex m_pendingMutex;
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>> m_pendingRequests;

    // 事件回调
    std::function<void(const IpcEvent&)> m_onEvent;

    // 读取线程
    std::thread m_readThread;
    volatile bool m_running{false};
};

} // namespace th::ipc
