#include "terminalhub/Session/OutputBuffer.hpp"

namespace th {

OutputBuffer::OutputBuffer(i32 maxSize)
    : m_maxSize(maxSize) {
    m_buffer.resize(static_cast<size_t>(m_maxSize));
    m_cursor = 0;
    m_count = 0;
}

void OutputBuffer::write(std::string_view data) {
    if (data.empty()) {
        return;
    }

    // Match original TypeScript behavior: split by \n, write each substring as a line
    // TS data.split("\n") behavior:
    //   "a\nb"     -> ["a", "b"]      (2 lines)
    //   "a\nb\n"   -> ["a", "b", ""]  (3 lines, trailing empty string is written)
    //   "\n"       -> ["", ""]        (2 lines)
    //   "single"   -> ["single"]      (1 line)
    //   ""         -> skipped (original: if (line === "" && lines.length === 1) continue)

    size_t start = 0;
    size_t pos = 0;
    i32 lineCount = 0;

    while ((pos = data.find('\n', start)) != std::string_view::npos) {
        std::string_view line = data.substr(start, pos - start);
        m_buffer[static_cast<size_t>(m_cursor)] = std::string(line);
        m_cursor = (m_cursor + 1) % m_maxSize;
        if (m_count < m_maxSize) {
            m_count++;
        }
        lineCount++;
        start = pos + 1;
    }

    // Handle remaining part after last \n
    std::string_view remaining = data.substr(start);

    // Original TS key logic:
    // if (line === "" && lines.length === 1) continue;
    // i.e. only skip when entire input has no \n and is empty string
    // Other cases (empty from \n, or non-empty trailing) are written
    if (!remaining.empty()) {
        m_buffer[static_cast<size_t>(m_cursor)] = std::string(remaining);
        m_cursor = (m_cursor + 1) % m_maxSize;
        if (m_count < m_maxSize) {
            m_count++;
        }
    } else if (lineCount > 0) {
        // Data ends with \n, trailing empty string needs to be written
        // This matches TS split behavior: "a\nb\n" -> ["a", "b", ""]
        m_buffer[static_cast<size_t>(m_cursor)] = "";
        m_cursor = (m_cursor + 1) % m_maxSize;
        if (m_count < m_maxSize) {
            m_count++;
        }
    }
    // else: remaining is empty and lineCount == 0
    // i.e. data is empty string, already returned at the beginning
}

std::vector<std::string> OutputBuffer::getRecentLines(i32 n) const {
    std::vector<std::string> lines;
    i32 targetCount = (n <= 0) ? m_count : std::min(n, m_count);

    // Calculate start position: begin from the oldest line in targetCount
    // When n < count, need to skip count - n older lines
    i32 start = 0;
    if (m_count >= m_maxSize) {
        // Buffer is full, cursor points to the oldest line
        start = m_cursor;
    }
    // Skip excess older lines, only take the most recent targetCount lines
    i32 skip = m_count - targetCount;
    start = (start + skip) % m_maxSize;

    for (i32 i = 0; i < targetCount; i++) {
        i32 index = (start + i) % m_maxSize;
        lines.push_back(m_buffer[static_cast<size_t>(index)]);
    }

    return lines;
}

std::string OutputBuffer::getAll() const {
    auto lines = getRecentLines();
    std::string result;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) {
            result += '\n';
        }
        result += lines[i];
    }
    return result;
}

void OutputBuffer::clear() {
    for (auto& slot : m_buffer) {
        slot.clear();
    }
    m_cursor = 0;
    m_count = 0;
}

i32 OutputBuffer::size() const {
    return m_count;
}

i32 OutputBuffer::maxSize() const {
    return m_maxSize;
}

} // namespace th
