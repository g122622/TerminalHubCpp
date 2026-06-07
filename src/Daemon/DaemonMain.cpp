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

// 全局指针，用于信号处理
static IpcServer* g_server = nullptr;
static SessionManager* g_sessionMgr = nullptr;
static std::mutex g_shutdownMutex;

// ============================================================
// Ctrl+C / 关闭信号处理
// ============================================================

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        Logger::info("收到关闭信号，正在停止...");

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
// 主循环
// ============================================================

int DaemonMain::run() {
    // 加载配置
    ConfigManager configMgr;
    auto loadResult = configMgr.load();
    if (!loadResult.success()) {
        // 尝试初始化
        auto initResult = ConfigManager::init();
        if (!initResult.success()) {
            std::cerr << "配置初始化失败: " << initResult.error().message() << "\n";
            return 1;
        }
        loadResult = configMgr.load();
        if (!loadResult.success()) {
            std::cerr << "配置加载失败: " << loadResult.error().message() << "\n";
            return 1;
        }
    }

    const Config& config = *configMgr.get();

    // 初始化日志
    LogLevel logLevel = LogLevel::Info;
    if (config.daemon.logLevel == "debug") logLevel = LogLevel::Debug;
    else if (config.daemon.logLevel == "warn") logLevel = LogLevel::Warn;
    else if (config.daemon.logLevel == "error") logLevel = LogLevel::Error;
    Logger::init(logLevel, "Daemon");

    Logger::info("TerminalHub Daemon 启动中...");

    // 写入 PID 文件
    auto pidPath = Paths::daemonPidPath();
    {
        std::ofstream pidFile(pidPath);
        if (pidFile.is_open()) {
            pidFile << GetCurrentProcessId();
        }
    }
    Logger::info("PID: " + std::to_string(GetCurrentProcessId()));

    // 初始化会话管理器
    SessionManager sessionMgr(config);
    sessionMgr.initialize();

    // 启动 IPC 服务器
    IpcServer server(config.daemon.socketPath);

    // 注册命令处理器

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
                throw std::runtime_error("会话不存在: " + p.sessionId);
            }

            session->addClient(clientId);
            session->touch();

            // 调整 PTY 大小
            if (session->ptyProcess && p.cols && p.rows) {
                session->ptyProcess->resize(*p.cols, *p.rows);
            }

            // 订阅会话输出
            std::string sid = p.sessionId;
            session->onOutput([&server, clientId, sid](const std::string& data) {
                server.sendToClient(clientId, EventType::Output,
                                    nlohmann::json(data), sid);
            });

            session->onExit([&server, clientId, sid](u32 exitCode) {
                server.sendToClient(clientId, EventType::Exit,
                                    {{"code", exitCode}}, sid);
            });

            // 返回历史输出
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
                throw std::runtime_error("会话不存在或已结束: " + p.sessionId);
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
                throw std::runtime_error("会话不存在或已结束: " + p.sessionId);
            }
            session->ptyProcess->resize(p.cols, p.rows);
            return nullptr;
        });

    // detach - 从会话移除客户端
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
        // 在另一个线程中延迟停止，避免死锁
        std::thread([&server]() {
            Sleep(100);
            server.stop();
        }).detach();
        return {{"result", true}};
    });

    // 客户端断开处理
    server.onClientDisconnect([&sessionMgr](u64 clientId) {
        // 从所有会话中移除该客户端
        auto sessions = sessionMgr.listSessions();
        for (const auto& item : sessions) {
            Session* session = sessionMgr.getSession(item.id);
            if (session) {
                session->removeClient(clientId);
            }
        }
    });

    // 注册信号处理
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    // 保存全局指针
    {
        std::lock_guard<std::mutex> lock(g_shutdownMutex);
        g_server = &server;
        g_sessionMgr = &sessionMgr;
    }

    // 启动 IPC 服务器（阻塞）
    if (!server.start()) {
        Logger::error("IPC 服务器启动失败");
        return 1;
    }

    Logger::info("IPC 服务器已启动: " + config.daemon.socketPath);

    // 等待服务器停止
    // IPC 服务器在 stop() 被调用后才会返回
    // 这里用简单的循环等待
    while (true) {
        Sleep(1000);
    }

    // 清理 PID 文件
    std::filesystem::remove(pidPath);

    Logger::info("Daemon 已停止");
    return 0;
}

} // namespace th
