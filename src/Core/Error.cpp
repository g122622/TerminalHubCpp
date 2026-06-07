#include "terminalhub/Core/Error.hpp"

namespace th {

Error::Error(Code code, std::string message, std::string source)
    : m_code(code)
    , m_message(std::move(message))
    , m_source(std::move(source)) {
}

Error::Code Error::code() const {
    return m_code;
}

const std::string& Error::message() const {
    return m_message;
}

const std::string& Error::source() const {
    return m_source;
}

std::string Error::toString() const {
    std::string result;
    if (!m_source.empty()) {
        result += "[" + m_source + "] ";
    }
    result += m_message;
    return result;
}

Error Error::invalidArgument(std::string_view msg, std::string_view source) {
    return Error(Code::InvalidArgument, std::string(msg), std::string(source));
}

Error Error::notFound(std::string_view msg, std::string_view source) {
    return Error(Code::NotFound, std::string(msg), std::string(source));
}

Error Error::ioError(std::string_view msg, std::string_view source) {
    return Error(Code::IOError, std::string(msg), std::string(source));
}

Error Error::sessionNotFound(std::string_view msg, std::string_view source) {
    return Error(Code::SessionNotFound, std::string(msg), std::string(source));
}

Error Error::configError(std::string_view msg, std::string_view source) {
    return Error(Code::ConfigError, std::string(msg), std::string(source));
}

Error Error::daemonError(std::string_view msg, std::string_view source) {
    return Error(Code::DaemonError, std::string(msg), std::string(source));
}

Error Error::ptyError(std::string_view msg, std::string_view source) {
    return Error(Code::PtyError, std::string(msg), std::string(source));
}

Error Error::ipcError(std::string_view msg, std::string_view source) {
    return Error(Code::IpcError, std::string(msg), std::string(source));
}

} // namespace th
