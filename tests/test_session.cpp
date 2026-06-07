#include <gtest/gtest.h>
#include "terminalhub/Session/Session.hpp"
#include "terminalhub/PTY/ConPty.hpp"
#include "terminalhub/Core/Types.hpp"

using namespace th;

// === Session basic tests ===

TEST(Session, Construction) {
    SessionMetadata meta;
    meta.id = "th_test_123";
    meta.title = "Test Session";
    meta.shell = "powershell";
    meta.cwd = "C:\\";
    meta.pid = 1234;
    meta.createdAt = 1000;
    meta.lastActivityAt = 1000;
    meta.connectedClients = 0;

    Session session(std::move(meta), 100);
    EXPECT_EQ(session.metadata.id, "th_test_123");
    EXPECT_EQ(session.metadata.title, "Test Session");
    EXPECT_EQ(session.metadata.connectedClients, 0);
    EXPECT_EQ(session.outputBuffer.maxSize(), 100);
}

TEST(Session, AddRemoveClient) {
    SessionMetadata meta;
    meta.id = "test";
    Session session(std::move(meta), 50);

    session.addClient(1);
    session.addClient(2);
    EXPECT_EQ(session.metadata.connectedClients, 2);
    EXPECT_EQ(session.clients().size(), 2u);

    session.removeClient(1);
    EXPECT_EQ(session.metadata.connectedClients, 1);
    EXPECT_EQ(session.clients().size(), 1u);
    EXPECT_EQ(session.clients().count(2), 1u);

    // Duplicate add does not increase count
    session.addClient(2);
    EXPECT_EQ(session.metadata.connectedClients, 1);
}

TEST(Session, TouchUpdatesTimestamp) {
    SessionMetadata meta;
    meta.id = "test";
    meta.lastActivityAt = 0;
    Session session(std::move(meta), 50);

    session.touch();
    EXPECT_GT(session.metadata.lastActivityAt, 0);
}

TEST(Session, IsAliveWithoutPty) {
    SessionMetadata meta;
    meta.id = "test";
    Session session(std::move(meta), 50);

    // Without PTY process, isAlive returns false
    EXPECT_FALSE(session.isAlive());
}

TEST(Session, BroadcastOutputCallback) {
    SessionMetadata meta;
    meta.id = "test";
    Session session(std::move(meta), 50);

    std::string received;
    session.onOutput([&received](const std::string& data) {
        received = data;
    });

    session.broadcastOutput("hello world");
    EXPECT_EQ(received, "hello world");
}

TEST(Session, BroadcastExitCallback) {
    SessionMetadata meta;
    meta.id = "test";
    Session session(std::move(meta), 50);

    u32 exitCode = 0;
    session.onExit([&exitCode](u32 code) {
        exitCode = code;
    });

    session.broadcastExit(42);
    EXPECT_EQ(exitCode, 42u);
}

TEST(Session, PerClientOutputListeners) {
    SessionMetadata meta;
    meta.id = "test";
    Session session(std::move(meta), 50);

    std::string client1Received;
    std::string client2Received;
    session.addOutputListener(1, [&client1Received](const std::string& data) {
        client1Received = data;
    });
    session.addOutputListener(2, [&client2Received](const std::string& data) {
        client2Received = data;
    });

    session.broadcastOutput("hello");
    EXPECT_EQ(client1Received, "hello");
    EXPECT_EQ(client2Received, "hello");
}

TEST(Session, RemoveClientListeners) {
    SessionMetadata meta;
    meta.id = "test";
    Session session(std::move(meta), 50);

    std::string client1Received;
    std::string client2Received;
    session.addOutputListener(1, [&client1Received](const std::string& data) {
        client1Received = data;
    });
    session.addOutputListener(2, [&client2Received](const std::string& data) {
        client2Received = data;
    });

    session.addClient(1);
    session.addClient(2);

    session.removeClient(1);

    session.broadcastOutput("after remove");
    EXPECT_EQ(client1Received, "");  // Should not receive after removal
    EXPECT_EQ(client2Received, "after remove");
}

TEST(Session, OutputBufferIntegration) {
    SessionMetadata meta;
    meta.id = "test";
    Session session(std::move(meta), 100);

    session.outputBuffer.write("line1\nline2\nline3");
    auto lines = session.outputBuffer.getRecentLines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "line1");
    EXPECT_EQ(lines[1], "line2");
    EXPECT_EQ(lines[2], "line3");
}

// === SessionListItem default value tests ===

TEST(SessionListItem, DefaultValues) {
    SessionListItem item;
    EXPECT_EQ(item.pid, 0);
    EXPECT_EQ(item.createdAt, 0);
    EXPECT_EQ(item.lastActivityAt, 0);
    EXPECT_EQ(item.connectedClients, 0);
    EXPECT_FALSE(item.alive);
}

// === ConPty helper function tests ===

TEST(ConPty, GetDefaultShell) {
    EXPECT_EQ(getDefaultShell("powershell"), "powershell.exe");
    EXPECT_EQ(getDefaultShell("cmd"), "cmd.exe");
    EXPECT_EQ(getDefaultShell("bash"), "bash");
    EXPECT_EQ(getDefaultShell("unknown"), "powershell.exe");
    EXPECT_EQ(getDefaultShell(""), "powershell.exe");
}

// === ConPty creation tests ===

TEST(ConPty, CreateWithEmptyShellFails) {
    ConPtyOptions opts;
    opts.shell = "";
    auto result = ConPty::create(opts);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error().code(), Error::Code::PtyError);
}

TEST(ConPty, CreateWithInvalidSizeFails) {
    ConPtyOptions opts;
    opts.shell = "powershell.exe";
    opts.cols = 0;
    opts.rows = 24;
    auto result = ConPty::create(opts);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error().code(), Error::Code::PtyError);

    opts.cols = 80;
    opts.rows = -1;
    result = ConPty::create(opts);
    EXPECT_FALSE(result.success());
}

TEST(ConPty, CreateAndDestroy) {
    ConPtyOptions opts;
    opts.shell = "cmd.exe";
    opts.cols = 80;
    opts.rows = 24;

    auto result = ConPty::create(opts);
    ASSERT_TRUE(result.success());

    auto& pty = result.value();
    EXPECT_GT(pty->pid(), 0u);
    EXPECT_TRUE(pty->isAlive());

    pty->kill();
    // Wait for process to exit
    Sleep(500);
    EXPECT_FALSE(pty->isAlive());
}

TEST(ConPty, CreateAndReadOutput) {
    ConPtyOptions opts;
    opts.shell = "cmd.exe";
    opts.cols = 80;
    opts.rows = 24;

    auto result = ConPty::create(opts);
    ASSERT_TRUE(result.success());

    auto& pty = result.value();

    std::string output;
    pty->onOutput([&output](std::string_view data) {
        output += data;
    });

    // cmd.exe outputs some content after startup
    Sleep(1000);
    EXPECT_FALSE(output.empty());

    pty->kill();
    Sleep(500);
}

TEST(ConPty, Resize) {
    ConPtyOptions opts;
    opts.shell = "cmd.exe";
    opts.cols = 80;
    opts.rows = 24;

    auto result = ConPty::create(opts);
    ASSERT_TRUE(result.success());

    auto& pty = result.value();

    // Should not crash
    pty->resize(120, 40);
    pty->resize(40, 10);

    // Invalid parameters should be ignored
    pty->resize(0, 24);
    pty->resize(80, -1);

    pty->kill();
    Sleep(500);
}

TEST(ConPty, WriteInput) {
    ConPtyOptions opts;
    opts.shell = "cmd.exe";
    opts.cols = 80;
    opts.rows = 24;

    auto result = ConPty::create(opts);
    ASSERT_TRUE(result.success());

    auto& pty = result.value();

    std::string output;
    pty->onOutput([&output](std::string_view data) {
        output += data;
    });

    // Write command
    pty->write("echo hello\r\n");
    Sleep(1000);

    // Should see "hello" in output
    EXPECT_NE(output.find("hello"), std::string::npos);

    pty->kill();
    Sleep(500);
}
