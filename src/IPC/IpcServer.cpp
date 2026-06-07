#include "terminalhub/IPC/IpcServer.hpp"
#include "terminalhub/Core/Logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <thread>

namespace th::ipc {

// IOCP completion port custom keys
static constexpr ULONG_PTR COMPLETION_KEY_SHUTDOWN = 0;
static constexpr ULONG_PTR COMPLETION_KEY_IO       = 1;

// ============================================================
// Construction / Destruction
// ============================================================

IpcServer::IpcServer(const std::string& pipeName)
    : m_pipeName(pipeName) {
    // Ensure Windows Named Pipe format
    if (m_pipeName.find("\\\\") == std::string::npos) {
        m_pipeName = "\\\\.\\pipe\\" + m_pipeName;
    }
}

IpcServer::~IpcServer() {
    stop();
}

// ============================================================
// Command registration
// ============================================================

void IpcServer::onCommand(CommandType command, CommandHandler handler) {
    m_handlers[command] = std::move(handler);
}

// ============================================================
// Start
// ============================================================

bool IpcServer::start() {
    if (m_running) {
        return true;
    }

    // Create stop event
    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_hStopEvent) {
        Logger::error("Failed to create stop event: " + std::to_string(GetLastError()));
        return false;
    }

    // Create IOCP
    m_hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!m_hCompletionPort) {
        Logger::error("Failed to create IOCP: " + std::to_string(GetLastError()));
        CloseHandle(m_hStopEvent);
        m_hStopEvent = nullptr;
        return false;
    }

    m_running = true;

    // Start worker threads
    DWORD threadCount = static_cast<DWORD>(std::max<unsigned int>(1u, std::thread::hardware_concurrency()));
    for (DWORD i = 0; i < threadCount; i++) {
        m_workers.emplace_back([this]() { _workerThread(); });
    }

    // Create initial pipe instance to accept connections
    _acceptConnection();

    Logger::info("IPC server started: " + m_pipeName);
    return true;
}

// ============================================================
// Stop
// ============================================================

void IpcServer::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;

    // Notify worker threads to exit
    if (m_hCompletionPort) {
        for (size_t i = 0; i < m_workers.size(); i++) {
            PostQueuedCompletionStatus(m_hCompletionPort, 0,
                                        COMPLETION_KEY_SHUTDOWN, nullptr);
        }
    }

    if (m_hStopEvent) {
        SetEvent(m_hStopEvent);
    }

    // Wait for worker threads
    for (auto& t : m_workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_workers.clear();

    // Close all clients
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

    Logger::info("IPC server stopped");
}

// ============================================================
// Broadcast / Send
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
        if (ctx->connected && ctx->state == ClientState::Connected) {
            _sendRawLocked(id, json);
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
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        _sendRawLocked(clientId, json);
    }
}

void IpcServer::_sendRawLocked(u64 clientId, const std::string& data) {
    auto it = m_clients.find(clientId);
    if (it == m_clients.end() || !it->second->connected) {
        return;
    }

    auto& ctx = it->second;

    // 初始化写入事件（首次使用时创建）
    if (!ctx->hWriteEvent) {
        ctx->hWriteEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ctx->hWriteEvent) {
            Logger::error("Failed to create write event for client " + std::to_string(clientId));
            return;
        }
    }

    memset(&ctx->writeOverlapped, 0, sizeof(OVERLAPPED));
    ctx->writeOverlapped.hEvent = ctx->hWriteEvent;
    ResetEvent(ctx->hWriteEvent);

    BOOL ok = WriteFile(ctx->hPipe, data.c_str(),
                        static_cast<DWORD>(data.size()),
                        nullptr, &ctx->writeOverlapped);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD written = 0;
            WaitForSingleObject(ctx->hWriteEvent, 5000);
            GetOverlappedResult(ctx->hPipe, &ctx->writeOverlapped, &written, FALSE);
        } else {
            Logger::error("WriteFile failed for client " + std::to_string(clientId) +
                          " err=" + std::to_string(err));
        }
    }
}

// ============================================================
// Client info
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

void IpcServer::onError(std::function<void(const std::string&)> callback) {
    m_onError = std::move(callback);
}

// ============================================================
// IOCP worker thread
// ============================================================

void IpcServer::_workerThread() {
    while (m_running) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(
            m_hCompletionPort, &bytesTransferred,
            &completionKey, &overlapped, INFINITE);

        if (!m_running) {
            break;
        }

        if (completionKey == COMPLETION_KEY_SHUTDOWN) {
            break;
        }

        if (!ok) {
            if (overlapped) {
                // 检查是否是写入完成（忽略错误）
                bool isWriteOverlapped = false;
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    for (auto& [id, ctx] : m_clients) {
                        if (&ctx->writeOverlapped == overlapped) {
                            isWriteOverlapped = true;
                            break;
                        }
                    }
                }
                if (isWriteOverlapped) {
                    continue;
                }

                // 读/连接失败 -> 客户端断开
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

        if (!overlapped) {
            continue;
        }

        // 检查是否是写入完成（忽略，由 _sendRawLocked 处理）
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            for (auto& [id, ctx] : m_clients) {
                if (&ctx->writeOverlapped == overlapped) {
                    goto next_iteration;
                }
            }
        }

        // 查找对应的客户端（通过读/连接 overlapped）
        {
            u64 clientId = 0;
            ClientState clientState = ClientState::Connecting;
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                for (auto& [id, ctx] : m_clients) {
                    if (&ctx->overlapped == overlapped) {
                        clientId = id;
                        clientState = ctx->state;
                        break;
                    }
                }
            }

            if (clientId == 0) {
                continue;
            }

            if (clientState == ClientState::Connecting) {
                // ConnectNamedPipe 完成 — 客户端已连接
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    auto it = m_clients.find(clientId);
                    if (it != m_clients.end()) {
                        it->second->state = ClientState::Connected;
                    }
                }

                if (m_onClientConnect) {
                    m_onClientConnect(clientId);
                }

                // 开始读取
                _postRead(clientId);

                // 接受下一个连接
                if (m_running) {
                    _acceptConnection();
                }
            } else {
                // ReadFile 完成
                if (bytesTransferred == 0) {
                    // 客户端断开
                    {
                        std::lock_guard<std::mutex> lock(m_clientsMutex);
                        auto it = m_clients.find(clientId);
                        if (it != m_clients.end()) {
                            it->second->connected = false;
                            m_clients.erase(it);
                        }
                    }
                    if (m_onClientDisconnect) {
                        m_onClientDisconnect(clientId);
                    }
                    continue;
                }

                // 处理接收到的数据
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

                // 发起下一次读取
                _postRead(clientId);
            }
        }

        next_iteration:;
    }
}

// ============================================================
// Accept connections
// ============================================================

void IpcServer::_acceptConnection() {
    // Create Named Pipe instance
    std::wstring wPipeName(m_pipeName.begin(), m_pipeName.end());

    HANDLE hPipe = CreateNamedPipeW(
        wPipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        65536,  // Output buffer size
        65536,  // Input buffer size
        0,      // Default timeout
        nullptr);

    if (hPipe == INVALID_HANDLE_VALUE) {
        std::string errMsg = "CreateNamedPipe failed: " + std::to_string(GetLastError());
        Logger::error(errMsg);
        if (m_onError) {
            m_onError(errMsg);
        }
        return;
    }

    // Associate with IOCP
    HANDLE hPort = CreateIoCompletionPort(hPipe, m_hCompletionPort,
                                           COMPLETION_KEY_IO, 0);
    if (!hPort) {
        Logger::error("Failed to associate with IOCP: " + std::to_string(GetLastError()));
        CloseHandle(hPipe);
        return;
    }

    // Create client context
    u64 clientId = m_nextClientId++;
    auto ctx = std::make_unique<ClientContext>();
    ctx->hPipe = hPipe;
    ctx->connected = true;
    ctx->state = ClientState::Connecting;
    ctx->clientId = clientId;

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clients[clientId] = std::move(ctx);
    }

    // Wait for connection asynchronously
    OVERLAPPED* overlapped = &m_clients[clientId]->overlapped;
    memset(overlapped, 0, sizeof(OVERLAPPED));

    BOOL connected = ConnectNamedPipe(hPipe, overlapped);
    DWORD lastError = GetLastError();

    if (!connected) {
        if (lastError == ERROR_IO_PENDING) {
            // Connection pending, IOCP will notify on completion
        } else if (lastError == ERROR_PIPE_CONNECTED) {
            // Client already connected — handle synchronously
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                auto it = m_clients.find(clientId);
                if (it != m_clients.end()) {
                    it->second->state = ClientState::Connected;
                }
            }
            if (m_onClientConnect) {
                m_onClientConnect(clientId);
            }
            _postRead(clientId);
            // Accept next connection
            if (m_running) {
                _acceptConnection();
            }
        } else {
            std::string errMsg = "ConnectNamedPipe failed: " + std::to_string(lastError);
            Logger::error(errMsg);
            if (m_onError) {
                m_onError(errMsg);
            }
            _cleanupClient(clientId);
            return;
        }
    }
}

// ============================================================
// Post async read
// ============================================================

void IpcServer::_postRead(u64 clientId) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto it = m_clients.find(clientId);
    if (it == m_clients.end() || !it->second->connected) {
        return;
    }

    auto& ctx = it->second;
    memset(&ctx->overlapped, 0, sizeof(OVERLAPPED));

    BOOL ok = ReadFile(ctx->hPipe, ctx->readBuffer, sizeof(ctx->readBuffer) - 1,
                       nullptr, &ctx->overlapped);

    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            ctx->connected = false;
        }
    }
}

// ============================================================
// Process received data
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

    // Frame by \n
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
        } else {
            Logger::warn("Failed to parse IPC request from client " + std::to_string(clientId));
            if (m_onError) {
                m_onError("Failed to parse IPC request from client " + std::to_string(clientId));
            }
        }
    }

    // Keep incomplete line
    if (start < lineBuffer.size()) {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clients.find(clientId);
        if (it != m_clients.end()) {
            it->second->lineBuffer = lineBuffer.substr(start);
        }
    }
}

// ============================================================
// Handle request
// ============================================================

void IpcServer::_handleRequest(u64 clientId, const IpcRequest& request) {
    IpcResponse response;
    response.id = request.id;

    auto it = m_handlers.find(request.command);
    if (it == m_handlers.end()) {
        response.success = false;
        response.error = "Unknown command: " + commandTypeToString(request.command);
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
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        _sendRawLocked(clientId, json);
    }
}

// ============================================================
// Cleanup client
// ============================================================

void IpcServer::_cleanupClient(u64 clientId) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto it = m_clients.find(clientId);
    if (it != m_clients.end()) {
        if (it->second->hPipe != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(it->second->hPipe);
            CloseHandle(it->second->hPipe);
        }
        if (it->second->hWriteEvent) {
            CloseHandle(it->second->hWriteEvent);
        }
        m_clients.erase(it);
    }
}

} // namespace th::ipc
