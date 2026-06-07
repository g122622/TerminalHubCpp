#include "terminalhub/IPC/IpcServer.hpp"
#include "terminalhub/Core/Logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <thread>

namespace th::ipc {

// 完成端口自定义键值
static constexpr ULONG_PTR COMPLETION_KEY_SHUTDOWN = 0;
static constexpr ULONG_PTR COMPLETION_KEY_IO       = 1;

// ============================================================
// 构造/析构
// ============================================================

IpcServer::IpcServer(const std::string& pipeName)
    : m_pipeName(pipeName) {
    // 确保 Windows Named Pipe 格式
    if (m_pipeName.find("\\\\") == std::string::npos) {
        m_pipeName = "\\\\.\\pipe\\" + m_pipeName;
    }
}

IpcServer::~IpcServer() {
    stop();
}

// ============================================================
// 命令注册
// ============================================================

void IpcServer::onCommand(CommandType command, CommandHandler handler) {
    m_handlers[command] = std::move(handler);
}

// ============================================================
// 启动
// ============================================================

bool IpcServer::start() {
    if (m_running) {
        return true;
    }

    // 创建停止事件
    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_hStopEvent) {
        Logger::error("创建停止事件失败: " + std::to_string(GetLastError()));
        return false;
    }

    // 创建 IOCP
    m_hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!m_hCompletionPort) {
        Logger::error("创建 IOCP 失败: " + std::to_string(GetLastError()));
        CloseHandle(m_hStopEvent);
        m_hStopEvent = nullptr;
        return false;
    }

    m_running = true;

    // 启动工作线程（数量 = CPU 核心数）
    DWORD threadCount = static_cast<DWORD>(std::max<unsigned int>(1u, std::thread::hardware_concurrency()));
    for (DWORD i = 0; i < threadCount; i++) {
        m_workers.emplace_back([this]() { _workerThread(); });
    }

    // 创建初始的 pipe 实例来接受连接
    _acceptConnection();

    Logger::info("IPC 服务器已启动: " + m_pipeName);
    return true;
}

// ============================================================
// 停止
// ============================================================

void IpcServer::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;

    // 通知工作线程退出
    if (m_hCompletionPort) {
        for (size_t i = 0; i < m_workers.size(); i++) {
            PostQueuedCompletionStatus(m_hCompletionPort, 0,
                                        COMPLETION_KEY_SHUTDOWN, nullptr);
        }
    }

    if (m_hStopEvent) {
        SetEvent(m_hStopEvent);
    }

    // 等待工作线程
    for (auto& t : m_workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_workers.clear();

    // 关闭所有客户端
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& [id, ctx] : m_clients) {
            if (ctx->hPipe != INVALID_HANDLE_VALUE) {
                DisconnectNamedPipe(ctx->hPipe);
                CloseHandle(ctx->hPipe);
            }
        }
        m_clients.clear();
    }

    if (m_hCompletionPort) {
        CloseHandle(m_hCompletionPort);
        m_hCompletionPort = nullptr;
    }

    if (m_hStopEvent) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = nullptr;
    }

    Logger::info("IPC 服务器已停止");
}

// ============================================================
// 广播/发送
// ============================================================

void IpcServer::broadcast(EventType eventType, const nlohmann::json& data,
                          const std::string& sessionId) {
    IpcEvent event;
    event.eventType = eventType;
    event.sessionId = sessionId;
    event.data = data;

    std::string json = event.serialize() + "\n";

    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (auto& [id, ctx] : m_clients) {
        if (ctx->connected) {
            DWORD written = 0;
            WriteFile(ctx->hPipe, json.c_str(), static_cast<DWORD>(json.size()),
                      &written, nullptr);
        }
    }
}

void IpcServer::sendToClient(u64 clientId, EventType eventType,
                              const nlohmann::json& data,
                              const std::string& sessionId) {
    IpcEvent event;
    event.eventType = eventType;
    event.sessionId = sessionId;
    event.data = data;

    std::string json = event.serialize() + "\n";
    _sendRaw(clientId, json);
}

void IpcServer::_sendRaw(u64 clientId, const std::string& data) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto it = m_clients.find(clientId);
    if (it != m_clients.end() && it->second->connected) {
        DWORD written = 0;
        WriteFile(it->second->hPipe, data.c_str(), static_cast<DWORD>(data.size()),
                  &written, nullptr);
    }
}

// ============================================================
// 客户端信息
// ============================================================

size_t IpcServer::getClientCount() const {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    return m_clients.size();
}

void IpcServer::onClientConnect(std::function<void(u64)> callback) {
    m_onClientConnect = std::move(callback);
}

void IpcServer::onClientDisconnect(std::function<void(u64)> callback) {
    m_onClientDisconnect = std::move(callback);
}

// ============================================================
// IOCP 工作线程
// ============================================================

void IpcServer::_workerThread() {
    while (m_running) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(
            m_hCompletionPort, &bytesTransferred,
            &completionKey, &overlapped, 500);

        if (!m_running) {
            break;
        }

        if (completionKey == COMPLETION_KEY_SHUTDOWN) {
            break;
        }

        if (!ok) {
            if (overlapped) {
                // IO 失败 → 客户端断开
                // 找到对应的客户端 ID
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                    if (&it->second->overlapped == overlapped) {
                        u64 clientId = it->first;
                        it->second->connected = false;
                        m_clients.erase(it);
                        if (m_onClientDisconnect) {
                            m_onClientDisconnect(clientId);
                        }
                        break;
                    }
                }
            }
            continue;
        }

        if (bytesTransferred == 0 || !overlapped) {
            continue;
        }

        // 找到对应的客户端
        u64 clientId = 0;
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            for (auto& [id, ctx] : m_clients) {
                if (&ctx->overlapped == overlapped) {
                    clientId = id;
                    break;
                }
            }
        }

        if (clientId == 0) {
            continue;
        }

        // 处理读取到的数据
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            auto it = m_clients.find(clientId);
            if (it == m_clients.end()) {
                continue;
            }
            auto& ctx = it->second;
            std::string data(ctx->readBuffer, bytesTransferred);
            ctx->lineBuffer += data;
        }

        _handleData(clientId, "");

        // 继续投递读操作
        _postRead(clientId);
    }
}

// ============================================================
// 接受连接
// ============================================================

void IpcServer::_acceptConnection() {
    // 创建 Named Pipe 实例
    std::wstring wPipeName(m_pipeName.begin(), m_pipeName.end());

    HANDLE hPipe = CreateNamedPipeW(
        wPipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        65536,  // 输出缓冲区大小
        65536,  // 输入缓冲区大小
        0,      // 默认超时
        nullptr);

    if (hPipe == INVALID_HANDLE_VALUE) {
        Logger::error("CreateNamedPipe 失败: " + std::to_string(GetLastError()));
        return;
    }

    // 关联到 IOCP
    HANDLE hPort = CreateIoCompletionPort(hPipe, m_hCompletionPort,
                                           COMPLETION_KEY_IO, 0);
    if (!hPort) {
        Logger::error("关联 IOCP 失败: " + std::to_string(GetLastError()));
        CloseHandle(hPipe);
        return;
    }

    // 创建客户端上下文
    u64 clientId = m_nextClientId++;
    auto ctx = std::make_unique<ClientContext>();
    ctx->hPipe = hPipe;
    ctx->connected = true;

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clients[clientId] = std::move(ctx);
    }

    // 异步等待连接
    OVERLAPPED* overlapped = &m_clients[clientId]->overlapped;
    memset(overlapped, 0, sizeof(OVERLAPPED));

    BOOL connected = ConnectNamedPipe(hPipe, overlapped);
    DWORD lastError = GetLastError();

    if (!connected) {
        if (lastError == ERROR_IO_PENDING) {
            // 正在等待连接，IOCP 会在完成时通知
        } else if (lastError == ERROR_PIPE_CONNECTED) {
            // 客户端已经连接
            // 投递读操作
            _postRead(clientId);
        } else {
            Logger::error("ConnectNamedPipe 失败: " + std::to_string(lastError));
            _cleanupClient(clientId);
            return;
        }
    }

    // 通知连接
    if (m_onClientConnect) {
        m_onClientConnect(clientId);
    }

    // 继续接受下一个连接
    if (m_running) {
        _acceptConnection();
    }
}

// ============================================================
// 投递异步读
// ============================================================

void IpcServer::_postRead(u64 clientId) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto it = m_clients.find(clientId);
    if (it == m_clients.end() || !it->second->connected) {
        return;
    }

    auto& ctx = it->second;
    memset(&ctx->overlapped, 0, sizeof(OVERLAPPED));

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(ctx->hPipe, ctx->readBuffer, sizeof(ctx->readBuffer) - 1,
                       &bytesRead, &ctx->overlapped);

    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            ctx->connected = false;
        }
    }
}

// ============================================================
// 处理接收数据
// ============================================================

void IpcServer::_handleData(u64 clientId, const std::string& /*data*/) {
    std::string lineBuffer;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clients.find(clientId);
        if (it == m_clients.end()) {
            return;
        }
        lineBuffer = it->second->lineBuffer;
        it->second->lineBuffer.clear();
    }

    // 按 \n 分帧
    size_t start = 0;
    size_t pos = 0;
    while ((pos = lineBuffer.find('\n', start)) != std::string::npos) {
        std::string line = lineBuffer.substr(start, pos - start);
        start = pos + 1;

        if (line.empty()) {
            continue;
        }

        auto req = IpcRequest::deserialize(line);
        if (req) {
            _handleRequest(clientId, *req);
        }
    }

    // 保留不完整的行
    if (start < lineBuffer.size()) {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clients.find(clientId);
        if (it != m_clients.end()) {
            it->second->lineBuffer = lineBuffer.substr(start);
        }
    }
}

// ============================================================
// 处理请求
// ============================================================

void IpcServer::_handleRequest(u64 clientId, const IpcRequest& request) {
    IpcResponse response;
    response.id = request.id;

    auto it = m_handlers.find(request.command);
    if (it == m_handlers.end()) {
        response.success = false;
        response.error = "未知命令: " + commandTypeToString(request.command);
    } else {
        try {
            auto result = it->second(request.payload, clientId);
            response.success = true;
            response.data = result;
        } catch (const std::exception& e) {
            response.success = false;
            response.error = e.what();
        }
    }

    std::string json = response.serialize() + "\n";
    _sendRaw(clientId, json);
}

// ============================================================
// 清理客户端
// ============================================================

void IpcServer::_cleanupClient(u64 clientId) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto it = m_clients.find(clientId);
    if (it != m_clients.end()) {
        if (it->second->hPipe != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(it->second->hPipe);
            CloseHandle(it->second->hPipe);
        }
        m_clients.erase(it);
    }
}

} // namespace th::ipc
