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

namespace th {

// 前向声明
class ConPty;

/**
 * @brief 会话元数据
 */
struct SessionMetadata {
    std::string id;              // th_{timestamp}_{random}
    std::string title;
    std::string shell;
    std::string cwd;
    DWORD pid = 0;
    i64 createdAt = 0;           // Unix 毫秒
    i64 lastActivityAt = 0;
    i32 connectedClients = 0;
};

/**
 * @brief 会话列表项（用于 th list 显示）
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
 * @brief 会话实体
 *
 * 管理一个 PTY 会话，包含输出缓冲区、客户端连接和事件通知。
 */
class Session {
public:
    Session(SessionMetadata metadata, i32 outputBufferSize);

    // 客户端管理
    void addClient(u64 clientId);
    void removeClient(u64 clientId);
    [[nodiscard]] const std::unordered_set<u64>& clients() const;

    /**
     * @brief 更新最后活动时间
     */
    void touch();

    /**
     * @brief 检查 PTY 进程是否存活
     */
    [[nodiscard]] bool isAlive() const;

    // 事件通知
    void broadcastOutput(const std::string& data);
    void broadcastExit(u32 exitCode);

    // 回调注册
    void onOutput(std::function<void(const std::string&)> callback);
    void onExit(std::function<void(u32)> callback);

    // 数据成员
    SessionMetadata metadata;
    OutputBuffer outputBuffer;
    std::unique_ptr<ConPty> ptyProcess;

private:
    std::unordered_set<u64> m_clients;
    std::function<void(const std::string&)> m_onOutput;
    std::function<void(u32)> m_onExit;
};

} // namespace th
