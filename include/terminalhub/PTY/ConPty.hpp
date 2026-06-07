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
 * @brief ConPTY process options
 */
struct ConPtyOptions {
    std::string shell;                              // Shell executable path
    std::string cwd;                                // Working directory (empty = inherit current)
    std::unordered_map<std::string, std::string> env; // Extra environment variables
    i32 cols = 80;                                  // Terminal columns
    i32 rows = 24;                                  // Terminal rows
};

/**
 * @brief Windows ConPTY wrapper
 *
 * Uses Win32 Pseudo Console API to create a pseudo-terminal process.
 * Implemented via CreatePseudoConsole + CreateProcess.
 */
class ConPty {
public:
    /**
     * @brief Create a ConPTY instance
     * @param options PTY options
     * @return unique_ptr on success, error on failure
     */
    [[nodiscard]] static Result<std::unique_ptr<ConPty>> create(const ConPtyOptions& options);

    ~ConPty();

    // Non-copyable
    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    // Movable
    ConPty(ConPty&& other) noexcept;
    ConPty& operator=(ConPty&& other) noexcept;

    /**
     * @brief Write input data to the PTY
     */
    void write(std::string_view data);

    /**
     * @brief Resize the PTY terminal
     */
    void resize(i32 cols, i32 rows);

    /**
     * @brief Kill the PTY process
     */
    void kill();

    /**
     * @brief Get the child process PID
     */
    [[nodiscard]] DWORD pid() const;

    /**
     * @brief Check if the child process is alive
     */
    [[nodiscard]] bool isAlive() const;

    /**
     * @brief Set output callback
     */
    void onOutput(std::function<void(std::string_view)> callback);

    /**
     * @brief Set exit callback
     */
    void onExit(std::function<void(u32 exitCode)> callback);

private:
    ConPty() = default;

    /**
     * @brief Read thread entry: reads data from the PTY output pipe
     */
    void _readThreadFunc();

    /**
     * @brief Exit watch thread entry: waits for the child process to exit
     */
    void _exitWatchThreadFunc();

    // Win32 handles
    HPCON m_hPc{nullptr};
    HANDLE m_hPipeIn{INVALID_HANDLE_VALUE};   // Write end (send input to PTY)
    HANDLE m_hPipeOut{INVALID_HANDLE_VALUE};   // Read end (read output from PTY)
    HANDLE m_hProcess{nullptr};
    DWORD m_pid{0};

    // Threads
    HANDLE m_hReadThread{nullptr};
    HANDLE m_hExitThread{nullptr};
    volatile bool m_running{false};

    // Callbacks
    std::function<void(std::string_view)> m_onOutput;
    std::function<void(u32 exitCode)> m_onExit;

    // Terminal dimensions
    i32 m_cols{80};
    i32 m_rows{24};
};

/**
 * @brief Get the default shell path
 * @param configShell Shell name from config (powershell/cmd/bash)
 */
std::string getDefaultShell(const std::string& configShell);

} // namespace th
