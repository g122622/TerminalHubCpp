#pragma once

#include "terminalhub/Core/Types.hpp"

namespace th {

/**
 * @brief 守护进程入口
 *
 * 通过环境变量 TERMINALHUB_DAEMON=1 识别。
 * 加载配置 → 初始化会话管理器 → 启动 IPC 服务器 → 注册命令处理器 → 运行。
 */
class DaemonMain {
public:
    /**
     * @brief 运行守护进程主循环
     * @return 退出码
     */
    static int run();

    /**
     * @brief 检查当前进程是否为守护进程
     */
    static bool isDaemonProcess();
};

} // namespace th
