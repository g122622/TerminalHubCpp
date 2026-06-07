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
// 构造/析构
// ============================================================

IpcClient::IpcClient() = default;

IpcClient::~IpcClient() {
    disconnect();
}

// ============================================================
// 连接
// ============================================================

Result<void> IpcClient::connect(const std::string& pipePath) {
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        return Result<void>::err(Error::ipcError("已连接", "IpcClient::connect"));
    }

    // 确保 Windows Named Pipe 格式
    std::string fullPath = pipePath;
    if (fullPath.find("\\\\") == std::string::npos) {
        fullPath = "\\\\.\\pipe\\" + fullPath;
    }

    std::wstring wPipeName(fullPath.begin(), fullPath.end());

    // 尝试连接 Named Pipe
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
            Error::ipcError("无法连接到 Daemon: " + std::to_string(err),
                            "IpcClient::connect"));
    }

    // 设置管道模式为字节流
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);

    m_hPipe = hPipe;
    m_running = true;

    // 启动读取线程
    m_readThread = std::thread([this]() { _readThreadFunc(); });

    return Result<void>::ok();
}

// ============================================================
// 断开连接
// ============================================================

void IpcClient::disconnect() {
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        return;
    }

    m_running = false;

    // 取消 IO 以让 ReadFile 退出
    CancelIoEx(m_hPipe, nullptr);

    if (m_readThread.joinable()) {
        m_readThread.join();
    }

    CloseHandle(m_hPipe);
    m_hPipe = INVALID_HANDLE_VALUE;

    // 拒绝所有等待中的请求
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
// 发送请求
// ============================================================

Result<nlohmann::json> IpcClient::request(CommandType command,
                                            const nlohmann::json& payload,
                                            u32 timeoutMs) {
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        return Result<nlohmann::json>::err(
            Error::ipcError("未连接到 Daemon", "IpcClient::request"));
    }

    std::string id = generateRequestId();

    IpcRequest req;
    req.id = id;
    req.command = command;
    req.payload = payload;

    // 创建等待中的请求
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests[id] = pending;
    }

    // 发送请求
    std::string json = req.serialize() + "\n";
    _sendRaw(json);

    // 等待响应
    auto future = pending->promise.get_future();
    if (timeoutMs == 0) {
        future.wait();
    } else {
        auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
        if (status == std::future_status::timeout) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingRequests.erase(id);
            return Result<nlohmann::json>::err(
                Error::ipcError("请求超时", "IpcClient::request"));
        }
    }

    // 清理
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.erase(id);
    }

    auto result = future.get();
    return Result<nlohmann::json>::ok(std::move(result));
}

// ============================================================
// 连接状态
// ============================================================

bool IpcClient::isConnected() const {
    return m_hPipe != INVALID_HANDLE_VALUE;
}

void IpcClient::onEvent(std::function<void(const IpcEvent&)> callback) {
    m_onEvent = std::move(callback);
}

// ============================================================
// 静态方法：检测 Daemon 是否运行
// ============================================================

bool IpcClient::isDaemonRunning(const std::string& pipePath) {
    std::string fullPath = pipePath;
    if (fullPath.find("\\\\") == std::string::npos) {
        fullPath = "\\\\.\\pipe\\" + fullPath;
    }

    std::wstring wPipeName(fullPath.begin(), fullPath.end());

    HANDLE hPipe = CreateFileW(
        wPipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        return true;
    }

    return false;
}

// ============================================================
// 读取线程
// ============================================================

void IpcClient::_readThreadFunc() {
    constexpr DWORD BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (m_running) {
        DWORD bytesRead = 0;
        memset(&overlapped, 0, sizeof(OVERLAPPED));
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        BOOL ok = ReadFile(m_hPipe, buffer, BUFFER_SIZE - 1, &bytesRead, &overlapped);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // 等待完成
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 500);
                if (waitResult == WAIT_OBJECT_0) {
                    ok = GetOverlappedResult(m_hPipe, &overlapped, &bytesRead, FALSE);
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
        }

        CloseHandle(overlapped.hEvent);

        if (!ok || bytesRead == 0) {
            break;
        }

        buffer[bytesRead] = '\0';
        _handleData(std::string(buffer, bytesRead));
    }

    m_running = false;
}

// ============================================================
// 处理接收数据
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

        // 尝试解析为响应
        auto response = IpcResponse::deserialize(line);
        if (response) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            auto it = m_pendingRequests.find(response->id);
            if (it != m_pendingRequests.end()) {
                if (response->success) {
                    it->second->promise.set_value(response->data);
                } else {
                    // 失败时设置空 JSON，错误信息通过 Result 传递
                    nlohmann::json errorData;
                    errorData["_error"] = response->error;
                    it->second->promise.set_value(errorData);
                }
            }
            continue;
        }

        // 尝试解析为事件
        auto event = IpcEvent::deserialize(line);
        if (event && m_onEvent) {
            m_onEvent(*event);
        }
    }

    // 保留不完整的行
    if (start < m_lineBuffer.size()) {
        m_lineBuffer = m_lineBuffer.substr(start);
    } else {
        m_lineBuffer.clear();
    }
}

// ============================================================
// 发送原始数据
// ============================================================

void IpcClient::_sendRaw(const std::string& data) {
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(m_hPipe, data.c_str(), static_cast<DWORD>(data.size()),
              &written, nullptr);
}

} // namespace th::ipc
