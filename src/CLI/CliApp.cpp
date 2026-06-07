#include "terminalhub/CLI/CliApp.hpp"
#include "terminalhub/IPC/IpcClient.hpp"
#include "terminalhub/IPC/IpcMessage.hpp"
#include "terminalhub/Storage/ConfigManager.hpp"
#include "terminalhub/Core/Logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <consoleapi2.h>

#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

namespace th {

using namespace ipc;

// ============================================================
// 辅助函数
// ============================================================

static std::string formatTimestamp(i64 ms) {
    time_t t = static_cast<time_t>(ms / 1000);
    struct tm tm_buf;
    localtime_s(&tm_buf, &t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

static void printInfo(const std::string& msg) {
    std::cout << "\x1b[34m" << "info" << "\x1b[0m" << " " << msg << "\n";
}

static void printSuccess(const std::string& msg) {
    std::cout << "\x1b[32m" << "ok" << "\x1b[0m" << " " << msg << "\n";
}

static void printError(const std::string& msg) {
    std::cerr << "\x1b[31m" << "error" << "\x1b[0m" << " " << msg << "\n";
}

/**
 * @brief 确保守护进程正在运行
 * @return 成功返回 true
 */
static bool ensureDaemonRunning(const Config& config) {
    std::string pipePath = config.daemon.socketPath;

    if (IpcClient::isDaemonRunning(pipePath)) {
        return true;
    }

    // 启动守护进程
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    std::string envStr = "TERMINALHUB_DAEMON=1";

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    PROCESS_INFORMATION pi{};

    // 构建环境块
    LPWCH currentEnv = GetEnvironmentStringsW();
    std::wstring envBlock;
    if (currentEnv) {
        LPWCH p = currentEnv;
        while (*p) {
            envBlock.append(p);
            envBlock.push_back(L'\0');
            p += wcslen(p) + 1;
        }
        FreeEnvironmentStringsW(currentEnv);
    }
    envBlock.append(L"TERMINALHUB_DAEMON=1");
    envBlock.push_back(L'\0');
    envBlock.push_back(L'\0');

    std::wstring wExePath(exePath, exePath + strlen(exePath));

    BOOL ok = CreateProcessW(
        wExePath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT,
        envBlock.data(),
        nullptr,
        &si,
        &pi);

    if (!ok) {
        printError("无法启动守护进程: " + std::to_string(GetLastError()));
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // 等待守护进程就绪（最多 10 次，每次 500ms）
    for (int i = 0; i < 10; i++) {
        Sleep(500);
        if (IpcClient::isDaemonRunning(pipePath)) {
            return true;
        }
    }

    printError("守护进程启动超时");
    return false;
}

// ============================================================
// 命令实现
// ============================================================

static int cmdInit() {
    auto result = ConfigManager::init();
    if (!result.success()) {
        printError(result.error().message());
        return 1;
    }
    printSuccess("配置已初始化");
    return 0;
}

static int cmdNew(const std::string& title, const std::string& shell,
                   const std::string& cwd, i32 cols, i32 rows) {
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        printError(loadResult.error().message());
        printInfo("请先运行 'th init' 初始化配置");
        return 1;
    }

    const Config& config = *configMgr.get();
    if (!ensureDaemonRunning(config)) {
        return 1;
    }

    IpcClient client;
    auto connectResult = client.connect(config.daemon.socketPath);
    if (!connectResult.success()) {
        printError(connectResult.error().message());
        return 1;
    }

    // 构建请求 payload
    nlohmann::json payload;
    if (!title.empty()) payload["title"] = title;
    if (!shell.empty()) payload["shell"] = shell;
    if (!cwd.empty()) payload["cwd"] = cwd;
    if (cols > 0) payload["cols"] = cols;
    if (rows > 0) payload["rows"] = rows;

    auto reqResult = client.request(CommandType::New, payload);
    if (!reqResult.success()) {
        printError(reqResult.error().message());
        return 1;
    }

    auto& data = reqResult.value();
    if (data.contains("_error")) {
        printError(data["_error"].get<std::string>());
        return 1;
    }

    std::string sessionId = data.value("sessionId", "");
    std::string sessionTitle = data.value("title", "");

    printSuccess("创建会话成功: " + sessionId);
    printInfo("标题: " + sessionTitle);
    printInfo("使用 'th attach " + sessionId + "' 连接到此会话");

    client.disconnect();
    return 0;
}

static int cmdList() {
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        printError(loadResult.error().message());
        return 1;
    }

    const Config& config = *configMgr.get();
    if (!ensureDaemonRunning(config)) {
        return 1;
    }

    IpcClient client;
    auto connectResult = client.connect(config.daemon.socketPath);
    if (!connectResult.success()) {
        printError(connectResult.error().message());
        return 1;
    }

    auto reqResult = client.request(CommandType::List);
    if (!reqResult.success()) {
        printError(reqResult.error().message());
        return 1;
    }

    auto& data = reqResult.value();
    if (data.contains("_error")) {
        printError(data["_error"].get<std::string>());
        return 1;
    }

    if (!data.is_array() || data.empty()) {
        printInfo("没有活跃的会话");
        return 0;
    }

    std::cout << "\n  \x1b[1;36m活跃会话列表\x1b[0m\n\n";

    for (const auto& session : data) {
        std::string id = session.value("id", "");
        std::string title = session.value("title", "");
        std::string shell = session.value("shell", "");
        i32 pid = session.value("pid", 0);
        i32 clients = session.value("connectedClients", 0);
        bool alive = session.value("alive", false);
        i64 createdAt = session.value("createdAt", i64(0));
        i64 lastActivity = session.value("lastActivityAt", i64(0));

        if (alive) {
            std::cout << "  \x1b[32m[" << id << "]\x1b[0m " << title << "\n";
        } else {
            std::cout << "  \x1b[33m[" << id << "]\x1b[0m " << title << " (已停止)\n";
        }
        std::cout << "    Shell: " << shell << " | PID: " << pid
                  << " | 客户端: " << clients << "\n";
        std::cout << "    创建时间: " << formatTimestamp(createdAt) << "\n";
        std::cout << "    最后活动: " << formatTimestamp(lastActivity) << "\n\n";
    }

    client.disconnect();
    return 0;
}

static int cmdKill(const std::string& sessionId) {
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        printError(loadResult.error().message());
        return 1;
    }

    const Config& config = *configMgr.get();
    if (!ensureDaemonRunning(config)) {
        return 1;
    }

    IpcClient client;
    auto connectResult = client.connect(config.daemon.socketPath);
    if (!connectResult.success()) {
        printError(connectResult.error().message());
        return 1;
    }

    KillPayload payload;
    payload.sessionId = sessionId;

    auto reqResult = client.request(CommandType::Kill, payload.toJson());
    if (!reqResult.success()) {
        printError(reqResult.error().message());
        return 1;
    }

    auto& data = reqResult.value();
    if (data.contains("_error")) {
        printError(data["_error"].get<std::string>());
        return 1;
    }

    bool success = data.value("result", false);
    if (success) {
        printSuccess("会话 " + sessionId + " 已终止");
    } else {
        printError("会话 " + sessionId + " 不存在或已结束");
    }

    client.disconnect();
    return success ? 0 : 1;
}

static int cmdRename(const std::string& sessionId, const std::string& newTitle) {
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        printError(loadResult.error().message());
        return 1;
    }

    const Config& config = *configMgr.get();
    if (!ensureDaemonRunning(config)) {
        return 1;
    }

    IpcClient client;
    auto connectResult = client.connect(config.daemon.socketPath);
    if (!connectResult.success()) {
        printError(connectResult.error().message());
        return 1;
    }

    RenamePayload payload;
    payload.sessionId = sessionId;
    payload.newTitle = newTitle;

    auto reqResult = client.request(CommandType::Rename, payload.toJson());
    if (!reqResult.success()) {
        printError(reqResult.error().message());
        return 1;
    }

    auto& data = reqResult.value();
    if (data.contains("_error")) {
        printError(data["_error"].get<std::string>());
        return 1;
    }

    bool success = data.value("result", false);
    if (success) {
        printSuccess("会话已重命名为: " + newTitle);
    } else {
        printError("会话 " + sessionId + " 不存在");
    }

    client.disconnect();
    return success ? 0 : 1;
}

static int cmdAttach(const std::string& sessionId) {
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        printError(loadResult.error().message());
        return 1;
    }

    const Config& config = *configMgr.get();
    if (!ensureDaemonRunning(config)) {
        return 1;
    }

    IpcClient client;
    auto connectResult = client.connect(config.daemon.socketPath);
    if (!connectResult.success()) {
        printError(connectResult.error().message());
        return 1;
    }

    // 获取终端尺寸
    i32 cols = config.terminal.cols;
    i32 rows = config.terminal.rows;
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hStdOut, &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    // 发送 attach 请求
    AttachSessionPayload payload;
    payload.sessionId = sessionId;
    payload.cols = cols;
    payload.rows = rows;

    auto reqResult = client.request(CommandType::Attach, payload.toJson());
    if (!reqResult.success()) {
        printError(reqResult.error().message());
        return 1;
    }

    auto& data = reqResult.value();
    if (data.contains("_error")) {
        printError(data["_error"].get<std::string>());
        return 1;
    }

    // 显示历史输出
    if (data.contains("history") && data["history"].is_array()) {
        // 清屏
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD coord{0, 0};
        DWORD count;
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(hConsole, &info);
        DWORD size = static_cast<DWORD>(info.dwSize.X) * info.dwSize.Y;
        FillConsoleOutputCharacter(hConsole, ' ', size, coord, &count);
        FillConsoleOutputAttribute(hConsole, info.wAttributes, size, coord, &count);
        SetConsoleCursorPosition(hConsole, coord);

        for (const auto& line : data["history"]) {
            std::cout << line.get<std::string>() << "\n";
        }
    }

    printInfo("已连接到会话 " + sessionId + "，按 Ctrl+D 退出");

    // 设置 raw mode
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldInputMode = 0;
    GetConsoleMode(hStdIn, &oldInputMode);
    DWORD newInputMode = oldInputMode;
    newInputMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                       ENABLE_PROCESSED_INPUT);
    newInputMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hStdIn, newInputMode);

    // 设置输出支持 VT 序列
    DWORD oldOutputMode = 0;
    GetConsoleMode(hStdOut, &oldOutputMode);
    SetConsoleMode(hStdOut, oldOutputMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // 设置事件回调
    bool exiting = false;
    client.onEvent([&sessionId, &exiting](const IpcEvent& event) {
        if (event.eventType == EventType::Output && event.sessionId == sessionId) {
            if (event.data.is_string()) {
                std::cout << event.data.get<std::string>() << std::flush;
            }
        } else if (event.eventType == EventType::Exit && event.sessionId == sessionId) {
            printInfo("\n会话已结束");
            exiting = true;
        }
    });

    // 输入循环
    INPUT_RECORD records[16];
    while (!exiting) {
        DWORD eventsRead = 0;
        if (!ReadConsoleInputW(hStdIn, records, 16, &eventsRead)) {
            break;
        }

        for (DWORD i = 0; i < eventsRead; i++) {
            if (records[i].EventType != KEY_EVENT) continue;
            auto& keyEvent = records[i].Event.KeyEvent;

            if (!keyEvent.bKeyDown) continue;

            // Ctrl+D 退出
            if (keyEvent.wVirtualKeyCode == 0x44 && // 'D'
                (keyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                exiting = true;
                break;
            }

            // 获取输入字符
            if (keyEvent.uChar.UnicodeChar != 0) {
                char buf[8] = {};
                int len = WideCharToMultiByte(CP_UTF8, 0,
                    &keyEvent.uChar.UnicodeChar, 1, buf, sizeof(buf), nullptr, nullptr);
                if (len > 0) {
                    InputPayload payload;
                    payload.sessionId = sessionId;
                    payload.data = std::string(buf, len);
                    client.request(CommandType::Input, payload.toJson(), 0);
                }
            }
        }
    }

    // 恢复控制台模式
    SetConsoleMode(hStdIn, oldInputMode);
    SetConsoleMode(hStdOut, oldOutputMode);

    client.disconnect();
    printInfo("已断开会话连接");
    return 0;
}

// ============================================================
// 主入口
// ============================================================

int CliApp::run(int argc, char* argv[]) {
    CLI::App app{"TerminalHub - 终端会话管理器"};
    app.set_version_flag("-v,--version", "1.0.0");

    // init 命令
    app.add_subcommand("init", "初始化配置")->callback([]() {
        return cmdInit();
    });

    // new 命令
    auto* newCmd = app.add_subcommand("new", "创建新会话");
    std::string newTitle;
    std::string newShell;
    std::string newCwd;
    i32 newCols = 0;
    i32 newRows = 0;
    newCmd->add_option("-t,--title", newTitle, "会话标题");
    newCmd->add_option("-s,--shell", newShell, "Shell 类型");
    newCmd->add_option("-d,--cwd", newCwd, "工作目录");
    newCmd->add_option("--cols", newCols, "终端列数");
    newCmd->add_option("--rows", newRows, "终端行数");
    newCmd->callback([&]() {
        return cmdNew(newTitle, newShell, newCwd, newCols, newRows);
    });

    // list 命令
    app.add_subcommand("list", "列出活跃会话")->callback([]() {
        return cmdList();
    });

    // attach 命令
    std::string attachSessionId;
    auto* attachCmd = app.add_subcommand("attach", "连接到会话");
    attachCmd->add_option("session-id", attachSessionId, "会话 ID")->required();
    attachCmd->callback([&]() {
        return cmdAttach(attachSessionId);
    });

    // kill 命令
    std::string killSessionId;
    auto* killCmd = app.add_subcommand("kill", "终止会话");
    killCmd->add_option("session-id", killSessionId, "会话 ID")->required();
    killCmd->callback([&]() {
        return cmdKill(killSessionId);
    });

    // rename 命令
    std::string renameSessionId;
    std::string renameNewTitle;
    auto* renameCmd = app.add_subcommand("rename", "重命名会话");
    renameCmd->add_option("session-id", renameSessionId, "会话 ID")->required();
    renameCmd->add_option("new-title", renameNewTitle, "新标题")->required();
    renameCmd->callback([&]() {
        return cmdRename(renameSessionId, renameNewTitle);
    });

    CLI11_PARSE(app, argc, argv);
    return 0;
}

} // namespace th
