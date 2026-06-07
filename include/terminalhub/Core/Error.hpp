#pragma once

#include <string>
#include <string_view>

namespace th {

/**
 * @brief 错误类，携带错误码、消息和来源
 */
class Error {
public:
    enum class Code {
        Success = 0,
        NotFound,
        InvalidArgument,
        IOError,
        SessionNotFound,
        ConfigError,
        DaemonError,
        PtyError,
        IpcError,
    };

    Error() = default;
    Error(Code code, std::string message, std::string source = "");
    Error(const Error&) = default;
    Error(Error&&) = default;
    Error& operator=(const Error&) = default;
    Error& operator=(Error&&) = default;

    [[nodiscard]] Code code() const;
    [[nodiscard]] const std::string& message() const;
    [[nodiscard]] const std::string& source() const;
    [[nodiscard]] std::string toString() const;

    static Error invalidArgument(std::string_view msg, std::string_view source = "");
    static Error notFound(std::string_view msg, std::string_view source = "");
    static Error ioError(std::string_view msg, std::string_view source = "");
    static Error sessionNotFound(std::string_view msg, std::string_view source = "");
    static Error configError(std::string_view msg, std::string_view source = "");
    static Error daemonError(std::string_view msg, std::string_view source = "");
    static Error ptyError(std::string_view msg, std::string_view source = "");
    static Error ipcError(std::string_view msg, std::string_view source = "");

private:
    Code m_code{Code::Success};
    std::string m_message;
    std::string m_source;
};

} // namespace th
