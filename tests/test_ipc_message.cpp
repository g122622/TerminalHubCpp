#include <gtest/gtest.h>
#include "terminalhub/IPC/IpcMessage.hpp"

using namespace th::ipc;

// === CommandType 转换 ===

TEST(IpcMessage, CommandTypeToString) {
    EXPECT_EQ(commandTypeToString(CommandType::List), "list");
    EXPECT_EQ(commandTypeToString(CommandType::New), "new");
    EXPECT_EQ(commandTypeToString(CommandType::Attach), "attach");
    EXPECT_EQ(commandTypeToString(CommandType::Detach), "detach");
    EXPECT_EQ(commandTypeToString(CommandType::Kill), "kill");
    EXPECT_EQ(commandTypeToString(CommandType::Rename), "rename");
    EXPECT_EQ(commandTypeToString(CommandType::Input), "input");
    EXPECT_EQ(commandTypeToString(CommandType::Resize), "resize");
    EXPECT_EQ(commandTypeToString(CommandType::Shutdown), "shutdown");
}

TEST(IpcMessage, CommandTypeFromString) {
    EXPECT_EQ(commandTypeFromString("list"), CommandType::List);
    EXPECT_EQ(commandTypeFromString("new"), CommandType::New);
    EXPECT_EQ(commandTypeFromString("attach"), CommandType::Attach);
    EXPECT_EQ(commandTypeFromString("kill"), CommandType::Kill);
    EXPECT_EQ(commandTypeFromString("rename"), CommandType::Rename);
    EXPECT_EQ(commandTypeFromString("input"), CommandType::Input);
    EXPECT_EQ(commandTypeFromString("resize"), CommandType::Resize);
    EXPECT_EQ(commandTypeFromString("shutdown"), CommandType::Shutdown);
    EXPECT_EQ(commandTypeFromString("unknown"), CommandType::List); // 默认
}

// === EventType 转换 ===

TEST(IpcMessage, EventTypeToString) {
    EXPECT_EQ(eventTypeToString(EventType::Output), "output");
    EXPECT_EQ(eventTypeToString(EventType::Exit), "exit");
    EXPECT_EQ(eventTypeToString(EventType::SessionAdded), "session_added");
    EXPECT_EQ(eventTypeToString(EventType::SessionRemoved), "session_removed");
    EXPECT_EQ(eventTypeToString(EventType::Error), "error");
}

TEST(IpcMessage, EventTypeFromString) {
    EXPECT_EQ(eventTypeFromString("output"), EventType::Output);
    EXPECT_EQ(eventTypeFromString("exit"), EventType::Exit);
    EXPECT_EQ(eventTypeFromString("session_added"), EventType::SessionAdded);
    EXPECT_EQ(eventTypeFromString("session_removed"), EventType::SessionRemoved);
    EXPECT_EQ(eventTypeFromString("error"), EventType::Error);
}

// === IpcRequest 序列化 ===

TEST(IpcMessage, RequestSerialize) {
    IpcRequest req;
    req.id = "test-123";
    req.command = CommandType::New;
    req.payload = {{"title", "my session"}, {"shell", "powershell"}};

    std::string json = req.serialize();

    EXPECT_NE(json.find("\"type\":\"request\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"test-123\""), std::string::npos);
    EXPECT_NE(json.find("\"command\":\"new\""), std::string::npos);
    EXPECT_NE(json.find("\"title\":\"my session\""), std::string::npos);
}

TEST(IpcMessage, RequestDeserialize) {
    std::string json = R"({"type":"request","id":"abc","command":"list","payload":{}})";

    auto req = IpcRequest::deserialize(json);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->id, "abc");
    EXPECT_EQ(req->command, CommandType::List);
}

TEST(IpcMessage, RequestDeserializeInvalid) {
    EXPECT_FALSE(IpcRequest::deserialize("not json").has_value());
    EXPECT_FALSE(IpcRequest::deserialize(R"({"type":"response"})").has_value());
}

// === IpcResponse 序列化 ===

TEST(IpcMessage, ResponseSerializeSuccess) {
    IpcResponse resp;
    resp.id = "req-1";
    resp.success = true;
    resp.data = {{"sessionId", "th_123_abc"}, {"title", "test"}};

    std::string json = resp.serialize();

    EXPECT_NE(json.find("\"type\":\"response\""), std::string::npos);
    EXPECT_NE(json.find("\"success\":true"), std::string::npos);
    EXPECT_NE(json.find("\"sessionId\""), std::string::npos);
}

TEST(IpcMessage, ResponseSerializeError) {
    IpcResponse resp;
    resp.id = "req-1";
    resp.success = false;
    resp.error = "会话不存在";

    std::string json = resp.serialize();

    EXPECT_NE(json.find("\"success\":false"), std::string::npos);
    EXPECT_NE(json.find("会话不存在"), std::string::npos);
}

TEST(IpcMessage, ResponseDeserializeSuccess) {
    std::string json = R"({"type":"response","id":"req-1","success":true,"data":{"result":42}})";

    auto resp = IpcResponse::deserialize(json);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->id, "req-1");
    EXPECT_TRUE(resp->success);
    EXPECT_EQ(resp->data["result"], 42);
}

TEST(IpcMessage, ResponseDeserializeError) {
    std::string json = R"({"type":"response","id":"req-1","success":false,"error":"未知命令"})";

    auto resp = IpcResponse::deserialize(json);
    ASSERT_TRUE(resp.has_value());
    EXPECT_FALSE(resp->success);
    EXPECT_EQ(resp->error, "未知命令");
}

// === IpcEvent 序列化 ===

TEST(IpcMessage, EventSerialize) {
    IpcEvent evt;
    evt.eventType = EventType::Output;
    evt.sessionId = "th_123";
    evt.data = "hello world";

    std::string json = evt.serialize();

    EXPECT_NE(json.find("\"type\":\"event\""), std::string::npos);
    EXPECT_NE(json.find("\"eventType\":\"output\""), std::string::npos);
    EXPECT_NE(json.find("\"sessionId\":\"th_123\""), std::string::npos);
}

TEST(IpcMessage, EventDeserialize) {
    std::string json = R"({"type":"event","eventType":"exit","sessionId":"th_456","data":{"code":0}})";

    auto evt = IpcEvent::deserialize(json);
    ASSERT_TRUE(evt.has_value());
    EXPECT_EQ(evt->eventType, EventType::Exit);
    EXPECT_EQ(evt->sessionId, "th_456");
    EXPECT_EQ(evt->data["code"], 0);
}

// === Payload 序列化 ===

TEST(IpcMessage, NewSessionPayloadRoundTrip) {
    NewSessionPayload p;
    p.title = "test session";
    p.cwd = "C:\\Users";
    p.shell = "powershell";
    p.cols = 120;
    p.rows = 30;

    nlohmann::json j = p.toJson();
    auto p2 = NewSessionPayload::fromJson(j);

    EXPECT_EQ(p2.title.value(), "test session");
    EXPECT_EQ(p2.cwd.value(), "C:\\Users");
    EXPECT_EQ(p2.shell.value(), "powershell");
    EXPECT_EQ(p2.cols.value(), 120);
    EXPECT_EQ(p2.rows.value(), 30);
}

TEST(IpcMessage, NewSessionPayloadEmpty) {
    nlohmann::json j = {};
    auto p = NewSessionPayload::fromJson(j);

    EXPECT_FALSE(p.title.has_value());
    EXPECT_FALSE(p.cwd.has_value());
    EXPECT_FALSE(p.shell.has_value());
    EXPECT_FALSE(p.cols.has_value());
    EXPECT_FALSE(p.rows.has_value());
}

TEST(IpcMessage, AttachSessionPayloadRoundTrip) {
    AttachSessionPayload p;
    p.sessionId = "th_123";
    p.cols = 100;
    p.rows = 25;

    nlohmann::json j = p.toJson();
    auto p2 = AttachSessionPayload::fromJson(j);

    EXPECT_EQ(p2.sessionId, "th_123");
    EXPECT_EQ(p2.cols.value(), 100);
    EXPECT_EQ(p2.rows.value(), 25);
}

TEST(IpcMessage, InputPayloadRoundTrip) {
    InputPayload p;
    p.sessionId = "th_456";
    p.data = "ls -la\n";

    nlohmann::json j = p.toJson();
    auto p2 = InputPayload::fromJson(j);

    EXPECT_EQ(p2.sessionId, "th_456");
    EXPECT_EQ(p2.data, "ls -la\n");
}

TEST(IpcMessage, ResizePayloadRoundTrip) {
    ResizePayload p;
    p.sessionId = "th_789";
    p.cols = 200;
    p.rows = 50;

    nlohmann::json j = p.toJson();
    auto p2 = ResizePayload::fromJson(j);

    EXPECT_EQ(p2.sessionId, "th_789");
    EXPECT_EQ(p2.cols, 200);
    EXPECT_EQ(p2.rows, 50);
}

TEST(IpcMessage, RenamePayloadRoundTrip) {
    RenamePayload p;
    p.sessionId = "th_abc";
    p.newTitle = "renamed";

    nlohmann::json j = p.toJson();
    auto p2 = RenamePayload::fromJson(j);

    EXPECT_EQ(p2.sessionId, "th_abc");
    EXPECT_EQ(p2.newTitle, "renamed");
}

TEST(IpcMessage, KillPayloadRoundTrip) {
    KillPayload p;
    p.sessionId = "th_xyz";

    nlohmann::json j = p.toJson();
    auto p2 = KillPayload::fromJson(j);

    EXPECT_EQ(p2.sessionId, "th_xyz");
}

// === UUID 生成 ===

TEST(IpcMessage, GenerateRequestId) {
    std::string id1 = generateRequestId();
    std::string id2 = generateRequestId();

    EXPECT_FALSE(id1.empty());
    EXPECT_FALSE(id2.empty());
    EXPECT_NE(id1, id2); // 应该是唯一的
}

TEST(IpcMessage, GenerateSessionId) {
    std::string id1 = generateSessionId();
    std::string id2 = generateSessionId();

    EXPECT_NE(id1, id2);
    // 格式: th_{timestamp}_{hex}
    EXPECT_EQ(id1.substr(0, 3), "th_");
    EXPECT_NE(id1.find('_', 3), std::string::npos);
}
