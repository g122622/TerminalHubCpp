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
// Helper functions
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
 * @brief Ensure the daemon is running, start it if not
 * @return true on success
 */
static bool ensureDaemonRunning(const Config& config) {
    std::string pipePath = config.daemon.socketPath;

    if (IpcClient::isDaemonRunning(pipePath)) {
        return true;
    }

    // Launch daemon process
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    std::string envStr = "TERMINALHUB_DAEMON=1";

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    PROCESS_INFORMATION pi{};

    // Build environment block
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
        printError("Failed to start daemon: " + std::to_string(GetLastError()));
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Wait for daemon to be ready (up to 10 attempts, 500ms each)
    for (int i = 0; i < 10; i++) {
        Sleep(500);
        if (IpcClient::isDaemonRunning(pipePath)) {
            return true;
        }
    }

    printError("Daemon startup timed out");
    return false;
}

// ============================================================
// Command implementations
// ============================================================

static int cmdInit() {
    auto result = ConfigManager::init();
    if (!result.success()) {
        printError(result.error().message());
        return 1;
    }
    printSuccess("Configuration initialized");
    return 0;
}

static int cmdNew(const std::string& title, const std::string& shell,
                   const std::string& cwd, i32 cols, i32 rows) {
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        printError(loadResult.error().message());
        printInfo("Run 'th init' first to initialize configuration");
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

    // Build request payload
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

    printSuccess("Session created: " + sessionId);
    printInfo("Title: " + sessionTitle);
    printInfo("Use 'th attach " + sessionId + "' to connect to this session");

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
        printInfo("No active sessions");
        return 0;
    }

    std::cout << "\n  \x1b[1;36mActive Sessions\x1b[0m\n\n";

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
            std::cout << "  \x1b[33m[" << id << "]\x1b[0m " << title << " (stopped)\n";
        }
        std::cout << "    Shell: " << shell << " | PID: " << pid
                  << " | Clients: " << clients << "\n";
        std::cout << "    Created: " << formatTimestamp(createdAt) << "\n";
        std::cout << "    Last activity: " << formatTimestamp(lastActivity) << "\n\n";
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
        printSuccess("Session " + sessionId + " terminated");
    } else {
        printError("Session " + sessionId + " not found or already exited");
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
        printSuccess("Session renamed to: " + newTitle);
    } else {
        printError("Session " + sessionId + " not found");
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

    // Get terminal size
    i32 cols = config.terminal.cols;
    i32 rows = config.terminal.rows;
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hStdOut, &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    // Send attach request
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

    // Display history output
    if (data.contains("history") && data["history"].is_array()) {
        // Clear screen
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

    printInfo("Connected to session " + sessionId + ", press Ctrl+D to exit");

    // Set raw mode
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldInputMode = 0;
    GetConsoleMode(hStdIn, &oldInputMode);
    DWORD newInputMode = oldInputMode;
    newInputMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                       ENABLE_PROCESSED_INPUT);
    newInputMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hStdIn, newInputMode);

    // Enable VT sequences on output
    DWORD oldOutputMode = 0;
    GetConsoleMode(hStdOut, &oldOutputMode);
    SetConsoleMode(hStdOut, oldOutputMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // Set event callback
    bool exiting = false;
    client.onEvent([&sessionId, &exiting](const IpcEvent& event) {
        if (event.eventType == EventType::Output && event.sessionId == sessionId) {
            if (event.data.is_string()) {
                std::cout << event.data.get<std::string>() << std::flush;
            }
        } else if (event.eventType == EventType::Exit && event.sessionId == sessionId) {
            printInfo("\nSession exited");
            exiting = true;
        }
    });

    // Input loop
    INPUT_RECORD records[16];
    while (!exiting) {
        DWORD eventsRead = 0;
        if (!ReadConsoleInputW(hStdIn, records, 16, &eventsRead)) {
            break;
        }

        for (DWORD i = 0; i < eventsRead; i++) {
            if (records[i].EventType == KEY_EVENT) {
                auto& keyEvent = records[i].Event.KeyEvent;

                if (!keyEvent.bKeyDown) continue;

                // Ctrl+D to exit
                if (keyEvent.wVirtualKeyCode == 0x44 && // 'D'
                    (keyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                    exiting = true;
                    break;
                }

                // Get input character
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
            } else if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                // Terminal resize event
                CONSOLE_SCREEN_BUFFER_INFO csbi2;
                if (GetConsoleScreenBufferInfo(hStdOut, &csbi2)) {
                    i32 newCols = csbi2.srWindow.Right - csbi2.srWindow.Left + 1;
                    i32 newRows = csbi2.srWindow.Bottom - csbi2.srWindow.Top + 1;
                    if (newCols > 0 && newRows > 0) {
                        ResizePayload resizePayload;
                        resizePayload.sessionId = sessionId;
                        resizePayload.cols = newCols;
                        resizePayload.rows = newRows;
                        client.request(CommandType::Resize, resizePayload.toJson(), 0);
                    }
                }
            }
        }
    }

    // Restore console mode
    SetConsoleMode(hStdIn, oldInputMode);
    SetConsoleMode(hStdOut, oldOutputMode);

    client.disconnect();
    printInfo("Disconnected from session");
    return 0;
}

// ============================================================
// Main entry
// ============================================================

int CliApp::run(int argc, char* argv[]) {
    CLI::App app{"TerminalHub - Terminal session manager"};
    app.set_version_flag("-v,--version", "1.0.0");

    // init command
    app.add_subcommand("init", "Initialize configuration")->callback([]() {
        return cmdInit();
    });

    // new command
    auto* newCmd = app.add_subcommand("new", "Create a new session");
    std::string newTitle;
    std::string newShell;
    std::string newCwd;
    i32 newCols = 0;
    i32 newRows = 0;
    newCmd->add_option("-t,--title", newTitle, "Session title");
    newCmd->add_option("-s,--shell", newShell, "Shell type");
    newCmd->add_option("-d,--cwd", newCwd, "Working directory");
    newCmd->add_option("--cols", newCols, "Terminal columns");
    newCmd->add_option("--rows", newRows, "Terminal rows");
    newCmd->callback([&]() {
        return cmdNew(newTitle, newShell, newCwd, newCols, newRows);
    });

    // list command
    app.add_subcommand("list", "List active sessions")->callback([]() {
        return cmdList();
    });

    // attach command
    std::string attachSessionId;
    auto* attachCmd = app.add_subcommand("attach", "Connect to a session");
    attachCmd->add_option("session-id", attachSessionId, "Session ID")->required();
    attachCmd->callback([&]() {
        return cmdAttach(attachSessionId);
    });

    // kill command
    std::string killSessionId;
    auto* killCmd = app.add_subcommand("kill", "Terminate a session");
    killCmd->add_option("session-id", killSessionId, "Session ID")->required();
    killCmd->callback([&]() {
        return cmdKill(killSessionId);
    });

    // rename command
    std::string renameSessionId;
    std::string renameNewTitle;
    auto* renameCmd = app.add_subcommand("rename", "Rename a session");
    renameCmd->add_option("session-id", renameSessionId, "Session ID")->required();
    renameCmd->add_option("new-title", renameNewTitle, "New title")->required();
    renameCmd->callback([&]() {
        return cmdRename(renameSessionId, renameNewTitle);
    });

    CLI11_PARSE(app, argc, argv);
    return 0;
}

} // namespace th
