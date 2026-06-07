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
// Helper functions
// ============================================================

std::string getDefaultShell(const std::string& configShell) {
    if (configShell == "cmd") {
        return "cmd.exe";
    }
    if (configShell == "bash") {
        return "bash";
    }
    // Default to powershell
    return "powershell.exe";
}

// ============================================================
// ConPty implementation
// ============================================================

Result<std::unique_ptr<ConPty>> ConPty::create(const ConPtyOptions& options) {
    // Validate arguments
    if (options.shell.empty()) {
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("Shell path cannot be empty", "ConPty::create"));
    }
    if (options.cols <= 0 || options.rows <= 0) {
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("Terminal dimensions must be positive", "ConPty::create"));
    }

    auto pty = std::unique_ptr<ConPty>(new ConPty());
    pty->m_cols = options.cols;
    pty->m_rows = options.rows;

    // Create pipe pairs
    // hPipePTYIn  -> ConPTY read side (host writes via hPipeIn)
    // hPipePTYOut -> ConPTY write side (host reads via hPipeOut)
    HANDLE hPipePTYIn = INVALID_HANDLE_VALUE;
    HANDLE hPipeIn = INVALID_HANDLE_VALUE;
    HANDLE hPipePTYOut = INVALID_HANDLE_VALUE;
    HANDLE hPipeOut = INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    // Create input pipe (host write -> PTY read)
    if (!CreatePipe(&hPipePTYIn, &hPipeIn, &sa, 0)) {
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("Failed to create input pipe: " + std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // Create output pipe (PTY write -> host read)
    if (!CreatePipe(&hPipeOut, &hPipePTYOut, &sa, 0)) {
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipeIn);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("Failed to create output pipe: " + std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // Create pseudo console
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
            Error::ptyError("CreatePseudoConsole failed: " + std::to_string(hr),
                            "ConPty::create"));
    }

    // ConPTY has copied the pipe handles, close PTY-side handles
    CloseHandle(hPipePTYIn);
    CloseHandle(hPipePTYOut);

    pty->m_hPipeIn = hPipeIn;
    pty->m_hPipeOut = hPipeOut;

    // Prepare STARTUPINFOEX
    STARTUPINFOEXW siEx{};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    // Initialize process thread attribute list
    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

    auto attrListBuffer = std::vector<BYTE>(attrListSize);
    siEx.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrListBuffer.data());

    if (!InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrListSize)) {
        ClosePseudoConsole(pty->m_hPc);
        CloseHandle(pty->m_hPipeIn);
        CloseHandle(pty->m_hPipeOut);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("InitializeProcThreadAttributeList failed: " +
                            std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // Set pseudo console attribute
    if (!UpdateProcThreadAttribute(siEx.lpAttributeList, 0,
                                    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                    pty->m_hPc, sizeof(pty->m_hPc),
                                    nullptr, nullptr)) {
        DeleteProcThreadAttributeList(siEx.lpAttributeList);
        ClosePseudoConsole(pty->m_hPc);
        CloseHandle(pty->m_hPipeIn);
        CloseHandle(pty->m_hPipeOut);
        return Result<std::unique_ptr<ConPty>>::err(
            Error::ptyError("UpdateProcThreadAttribute failed: " +
                            std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    // Build command line
    std::wstring wCmdLine(options.shell.begin(), options.shell.end());

    // Build environment block
    std::wstring envBlock;
    if (!options.env.empty() || true) {
        // Get current process environment, append TERM=xterm-256color
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

        // Append TERM
        envBlock.append(L"TERM=xterm-256color");
        envBlock.push_back(L'\0');

        // Append user-defined environment variables
        for (const auto& [key, value] : options.env) {
            std::wstring entry(key.begin(), key.end());
            entry += L'=';
            entry.append(value.begin(), value.end());
            envBlock.append(entry);
            envBlock.push_back(L'\0');
        }
        envBlock.push_back(L'\0'); // Double null terminator
    }

    // Working directory
    std::wstring wCwd;
    if (!options.cwd.empty()) {
        wCwd = std::wstring(options.cwd.begin(), options.cwd.end());
    }

    // Create process
    PROCESS_INFORMATION pi{};
    DWORD creationFlags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;

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
            Error::ptyError("CreateProcess failed: " + std::to_string(GetLastError()),
                            "ConPty::create"));
    }

    pty->m_hProcess = pi.hProcess;
    pty->m_pid = pi.dwProcessId;
    CloseHandle(pi.hThread);

    Logger::info("ConPTY process started: PID=" + std::to_string(pty->m_pid) +
                 " shell=" + options.shell);

    // Start read thread
    pty->m_running = true;
    pty->m_hReadThread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0,
            [](void* arg) -> unsigned {
                static_cast<ConPty*>(arg)->_readThreadFunc();
                return 0;
            },
            pty.get(), 0, nullptr));

    // Start exit watch thread
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

    // Close write end
    if (m_hPipeIn != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipeIn);
        m_hPipeIn = INVALID_HANDLE_VALUE;
    }

    // Close read end (cancel IO to let ReadFile exit)
    if (m_hPipeOut != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_hPipeOut, nullptr);
        CloseHandle(m_hPipeOut);
        m_hPipeOut = INVALID_HANDLE_VALUE;
    }

    // Wait for threads to exit
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

    // Close pseudo console
    if (m_hPc) {
        ClosePseudoConsole(m_hPc);
        m_hPc = nullptr;
    }

    // Close process handle
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
        Logger::warn("ResizePseudoConsole failed: " + std::to_string(hr));
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
