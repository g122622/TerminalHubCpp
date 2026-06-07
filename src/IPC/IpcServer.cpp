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

    // Start worker threads (count = CPU cores)
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
            &completionKey, &overlapped, 500);

        if (!m_running) {
            break;
        }

        if (completionKey == COMPLETION_KEY_SHUTDOWN) {
            break;
        }

        if (!ok) {
            if (overlapped) {
                // IO failure -> client disconnected
                // Find the corresponding client ID
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

        // Find the corresponding client
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

        // Process received data
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

        // Post next read
        _postRead(clientId);
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
            // Client already connected
            // Post read operation
            _postRead(clientId);
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

    // Notify connection
    if (m_onClientConnect) {
        m_onClientConnect(clientId);
    }

    // Continue accepting next connection
    if (m_running) {
        _acceptConnection();
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
    _sendRaw(clientId, json);
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
        m_clients.erase(it);
    }
}

} // namespace th::ipc
