#include "terminalhub/Core/Logger.hpp"
#include "terminalhub/Core/Result.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // TODO: 实现入口点 - CLI/Daemon 模式切换
    std::cout << "TerminalHub C++ v1.0.0\n";

    // 初始化日志
    th::Logger::init(th::LogLevel::Info);

    // 检测守护进程模式
    const char* daemonEnv = std::getenv("TERMINALHUB_DAEMON");
    if (daemonEnv != nullptr && std::string(daemonEnv) == "1") {
        th::Logger::info("启动守护进程模式");
        // TODO: DaemonMain::run()
    } else {
        th::Logger::info("启动 CLI 模式");
        // TODO: CliApp::run(argc, argv)
    }

    return 0;
}
