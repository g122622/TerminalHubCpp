#include <gtest/gtest.h>
#include "terminalhub/Session/OutputBuffer.hpp"

using namespace th;

// === 基本读写测试 ===

TEST(OutputBuffer, BasicWriteAndRead) {
    OutputBuffer buf(5);

    buf.write("hello\nworld");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "hello");
    EXPECT_EQ(lines[1], "world");
}

TEST(OutputBuffer, WriteWithTrailingNewline) {
    OutputBuffer buf(5);

    // "line1\nline2\n" → split 为 ["line1", "line2", ""]
    // 前两个非空写入，尾部空串也写入（因为 lines.length !== 1）
    buf.write("line1\nline2\n");
    EXPECT_EQ(buf.size(), 3);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "line1");
    EXPECT_EQ(lines[1], "line2");
    EXPECT_EQ(lines[2], "");
}

TEST(OutputBuffer, WriteEmptyString) {
    OutputBuffer buf(5);

    buf.write("");
    EXPECT_EQ(buf.size(), 0);
}

TEST(OutputBuffer, WriteOnlyNewline) {
    OutputBuffer buf(5);

    // "\n" → split 为 ["", ""]（2个元素），都写入
    buf.write("\n");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "");
    EXPECT_EQ(lines[1], "");
}

// === 环形缓冲区测试 ===

TEST(OutputBuffer, RingBufferOverwrite) {
    OutputBuffer buf(3);

    buf.write("line1\nline2\nline3");
    EXPECT_EQ(buf.size(), 3);

    // 缓冲区已满，写入新行会覆盖最旧行
    buf.write("\nline4");
    EXPECT_EQ(buf.size(), 3);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "line3");
    EXPECT_EQ(lines[1], "");
    EXPECT_EQ(lines[2], "line4");
}

TEST(OutputBuffer, GetRecentLinesWithLimit) {
    OutputBuffer buf(10);

    buf.write("line1\nline2\nline3\nline4\nline5");
    EXPECT_EQ(buf.size(), 5);

    auto lines = buf.getRecentLines(3);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "line3");
    EXPECT_EQ(lines[1], "line4");
    EXPECT_EQ(lines[2], "line5");
}

TEST(OutputBuffer, GetRecentLinesMoreThanAvailable) {
    OutputBuffer buf(10);

    buf.write("line1\nline2");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines(5);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "line1");
    EXPECT_EQ(lines[1], "line2");
}

TEST(OutputBuffer, GetAll) {
    OutputBuffer buf(10);

    buf.write("hello\nworld");
    std::string all = buf.getAll();
    EXPECT_EQ(all, "hello\nworld");
}

TEST(OutputBuffer, Clear) {
    OutputBuffer buf(5);

    buf.write("line1\nline2");
    EXPECT_EQ(buf.size(), 2);

    buf.clear();
    EXPECT_EQ(buf.size(), 0);

    auto lines = buf.getRecentLines();
    EXPECT_TRUE(lines.empty());
}

TEST(OutputBuffer, ClearAndReuse) {
    OutputBuffer buf(3);

    buf.write("a\nb\nc");
    buf.clear();
    EXPECT_EQ(buf.size(), 0);

    buf.write("x\ny");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "x");
    EXPECT_EQ(lines[1], "y");
}

TEST(OutputBuffer, MultipleWrites) {
    OutputBuffer buf(10);

    // 连续写入无换行 → 多行
    buf.write("line1");
    buf.write("line2");
    buf.write("line3");
    EXPECT_EQ(buf.size(), 3);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "line1");
    EXPECT_EQ(lines[1], "line2");
    EXPECT_EQ(lines[2], "line3");
}

TEST(OutputBuffer, SingleLineNoNewline) {
    OutputBuffer buf(5);

    buf.write("single line");
    EXPECT_EQ(buf.size(), 1);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "single line");
}

TEST(OutputBuffer, MaxSizeAccessor) {
    OutputBuffer buf(42);
    EXPECT_EQ(buf.maxSize(), 42);
}

TEST(OutputBuffer, RingBufferFullCycle) {
    OutputBuffer buf(3);

    buf.write("a\nb\nc\nd\ne");
    EXPECT_EQ(buf.size(), 3);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "c");
    EXPECT_EQ(lines[1], "d");
    EXPECT_EQ(lines[2], "e");
}

TEST(OutputBuffer, GetAllAfterOverwrite) {
    OutputBuffer buf(3);

    buf.write("a\nb\nc\nd");
    EXPECT_EQ(buf.getAll(), "b\nc\nd");
}

TEST(OutputBuffer, GetRecentLinesZeroReturnsAll) {
    OutputBuffer buf(10);

    buf.write("a\nb\nc");
    auto lines = buf.getRecentLines(0);
    ASSERT_EQ(lines.size(), 3u);
}

TEST(OutputBuffer, OverwriteThenClearThenWrite) {
    OutputBuffer buf(2);

    buf.write("a\nb\nc"); // 覆盖 a
    EXPECT_EQ(buf.size(), 2);

    buf.clear();
    EXPECT_EQ(buf.size(), 0);

    buf.write("new");
    EXPECT_EQ(buf.size(), 1);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "new");
}

// === 与原版 TypeScript 行为对齐的关键测试 ===

TEST(OutputBuffer, EmptyStringSkipped) {
    // 原版 TS: if (line === "" && lines.length === 1) continue;
    // 单独写入空字符串不产生任何行
    OutputBuffer buf(5);

    buf.write("");
    EXPECT_EQ(buf.size(), 0);
}

TEST(OutputBuffer, NewlineProducesTwoEmptyLines) {
    // 原版 TS: "\n".split("\n") → ["", ""]
    // lines.length === 2, 不是 1, 所以空串也写入
    OutputBuffer buf(5);

    buf.write("\n");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "");
    EXPECT_EQ(lines[1], "");
}

TEST(OutputBuffer, TrailingNewlineProducesEmptyLine) {
    // "hello\n".split("\n") → ["hello", ""]
    // lines.length === 2, 空串写入
    OutputBuffer buf(5);

    buf.write("hello\n");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "hello");
    EXPECT_EQ(lines[1], "");
}
