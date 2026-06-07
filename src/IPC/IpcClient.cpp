#include "terminalhub/IPC/IpcClient.hpp"
#include "terminalhub/Core/Logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <future>

namespace th::ipc {

// ============================================================
// Construction / Destruction
// ============================================================

IpcClient::IpcClient() = default;

IpcClient::~IpcClient() {
    disconnect();
}

// ============================================================
// Connect
// ============================================================

Result<void> IpcClient::connect(const std::string& pipePath) {
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        return Result<void>::err(Error::ipcError("Already connected", "IpcClient::connect"));
    }

    // Ensure Windows Named Pipe format
    std::string fullPath = pipePath;
    if (fullPath.find("\\\\") == std::string::npos) {
        fullPath = "\\\\.\\pipe\\" + fullPath;
    }

    std::wstring wPipeName(fullPath.begin(), fullPath.end());

    // Try to open Named Pipe
    HANDLE hPipe = CreateFileW(
        wPipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (hPipe == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return Result<void>::err(
            Error::ipcError("Failed to connect to daemon: " + std::to_string(err),
                            "IpcClient::connect"));
    }

    // Set pipe mode to byte stream
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);

    m_hPipe = hPipe;
    m_running = true;

    // Start read thread
    m_readThread = std::thread([this]() { _readThreadFunc(); });

    return Result<void>::ok();
}

// ============================================================
// Disconnect
// ============================================================

void IpcClient::disconnect() {
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        return;
    }

    m_running = false;

    // Cancel IO to let ReadFile exit
    CancelIoEx(m_hPipe, nullptr);

    if (m_readThread.joinable()) {
        m_readThread.join();
    }

    CloseHandle(m_hPipe);
    m_hPipe = INVALID_HANDLE_VALUE;

    // Reject all pending requests
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        for (auto& [id, pending] : m_pendingRequests) {
            try {
                pending->promise.set_value(nlohmann::json());
            } catch (...) {}
        }
        m_pendingRequests.clear();
    }
}

// ============================================================
// Send request
// ============================================================

Result<nlohmann::json> IpcClient::request(CommandType command,
                                            const nlohmann::json& payload,
                                            u32 timeoutMs) {
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        return Result<nlohmann::json>::err(
            Error::ipcError("Not connected to daemon", "IpcClient::request"));
    }

    std::string id = generateRequestId();

    IpcRequest req;
    req.id = id;
    req.command = command;
    req.payload = payload;

    // Create pending request
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests[id] = pending;
    }

    // Send request
    std::string json = req.serialize() + "\n";
    _sendRaw(json);

    // Wait for response
    auto future = pending->promise.get_future();
    if (timeoutMs == 0) {
        future.wait();
    } else {
        auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
        if (status == std::future_status::timeout) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingRequests.erase(id);
            return Result<nlohmann::json>::err(
                Error::ipcError("Request timed out", "IpcClient::request"));
        }
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.erase(id);
    }

    auto result = future.get();

    // Check for error marker from server
    if (result.is_object() && result.contains("__error")) {
        std::string errorMsg = result["__error"].get<std::string>();
        return Result<nlohmann::json>::err(
            Error::ipcError(errorMsg, "IpcClient::request"));
    }

    return Result<nlohmann::json>::ok(std::move(result));
}

// ============================================================
// Connection state
// ============================================================

bool IpcClient::isConnected() const {
    return m_hPipe != INVALID_HANDLE_VALUE;
}

void IpcClient::onEvent(std::function<void(const IpcEvent&)> callback) {
    m_onEvent = std::move(callback);
}

void IpcClient::onDisconnect(std::function<void()> callback) {
    m_onDisconnect = std::move(callback);
}

// ============================================================
// Static: check if daemon is running
// ============================================================

bool IpcClient::isDaemonRunning(const std::string& pipePath) {
    std::string fullPath = pipePath;
    if (fullPath.find("\\\\") == std::string::npos) {
        fullPath = "\\\\.\\pipe\\" + fullPath;
    }

    std::wstring wPipeName(fullPath.begin(), fullPath.end());

    // WaitNamedPipe 只检查管道是否存在且可连接，不会实际建立连接
    if (WaitNamedPipeW(wPipeName.c_str(), 0)) {
        return true;
    }

    // 如果 WaitNamedPipe 返回 FALSE 但错误码是 ERROR_PIPE_BUSY，
    // 说明管道存在但正忙（守护进程在运行）
    DWORD err = GetLastError();
    if (err == ERROR_PIPE_BUSY) {
        return true;
    }

    return false;
}

// ============================================================
// Read thread
// ============================================================

void IpcClient::_readThreadFunc() {
    constexpr DWORD BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];

    while (m_running) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) {
            break;
        }

        BOOL ok = ReadFile(m_hPipe, buffer, BUFFER_SIZE - 1,
                           nullptr, &overlapped);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait for completion
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 500);
                if (waitResult == WAIT_OBJECT_0) {
                    DWORD bytesRead = 0;
                    ok = GetOverlappedResult(m_hPipe, &overlapped, &bytesRead, FALSE);
                    if (ok && bytesRead > 0) {
                        buffer[bytesRead] = '\0';
                        _handleData(std::string(buffer, bytesRead));
                    } else if (!ok || bytesRead == 0) {
                        CloseHandle(overlapped.hEvent);
                        break;
                    }
                } else if (waitResult == WAIT_TIMEOUT) {
                    CloseHandle(overlapped.hEvent);
                    continue;
                } else {
                    CloseHandle(overlapped.hEvent);
                    break;
                }
            } else {
                CloseHandle(overlapped.hEvent);
                break;
            }
        } else {
            // ReadFile completed synchronously
            DWORD bytesRead = 0;
            GetOverlappedResult(m_hPipe, &overlapped, &bytesRead, FALSE);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                _handleData(std::string(buffer, bytesRead));
            } else {
                CloseHandle(overlapped.hEvent);
                break;
            }
        }

        CloseHandle(overlapped.hEvent);
    }

    m_running = false;

    // Notify disconnect callback if not explicitly disconnecting
    if (m_hPipe != INVALID_HANDLE_VALUE && m_onDisconnect) {
        m_onDisconnect();
    }
}

// ============================================================
// Process received data
// ============================================================

void IpcClient::_handleData(const std::string& data) {
    m_lineBuffer += data;

    size_t start = 0;
    size_t pos = 0;
    while ((pos = m_lineBuffer.find('\n', start)) != std::string::npos) {
        std::string line = m_lineBuffer.substr(start, pos - start);
        start = pos + 1;

        if (line.empty()) {
            continue;
        }

        // Try to parse as response
        auto response = IpcResponse::deserialize(line);
        if (response) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            auto it = m_pendingRequests.find(response->id);
            if (it != m_pendingRequests.end()) {
                if (response->success) {
                    it->second->promise.set_value(response->data);
                } else {
                    // Store error as special marker object with __error field
                    nlohmann::json errorData;
                    errorData["__error"] = response->error;
                    it->second->promise.set_value(errorData);
                }
            }
            continue;
        }

        // Try to parse as event
        auto event = IpcEvent::deserialize(line);
        if (event && m_onEvent) {
            m_onEvent(*event);
        }
    }

    // Keep incomplete line
    if (start < m_lineBuffer.size()) {
        m_lineBuffer = m_lineBuffer.substr(start);
    } else {
        m_lineBuffer.clear();
    }
}

// ============================================================
// Send raw data
// ============================================================

void IpcClient::_sendRaw(const std::string& data) {
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        return;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        return;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(m_hPipe, data.c_str(), static_cast<DWORD>(data.size()),
                        nullptr, &overlapped);

    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        WaitForSingleObject(overlapped.hEvent, 5000);
        GetOverlappedResult(m_hPipe, &overlapped, &written, TRUE);
    }

    CloseHandle(overlapped.hEvent);
}

} // namespace th::ipc
