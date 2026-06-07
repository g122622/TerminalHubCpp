#include "terminalhub/IPC/IpcMessage.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <rpc.h>

#include <chrono>
#include <random>

namespace th::ipc {

// ============================================================
// CommandType conversion
// ============================================================

std::string commandTypeToString(CommandType cmd) {
    switch (cmd) {
        case CommandType::List:    return "list";
        case CommandType::New:     return "new";
        case CommandType::Attach:  return "attach";
        case CommandType::Detach:  return "detach";
        case CommandType::Kill:    return "kill";
        case CommandType::Rename:  return "rename";
        case CommandType::Input:   return "input";
        case CommandType::Resize:  return "resize";
        case CommandType::Shutdown:return "shutdown";
    }
    return "unknown";
}

CommandType commandTypeFromString(const std::string& str) {
    if (str == "list")     return CommandType::List;
    if (str == "new")      return CommandType::New;
    if (str == "attach")   return CommandType::Attach;
    if (str == "detach")   return CommandType::Detach;
    if (str == "kill")     return CommandType::Kill;
    if (str == "rename")   return CommandType::Rename;
    if (str == "input")    return CommandType::Input;
    if (str == "resize")   return CommandType::Resize;
    if (str == "shutdown") return CommandType::Shutdown;
    return CommandType::List; // Default
}

// ============================================================
// EventType conversion
// ============================================================

std::string eventTypeToString(EventType evt) {
    switch (evt) {
        case EventType::Output:        return "output";
        case EventType::Exit:          return "exit";
        case EventType::SessionAdded:  return "session_added";
        case EventType::SessionRemoved:return "session_removed";
        case EventType::Error:         return "error";
    }
    return "unknown";
}

EventType eventTypeFromString(const std::string& str) {
    if (str == "output")          return EventType::Output;
    if (str == "exit")            return EventType::Exit;
    if (str == "session_added")   return EventType::SessionAdded;
    if (str == "session_removed") return EventType::SessionRemoved;
    if (str == "error")           return EventType::Error;
    return EventType::Error;
}

// ============================================================
// IpcRequest serialization
// ============================================================

std::string IpcRequest::serialize() const {
    nlohmann::json j;
    j["type"] = "request";
    j["id"] = id;
    j["command"] = commandTypeToString(command);
    j["payload"] = payload;
    return j.dump();
}

std::optional<IpcRequest> IpcRequest::deserialize(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        if (j.value("type", "") != "request") {
            return std::nullopt;
        }
        IpcRequest req;
        req.id = j.value("id", "");
        req.command = commandTypeFromString(j.value("command", ""));
        req.payload = j.value("payload", nlohmann::json::object());
        return req;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================
// IpcResponse serialization
// ============================================================

std::string IpcResponse::serialize() const {
    nlohmann::json j;
    j["type"] = "response";
    j["id"] = id;
    j["success"] = success;
    if (success) {
        j["data"] = data;
    } else {
        j["error"] = error;
    }
    return j.dump();
}

std::optional<IpcResponse> IpcResponse::deserialize(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        if (j.value("type", "") != "response") {
            return std::nullopt;
        }
        IpcResponse resp;
        resp.id = j.value("id", "");
        resp.success = j.value("success", false);
        if (resp.success) {
            resp.data = j.value("data", nlohmann::json());
        } else {
            resp.error = j.value("error", "");
        }
        return resp;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================
// IpcEvent serialization
// ============================================================

std::string IpcEvent::serialize() const {
    nlohmann::json j;
    j["type"] = "event";
    j["eventType"] = eventTypeToString(eventType);
    j["sessionId"] = sessionId;
    j["data"] = data;
    return j.dump();
}

std::optional<IpcEvent> IpcEvent::deserialize(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        if (j.value("type", "") != "event") {
            return std::nullopt;
        }
        IpcEvent evt;
        evt.eventType = eventTypeFromString(j.value("eventType", "error"));
        evt.sessionId = j.value("sessionId", "");
        evt.data = j.value("data", nlohmann::json());
        return evt;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================
// Payload implementations
// ============================================================

NewSessionPayload NewSessionPayload::fromJson(const nlohmann::json& j) {
    NewSessionPayload p;
    if (j.contains("title") && !j["title"].is_null()) p.title = j["title"].get<std::string>();
    if (j.contains("cwd") && !j["cwd"].is_null()) p.cwd = j["cwd"].get<std::string>();
    if (j.contains("shell") && !j["shell"].is_null()) p.shell = j["shell"].get<std::string>();
    if (j.contains("cols") && !j["cols"].is_null()) p.cols = j["cols"].get<i32>();
    if (j.contains("rows") && !j["rows"].is_null()) p.rows = j["rows"].get<i32>();
    return p;
}

nlohmann::json NewSessionPayload::toJson() const {
    nlohmann::json j;
    if (title) j["title"] = *title;
    if (cwd) j["cwd"] = *cwd;
    if (shell) j["shell"] = *shell;
    if (cols) j["cols"] = *cols;
    if (rows) j["rows"] = *rows;
    return j;
}

AttachSessionPayload AttachSessionPayload::fromJson(const nlohmann::json& j) {
    AttachSessionPayload p;
    p.sessionId = j.value("sessionId", "");
    if (j.contains("cols") && !j["cols"].is_null()) p.cols = j["cols"].get<i32>();
    if (j.contains("rows") && !j["rows"].is_null()) p.rows = j["rows"].get<i32>();
    return p;
}

nlohmann::json AttachSessionPayload::toJson() const {
    nlohmann::json j;
    j["sessionId"] = sessionId;
    if (cols) j["cols"] = *cols;
    if (rows) j["rows"] = *rows;
    return j;
}

InputPayload InputPayload::fromJson(const nlohmann::json& j) {
    InputPayload p;
    p.sessionId = j.value("sessionId", "");
    p.data = j.value("data", "");
    return p;
}

nlohmann::json InputPayload::toJson() const {
    return {{"sessionId", sessionId}, {"data", data}};
}

ResizePayload ResizePayload::fromJson(const nlohmann::json& j) {
    ResizePayload p;
    p.sessionId = j.value("sessionId", "");
    p.cols = j.value("cols", 80);
    p.rows = j.value("rows", 24);
    return p;
}

nlohmann::json ResizePayload::toJson() const {
    return {{"sessionId", sessionId}, {"cols", cols}, {"rows", rows}};
}

RenamePayload RenamePayload::fromJson(const nlohmann::json& j) {
    RenamePayload p;
    p.sessionId = j.value("sessionId", "");
    p.newTitle = j.value("newTitle", "");
    return p;
}

nlohmann::json RenamePayload::toJson() const {
    return {{"sessionId", sessionId}, {"newTitle", newTitle}};
}

KillPayload KillPayload::fromJson(const nlohmann::json& j) {
    KillPayload p;
    p.sessionId = j.value("sessionId", "");
    return p;
}

nlohmann::json KillPayload::toJson() const {
    return {{"sessionId", sessionId}};
}

// ============================================================
// UUID generation
// ============================================================

std::string generateRequestId() {
    UUID uuid;
    if (SUCCEEDED(UuidCreate(&uuid))) {
        RPC_CSTR str = nullptr;
        if (SUCCEEDED(UuidToStringA(&uuid, &str)) && str != nullptr) {
            // UuidToStringA produces format like "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
            // but on some Windows versions it may omit hyphens.
            // Insert hyphens at standard UUID positions if missing.
            std::string result(reinterpret_cast<char*>(str));
            RpcStringFreeA(&str);
            for (auto& c : result) {
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            // Insert hyphens if the result is 32 hex chars (no hyphens)
            if (result.size() == 32) {
                result = result.substr(0, 8) + "-" +
                         result.substr(8, 4) + "-" +
                         result.substr(12, 4) + "-" +
                         result.substr(16, 4) + "-" +
                         result.substr(20, 12);
            }
            return result;
        }
    }
    // Fallback: timestamp + random
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::random_device rd;
    return "fallback-" + std::to_string(now) + "-" + std::to_string(rd());
}

std::string generateSessionId() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<u32> dist(0, 0xFFFFFFFF);
    char buf[64];
    snprintf(buf, sizeof(buf), "th_%lld_%08x",
             static_cast<long long>(ms), dist(gen));
    return buf;
}

} // namespace th::ipc
