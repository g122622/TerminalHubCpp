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
 * @brief 命令处理器类型
 *
 * @param payload 请求 payload（JSON 对象）
 * @param clientId 客户端标识（用于 sendToClient）
 * @return 响应数据（JSON），或抛出异常时由服务器构造错误响应
 */
using CommandHandler = std::function<nlohmann::json(const nlohmann::json& payload, u64 clientId)>;

/**
 * @brief IOCP Named Pipe 服务器
 *
 * 运行在 Daemon 进程中，处理 CLI 客户端请求。
 * 使用换行符分隔的 JSON 协议。
 */
class IpcServer {
public:
    explicit IpcServer(const std::string& pipeName);
    ~IpcServer();

    // 禁止拷贝
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    /**
     * @brief 注册命令处理器
     */
    void onCommand(CommandType command, CommandHandler handler);

    /**
     * @brief 启动服务器
     */
    bool start();

    /**
     * @brief 停止服务器
     */
    void stop();

    /**
     * @brief 向所有客户端广播事件
     */
    void broadcast(EventType eventType, const nlohmann::json& data,
                   const std::string& sessionId = "");

    /**
     * @brief 向特定客户端发送事件
     */
    void sendToClient(u64 clientId, EventType eventType, const nlohmann::json& data,
                      const std::string& sessionId = "");

    /**
     * @brief 获取连接的客户端数量
     */
    size_t getClientCount() const;

    /**
     * @brief 设置客户端连接回调
     */
    void onClientConnect(std::function<void(u64 clientId)> callback);

    /**
     * @brief 设置客户端断开回调
     */
    void onClientDisconnect(std::function<void(u64 clientId)> callback);

private:
    /**
     * @brief IOCP 工作线程
     */
    void _workerThread();

    /**
     * @brief 接受新连接
     */
    void _acceptConnection();

    /**
     * @brief 处理完成端口通知
     */
    void _handleCompletion(DWORD bytesTransferred, ULONG_PTR completionKey,
                           OVERLAPPED* overlapped);

    /**
     * @brief 投递异步读操作
     */
    void _postRead(u64 clientId);

    /**
     * @brief 处理接收到的数据（按 \n 分帧）
     */
    void _handleData(u64 clientId, const std::string& data);

    /**
     * @brief 处理请求
     */
    void _handleRequest(u64 clientId, const IpcRequest& request);

    /**
     * @brief 发送原始数据到客户端
     */
    void _sendRaw(u64 clientId, const std::string& data);

    /**
     * @brief 清理客户端资源
     */
    void _cleanupClient(u64 clientId);

    std::string m_pipeName;  // Named Pipe 名称（如 \\.\pipe\terminalhub）

    // IOCP
    HANDLE m_hCompletionPort{nullptr};
    HANDLE m_hStopEvent{nullptr};

    // 客户端信息
    struct ClientContext {
        HANDLE hPipe{INVALID_HANDLE_VALUE};
        OVERLAPPED overlapped{};
        char readBuffer[4096]{};
        std::string lineBuffer;  // 不完整行的缓冲
        bool connected{false};
    };

    mutable std::mutex m_clientsMutex;
    std::unordered_map<u64, std::unique_ptr<ClientContext>> m_clients;
    u64 m_nextClientId{1};

    // 命令处理器
    std::unordered_map<CommandType, CommandHandler> m_handlers;

    // 回调
    std::function<void(u64)> m_onClientConnect;
    std::function<void(u64)> m_onClientDisconnect;

    // 工作线程
    std::vector<std::thread> m_workers;
    volatile bool m_running{false};
};

} // namespace th::ipc
