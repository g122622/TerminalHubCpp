#include "terminalhub/PTY/ConPty.hpp"
#include "terminalhub/Core/Logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <consoleapi.h>

#include <process.h>
#include <vector>

namespace th {

// ============================================================
// 辅助函数
// ============================================================

std::string getDefaultShell(const std::string& configShell) {
    if (configShell == "cmd") {
        return "cmd.exe";
    }
    if (configShell == "bash") {
        return "bash";
    }
    // 默认 powershell
    return "powershell.exe";
}

// ============================================================
// ConPty 实现
// ============================================================

Result<std::unique_ptr<ConPty>> ConPty::create(const ConPtyOptions& options) {
    // 验证参数
    if (options.shell.empty()) {
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("shell 路径不能为空", "ConPty::create"));
    }
    if (options.cols <= 0 || options.rows <= 0) {
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("终端尺寸必须为正整数", "ConPty::create"));
    }

    auto pty = std::unique_ptr<ConPty>(new ConPty());
    pty->m_cols = options.cols;
    pty->m_rows = options.rows;

    // 创建管道对
    // hPipePTYIn  → ConPTY 读取端（宿主通过 hPipeIn 写入）
    // hPipePTYOut → ConPTY 写入端（宿主通过 hPipeOut 读取）
    HANDLE hPipePTYIn = INVALID_HANDLE_VALUE;
    HANDLE hPipeIn = INVALID_HANDLE_VALUE;
    HANDLE hPipePTYOut = INVALID_HANDLE_VALUE;
    HANDLE hPipeOut = INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    // 创建输入管道（宿主写 → PTY 读）
    if (!CreatePipe(&hPipePTYIn, &hPipeIn, &sa, 0)) {
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("创建输入管道失败: " + std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // 创建输出管道（PTY 写 → 宿主读）
    if (!CreatePipe(&hPipeOut, &hPipePTYOut, &sa, 0)) {
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipeIn);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("创建输出管道失败: " + std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // 创建伪控制台
    COORD consoleSize;
    consoleSize.X = static_cast<SHORT>(options.cols);
    consoleSize.Y = static_cast<SHORT>(options.rows);

    HRESULT hr = CreatePseudoConsole(consoleSize, hPipePTYIn, hPipePTYOut, 0, &pty->m_hPc);
    if (FAILED(hr)) {
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipeIn);
        CloseHandle(hPipePTYOut);
        CloseHandle(hPipeOut);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("CreatePseudoConsole 失败: " + std::to_string(hr),
                            "ConPty::create"));
    }

    // ConPTY 已复制管道句柄，关闭 PTY 侧的句柄
    CloseHandle(hPipePTYIn);
    CloseHandle(hPipePTYOut);

    pty->m_hPipeIn = hPipeIn;
    pty->m_hPipeOut = hPipeOut;

    // 准备 STARTUPINFOEX
    STARTUPINFOEXW siEx{};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    // 初始化进程线程属性列表
    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

    auto attrListBuffer = std::vector<BYTE>(attrListSize);
    siEx.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrListBuffer.data());

    if (!InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrListSize)) {
        ClosePseudoConsole(pty->m_hPc);
        CloseHandle(pty->m_hPipeIn);
        CloseHandle(pty->m_hPipeOut);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("InitializeProcThreadAttributeList 失败: " +
                            std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // 设置伪控制台属性
    if (!UpdateProcThreadAttribute(siEx.lpAttributeList, 0,
                                    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                    pty->m_hPc, sizeof(pty->m_hPc),
                                    nullptr, nullptr)) {
        DeleteProcThreadAttributeList(siEx.lpAttributeList);
        ClosePseudoConsole(pty->m_hPc);
        CloseHandle(pty->m_hPipeIn);
        CloseHandle(pty->m_hPipeOut);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("UpdateProcThreadAttribute 失败: " +
                            std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // 构建命令行
    std::wstring wCmdLine(options.shell.begin(), options.shell.end());

    // 构建环境块
    std::wstring envBlock;
    if (!options.env.empty() || true) {
        // 获取当前进程环境块，追加 TERM=xterm-256color
        LPWCH currentEnv = GetEnvironmentStringsW();
        if (currentEnv) {
            LPWCH p = currentEnv;
            while (*p) {
                envBlock.append(p);
                envBlock.push_back(L'\0');
                p += wcslen(p) + 1;
            }
            FreeEnvironmentStringsW(currentEnv);
        }

        // 追加 TERM
        envBlock.append(L"TERM=xterm-256color");
        envBlock.push_back(L'\0');

        // 追加用户自定义环境变量
        for (const auto& [key, value] : options.env) {
            std::wstring entry(key.begin(), key.end());
            entry += L'=';
            entry.append(value.begin(), value.end());
            envBlock.append(entry);
            envBlock.push_back(L'\0');
        }
        envBlock.push_back(L'\0'); // 双 null 终止
    }

    // 工作目录
    std::wstring wCwd;
    if (!options.cwd.empty()) {
        wCwd = std::wstring(options.cwd.begin(), options.cwd.end());
    }

    // 创建进程
    PROCESS_INFORMATION pi{};
    DWORD creationFlags = EXTENDED_STARTUPINFO_PRESENT;

    BOOL success = CreateProcessW(
        nullptr,
        wCmdLine.data(),
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        envBlock.empty() ? nullptr : envBlock.data(),
        wCwd.empty() ? nullptr : wCwd.c_str(),
        &siEx.StartupInfo,
        &pi
    );

    DeleteProcThreadAttributeList(siEx.lpAttributeList);

    if (!success) {
        ClosePseudoConsole(pty->m_hPc);
        CloseHandle(pty->m_hPipeIn);
        CloseHandle(pty->m_hPipeOut);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("CreateProcess 失败: " + std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    pty->m_hProcess = pi.hProcess;
    pty->m_pid = pi.dwProcessId;
    CloseHandle(pi.hThread);

    Logger::info("ConPTY 进程已启动: PID=" + std::to_string(pty->m_pid) +
                 " shell=" + options.shell);

    // 启动读取线程
    pty->m_running = true;
    pty->m_hReadThread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0,
            [](void* arg) -> unsigned {
                static_cast<ConPty*>(arg)->_readThreadFunc();
                return 0;
            },
            pty.get(), 0, nullptr));

    // 启动退出监听线程
    pty->m_hExitThread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0,
            [](void* arg) -> unsigned {
                static_cast<ConPty*>(arg)->_exitWatchThreadFunc();
                return 0;
            },
            pty.get(), 0, nullptr));

    return Result<std::unique_ptr<ConPty>>::ok(std::move(pty));
}

ConPty::~ConPty() {
    m_running = false;

    // 关闭写入端
    if (m_hPipeIn != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipeIn);
        m_hPipeIn = INVALID_HANDLE_VALUE;
    }

    // 关闭读取端（取消 IO 让 ReadFile 退出）
    if (m_hPipeOut != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_hPipeOut, nullptr);
        CloseHandle(m_hPipeOut);
        m_hPipeOut = INVALID_HANDLE_VALUE;
    }

    // 等待线程退出
    if (m_hReadThread) {
        WaitForSingleObject(m_hReadThread, 3000);
        CloseHandle(m_hReadThread);
        m_hReadThread = nullptr;
    }

    if (m_hExitThread) {
        WaitForSingleObject(m_hExitThread, 3000);
        CloseHandle(m_hExitThread);
        m_hExitThread = nullptr;
    }

    // 关闭伪控制台
    if (m_hPc) {
        ClosePseudoConsole(m_hPc);
        m_hPc = nullptr;
    }

    // 关闭进程句柄
    if (m_hProcess) {
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
    }
}

ConPty::ConPty(ConPty&& other) noexcept
    : m_hPc(other.m_hPc)
    , m_hPipeIn(other.m_hPipeIn)
    , m_hPipeOut(other.m_hPipeOut)
    , m_hProcess(other.m_hProcess)
    , m_pid(other.m_pid)
    , m_hReadThread(other.m_hReadThread)
    , m_hExitThread(other.m_hExitThread)
    , m_running(other.m_running)
    , m_onOutput(std::move(other.m_onOutput))
    , m_onExit(std::move(other.m_onExit))
    , m_cols(other.m_cols)
    , m_rows(other.m_rows) {
    other.m_hPc = nullptr;
    other.m_hPipeIn = INVALID_HANDLE_VALUE;
    other.m_hPipeOut = INVALID_HANDLE_VALUE;
    other.m_hProcess = nullptr;
    other.m_pid = 0;
    other.m_hReadThread = nullptr;
    other.m_hExitThread = nullptr;
    other.m_running = false;
}

ConPty& ConPty::operator=(ConPty&& other) noexcept {
    if (this != &other) {
        this->~ConPty();

        m_hPc = other.m_hPc;
        m_hPipeIn = other.m_hPipeIn;
        m_hPipeOut = other.m_hPipeOut;
        m_hProcess = other.m_hProcess;
        m_pid = other.m_pid;
        m_hReadThread = other.m_hReadThread;
        m_hExitThread = other.m_hExitThread;
        m_running = other.m_running;
        m_onOutput = std::move(other.m_onOutput);
        m_onExit = std::move(other.m_onExit);
        m_cols = other.m_cols;
        m_rows = other.m_rows;

        other.m_hPc = nullptr;
        other.m_hPipeIn = INVALID_HANDLE_VALUE;
        other.m_hPipeOut = INVALID_HANDLE_VALUE;
        other.m_hProcess = nullptr;
        other.m_pid = 0;
        other.m_hReadThread = nullptr;
        other.m_hExitThread = nullptr;
        other.m_running = false;
    }
    return *this;
}

void ConPty::write(std::string_view data) {
    if (m_hPipeIn == INVALID_HANDLE_VALUE || data.empty()) {
        return;
    }

    DWORD written = 0;
    WriteFile(m_hPipeIn, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
}

void ConPty::resize(i32 cols, i32 rows) {
    if (!m_hPc || cols <= 0 || rows <= 0) {
        return;
    }

    COORD newSize;
    newSize.X = static_cast<SHORT>(cols);
    newSize.Y = static_cast<SHORT>(rows);

    HRESULT hr = ResizePseudoConsole(m_hPc, newSize);
    if (SUCCEEDED(hr)) {
        m_cols = cols;
        m_rows = rows;
    } else {
        Logger::warn("ResizePseudoConsole 失败: " + std::to_string(hr));
    }
}

void ConPty::kill() {
    if (m_hProcess) {
        TerminateProcess(m_hProcess, 1);
    }
}

DWORD ConPty::pid() const {
    return m_pid;
}

bool ConPty::isAlive() const {
    if (!m_hProcess) {
        return false;
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(m_hProcess, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

void ConPty::onOutput(std::function<void(std::string_view)> callback) {
    m_onOutput = std::move(callback);
}

void ConPty::onExit(std::function<void(u32 exitCode)> callback) {
    m_onExit = std::move(callback);
}

void ConPty::_readThreadFunc() {
    constexpr DWORD BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];

    while (m_running) {
        DWORD bytesRead = 0;
        BOOL success = ReadFile(m_hPipeOut, buffer, BUFFER_SIZE - 1, &bytesRead, nullptr);

        if (!success || bytesRead == 0) {
            break;
        }

        if (m_onOutput) {
            m_onOutput(std::string_view(buffer, bytesRead));
        }
    }
}

void ConPty::_exitWatchThreadFunc() {
    if (!m_hProcess) {
        return;
    }

    WaitForSingleObject(m_hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(m_hProcess, &exitCode);

    if (m_onExit) {
        m_onExit(static_cast<u32>(exitCode));
    }
}

} // namespace th
