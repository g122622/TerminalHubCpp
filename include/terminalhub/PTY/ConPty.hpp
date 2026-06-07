#pragma once

#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Core/Types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace th {

/**
 * @brief ConPTY 进程选项
 */
struct ConPtyOptions {
    std::string shell;                              // shell 可执行路径
    std::string cwd;                                // 工作目录（空=继承当前）
    std::unordered_map<std::string, std::string> env; // 额外环境变量
    i32 cols = 80;                                  // 终端列数
    i32 rows = 24;                                  // 终端行数
};

/**
 * @brief Windows ConPTY 封装
 *
 * 使用 Win32 Pseudo Console API 创建伪终端进程。
 * 通过 CreatePseudoConsole + CreateProcess 实现。
 */
class ConPty {
public:
    /**
     * @brief 创建 ConPTY 实例
     * @param options PTY 选项
     * @return 成功返回 unique_ptr，失败返回错误
     */
    [[nodiscard]] static Result<std::unique_ptr<ConPty>> create(const ConPtyOptions& options);

    ~ConPty();

    // 禁止拷贝
    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    // 允许移动
    ConPty(ConPty&& other) noexcept;
    ConPty& operator=(ConPty&& other) noexcept;

    /**
     * @brief 向 PTY 写入输入数据
     */
    void write(std::string_view data);

    /**
     * @brief 调整 PTY 终端大小
     */
    void resize(i32 cols, i32 rows);

    /**
     * @brief 终止 PTY 进程
     */
    void kill();

    /**
     * @brief 获取子进程 PID
     */
    [[nodiscard]] DWORD pid() const;

    /**
     * @brief 检查子进程是否存活
     */
    [[nodiscard]] bool isAlive() const;

    /**
     * @brief 设置输出回调
     */
    void onOutput(std::function<void(std::string_view)> callback);

    /**
     * @brief 设置退出回调
     */
    void onExit(std::function<void(u32 exitCode)> callback);

private:
    ConPty() = default;

    /**
     * @brief 读取线程入口：从 PTY 输出管道读取数据
     */
    void _readThreadFunc();

    /**
     * @brief 退出监听线程入口：等待子进程退出
     */
    void _exitWatchThreadFunc();

    // Win32 句柄
    HPCON m_hPc{nullptr};
    HANDLE m_hPipeIn{INVALID_HANDLE_VALUE};   // 写入端（向 PTY 发送输入）
    HANDLE m_hPipeOut{INVALID_HANDLE_VALUE};   // 读取端（从 PTY 读取输出）
    HANDLE m_hProcess{nullptr};
    DWORD m_pid{0};

    // 线程
    HANDLE m_hReadThread{nullptr};
    HANDLE m_hExitThread{nullptr};
    volatile bool m_running{false};

    // 回调
    std::function<void(std::string_view)> m_onOutput;
    std::function<void(u32 exitCode)> m_onExit;

    // 终端尺寸
    i32 m_cols{80};
    i32 m_rows{24};
};

/**
 * @brief 获取默认 shell 路径
 * @param configShell 配置中的 shell 名称（powershell/cmd/bash）
 */
std::string getDefaultShell(const std::string& configShell);

} // namespace th
