#include <gtest/gtest.h>
#include "terminalhub/Session/OutputBuffer.hpp"

using namespace th;

// === Basic read/write tests ===

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

    // "line1\nline2\n" → split into ["line1", "line2", ""]
    // The first two non-empty strings are written, the trailing empty string is also written (because lines.length !== 1)
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

    // "\n" → split into ["", ""] (2 elements), both are written
    buf.write("\n");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "");
    EXPECT_EQ(lines[1], "");
}

// === Ring buffer tests ===

TEST(OutputBuffer, RingBufferOverwrite) {
    OutputBuffer buf(3);

    buf.write("line1\nline2\nline3");
    EXPECT_EQ(buf.size(), 3);

    // Buffer is full, writing a new line overwrites the oldest
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

    // Continuous writes without newlines → multiple lines
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

    buf.write("a\nb\nc"); // Overwrite a
    EXPECT_EQ(buf.size(), 2);

    buf.clear();
    EXPECT_EQ(buf.size(), 0);

    buf.write("new");
    EXPECT_EQ(buf.size(), 1);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "new");
}

// === Key tests aligned with original TypeScript behavior ===

TEST(OutputBuffer, EmptyStringSkipped) {
    // Original TS: if (line === "" && lines.length === 1) continue;
    // Writing an empty string alone produces no lines
    OutputBuffer buf(5);

    buf.write("");
    EXPECT_EQ(buf.size(), 0);
}

TEST(OutputBuffer, NewlineProducesTwoEmptyLines) {
    // Original TS: "\n".split("\n") → ["", ""]
    // lines.length === 2, not 1, so empty strings are also written
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
    // lines.length === 2, empty string is written
    OutputBuffer buf(5);

    buf.write("hello\n");
    EXPECT_EQ(buf.size(), 2);

    auto lines = buf.getRecentLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "hello");
    EXPECT_EQ(lines[1], "");
}
