#include "terminalhub/Daemon/DaemonMain.hpp"
#include "terminalhub/IPC/IpcServer.hpp"
#include "terminalhub/IPC/IpcMessage.hpp"
#include "terminalhub/Session/SessionManager.hpp"
#include "terminalhub/PTY/ConPty.hpp"
#include "terminalhub/Storage/ConfigManager.hpp"
#include "terminalhub/Core/Logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <csignal>
#include <fstream>
#include <iostream>
#include <mutex>

namespace th {

using namespace ipc;

// Global pointers for signal handling
static IpcServer* g_server = nullptr;
static SessionManager* g_sessionMgr = nullptr;
static std::mutex g_shutdownMutex;

// ============================================================
// Ctrl+C / close signal handling
// ============================================================

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        Logger::info("Shutdown signal received, stopping...");

        std::lock_guard<std::mutex> lock(g_shutdownMutex);
        if (g_server) {
            g_server->stop();
        }
        return TRUE;
    }
    return FALSE;
}

// ============================================================
// isDaemonProcess
// ============================================================

bool DaemonMain::isDaemonProcess() {
    const char* env = std::getenv("TERMINALHUB_DAEMON");
    return env != nullptr && std::string(env) == "1";
}

// ============================================================
// Main loop
// ============================================================

int DaemonMain::run() {
    // Load config
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        // Try initializing
        auto initResult = ConfigManager::init();
        if (!initResult.success()) {
            std::cerr << "Config init failed: " << initResult.error().message() << "\n";
            return 1;
        }
        loadResult = configMgr.load();
        if (!loadResult.success()) {
            std::cerr << "Config load failed: " << loadResult.error().message() << "\n";
            return 1;
        }
    }

    const Config& config = *configMgr.get();

    // Initialize logger
    LogLevel logLevel = LogLevel::Info;
    if (config.daemon.logLevel == "debug") logLevel = LogLevel::Debug;
    else if (config.daemon.logLevel == "warn") logLevel = LogLevel::Warn;
    else if (config.daemon.logLevel == "error") logLevel = LogLevel::Error;
    Logger::init(logLevel, "Daemon");

    Logger::info("TerminalHub Daemon starting...");

    // Write PID file
    auto pidPath = Paths::daemonPidPath();
    {
        std::ofstream pidFile(pidPath);
        if (pidFile.is_open()) {
            pidFile << GetCurrentProcessId();
        }
    }
    Logger::info("PID: " + std::to_string(GetCurrentProcessId()));

    // Initialize session manager
    SessionManager sessionMgr(config);
    sessionMgr.initialize();

    // Start IPC server
    IpcServer server(config.daemon.socketPath);

    // Register command handlers

    // list
    server.onCommand(CommandType::List,
        [&sessionMgr](const nlohmann::json& /*payload*/, u64 /*clientId*/) -> nlohmann::json {
            auto sessions = sessionMgr.listSessions();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& s : sessions) {
                arr.push_back({
                    {"id", s.id},
                    {"title", s.title},
                    {"shell", s.shell},
                    {"pid", static_cast<i64>(s.pid)},
                    {"createdAt", s.createdAt},
                    {"lastActivityAt", s.lastActivityAt},
                    {"connectedClients", s.connectedClients},
                    {"alive", s.alive},
                });
            }
            return arr;
        });

    // new
    server.onCommand(CommandType::New,
        [&sessionMgr](const nlohmann::json& payload, u64 /*clientId*/) -> nlohmann::json {
            CreateSessionOptions opts;
            auto p = NewSessionPayload::fromJson(payload);
            opts.title = p.title;
            opts.cwd = p.cwd;
            opts.shell = p.shell;
            opts.cols = p.cols;
            opts.rows = p.rows;

            auto result = sessionMgr.createSession(opts);
            if (!result.success()) {
                throw std::runtime_error(result.error().message());
            }
            Session* session = result.value();
            return {
                {"sessionId", session->metadata.id},
                {"title", session->metadata.title},
            };
        });

    // kill
    server.onCommand(CommandType::Kill,
        [&sessionMgr](const nlohmann::json& payload, u64 /*clientId*/) -> nlohmann::json {
            auto p = KillPayload::fromJson(payload);
            bool success = sessionMgr.killSession(p.sessionId);
            return {{"result", success}};
        });

    // rename
    server.onCommand(CommandType::Rename,
        [&sessionMgr](const nlohmann::json& payload, u64 /*clientId*/) -> nlohmann::json {
            auto p = RenamePayload::fromJson(payload);
            bool success = sessionMgr.renameSession(p.sessionId, p.newTitle);
            return {{"result", success}};
        });

    // attach
    server.onCommand(CommandType::Attach,
        [&sessionMgr, &server](const nlohmann::json& payload, u64 clientId) -> nlohmann::json {
            auto p = AttachSessionPayload::fromJson(payload);
            Session* session = sessionMgr.getSession(p.sessionId);
            if (!session) {
                throw std::runtime_error("Session not found: " + p.sessionId);
            }

            session->addClient(clientId);
            session->touch();

            // Resize PTY
            if (session->ptyProcess && p.cols && p.rows) {
                session->ptyProcess->resize(*p.cols, *p.rows);
            }

            // Subscribe to session output (per-client listener)
            std::string sid = p.sessionId;
            session->addOutputListener(clientId, [&server, clientId, sid](const std::string& data) {
                server.sendToClient(clientId, EventType::Output,
                                    nlohmann::json(data), sid);
            });

            session->addExitListener(clientId, [&server, clientId, sid](u32 exitCode) {
                server.sendToClient(clientId, EventType::Exit,
                                    {{"code", exitCode}}, sid);
            });

            // Return history output
            auto history = session->outputBuffer.getRecentLines();
            nlohmann::json historyArr = nlohmann::json::array();
            for (const auto& line : history) {
                historyArr.push_back(line);
            }
            return {{"history", historyArr}};
        });

    // input
    server.onCommand(CommandType::Input,
        [&sessionMgr](const nlohmann::json& payload, u64 /*clientId*/) -> nlohmann::json {
            auto p = InputPayload::fromJson(payload);
            Session* session = sessionMgr.getSession(p.sessionId);
            if (!session || !session->ptyProcess) {
                throw std::runtime_error("Session not found or exited: " + p.sessionId);
            }
            session->ptyProcess->write(p.data);
            session->touch();
            return nullptr;
        });

    // resize
    server.onCommand(CommandType::Resize,
        [&sessionMgr](const nlohmann::json& payload, u64 /*clientId*/) -> nlohmann::json {
            auto p = ResizePayload::fromJson(payload);
            Session* session = sessionMgr.getSession(p.sessionId);
            if (!session || !session->ptyProcess) {
                throw std::runtime_error("Session not found or exited: " + p.sessionId);
            }
            session->ptyProcess->resize(p.cols, p.rows);
            return nullptr;
        });

    // detach - remove client from session
    server.onCommand(CommandType::Detach,
        [&sessionMgr](const nlohmann::json& payload, u64 clientId) -> nlohmann::json {
            auto p = AttachSessionPayload::fromJson(payload);
            Session* session = sessionMgr.getSession(p.sessionId);
            if (session) {
                session->removeClient(clientId);
            }
            return nullptr;
        });

    // shutdown
    server.onCommand(CommandType::Shutdown,
        [&server](const nlohmann::json& /*payload*/, u64 /*clientId*/) -> nlohmann::json {
        // Delayed stop in another thread to avoid deadlock
        std::thread([&server]() {
            Sleep(100);
            server.stop();
        }).detach();
        return {{"result", true}};
    });

    // Client disconnect handling
    server.onClientDisconnect([&sessionMgr](u64 clientId) {
        // Remove client from all sessions
        auto sessions = sessionMgr.listSessions();
        for (const auto& item : sessions) {
            Session* session = sessionMgr.getSession(item.id);
            if (session) {
                session->removeClient(clientId);
            }
        }
    });

    // Register signal handler
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    // Save global pointers
    {
        std::lock_guard<std::mutex> lock(g_shutdownMutex);
        g_server = &server;
        g_sessionMgr = &sessionMgr;
    }

    // Start IPC server (blocking)
    if (!server.start()) {
        Logger::error("IPC server failed to start");
        return 1;
    }

    Logger::info("IPC server started: " + config.daemon.socketPath);

    // Wait for server to stop
    // IPC server returns only after stop() is called
    // Simple loop to keep process alive
    while (true) {
        Sleep(1000);
    }

    // Cleanup PID file
    std::filesystem::remove(pidPath);

    Logger::info("Daemon stopped");
    return 0;
}

} // namespace th
