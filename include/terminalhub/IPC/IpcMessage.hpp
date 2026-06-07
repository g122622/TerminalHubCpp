#pragma once

#include "terminalhub/Core/Types.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace th::ipc {

// ============================================================
// Command types
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
// Event types
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
// Message base class
// ============================================================

enum class MessageType {
    Request,
    Response,
    Event,
};

// ============================================================
// IPC Request
// ============================================================

struct IpcRequest {
    MessageType type = MessageType::Request;
    std::string id;             // UUID request identifier
    CommandType command;
    nlohmann::json payload;     // Optional payload

    std::string serialize() const;
    static std::optional<IpcRequest> deserialize(const std::string& json);
};

// ============================================================
// IPC Response
// ============================================================

struct IpcResponse {
    MessageType type = MessageType::Response;
    std::string id;             // ID of the corresponding request
    bool success = false;
    nlohmann::json data;        // Return data on success
    std::string error;          // Error message on failure

    std::string serialize() const;
    static std::optional<IpcResponse> deserialize(const std::string& json);
};

// ============================================================
// IPC Event
// ============================================================

struct IpcEvent {
    MessageType type = MessageType::Event;
    EventType eventType;
    std::string sessionId;      // Optional, associated session ID
    nlohmann::json data;

    std::string serialize() const;
    static std::optional<IpcEvent> deserialize(const std::string& json);
};

// ============================================================
// Payload types
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
// Helper: generate UUID
// ============================================================

std::string generateRequestId();
std::string generateSessionId();

} // namespace th::ipc
