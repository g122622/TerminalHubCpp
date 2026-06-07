#pragma once

#include "terminalhub/Core/Types.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace th::ipc {

// ============================================================
// 命令类型
// ============================================================

enum class CommandType {
    List,
    New,
    Attach,
    Detach,
    Kill,
    Rename,
    Input,
    Resize,
    Shutdown,
};

std::string commandTypeToString(CommandType cmd);
CommandType commandTypeFromString(const std::string& str);

// ============================================================
// 事件类型
// ============================================================

enum class EventType {
    Output,
    Exit,
    SessionAdded,
    SessionRemoved,
    Error,
};

std::string eventTypeToString(EventType evt);
EventType eventTypeFromString(const std::string& str);

// ============================================================
// 消息基类
// ============================================================

enum class MessageType {
    Request,
    Response,
    Event,
};

// ============================================================
// IPC 请求
// ============================================================

struct IpcRequest {
    MessageType type = MessageType::Request;
    std::string id;             // UUID 请求标识
    CommandType command;
    nlohmann::json payload;     // 可选 payload

    std::string serialize() const;
    static std::optional<IpcRequest> deserialize(const std::string& json);
};

// ============================================================
// IPC 响应
// ============================================================

struct IpcResponse {
    MessageType type = MessageType::Response;
    std::string id;             // 对应请求的 ID
    bool success = false;
    nlohmann::json data;        // 成功时的返回数据
    std::string error;          // 失败时的错误信息

    std::string serialize() const;
    static std::optional<IpcResponse> deserialize(const std::string& json);
};

// ============================================================
// IPC 事件
// ============================================================

struct IpcEvent {
    MessageType type = MessageType::Event;
    EventType eventType;
    std::string sessionId;      // 可选，关联的会话 ID
    nlohmann::json data;

    std::string serialize() const;
    static std::optional<IpcEvent> deserialize(const std::string& json);
};

// ============================================================
// Payload 类型
// ============================================================

struct NewSessionPayload {
    std::optional<std::string> title;
    std::optional<std::string> cwd;
    std::optional<std::string> shell;
    std::optional<i32> cols;
    std::optional<i32> rows;

    static NewSessionPayload fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct AttachSessionPayload {
    std::string sessionId;
    std::optional<i32> cols;
    std::optional<i32> rows;

    static AttachSessionPayload fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct InputPayload {
    std::string sessionId;
    std::string data;

    static InputPayload fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct ResizePayload {
    std::string sessionId;
    i32 cols = 80;
    i32 rows = 24;

    static ResizePayload fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct RenamePayload {
    std::string sessionId;
    std::string newTitle;

    static RenamePayload fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct KillPayload {
    std::string sessionId;

    static KillPayload fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

// ============================================================
// 辅助：生成 UUID
// ============================================================

std::string generateRequestId();
std::string generateSessionId();

} // namespace th::ipc
