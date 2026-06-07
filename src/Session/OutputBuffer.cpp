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

    // 与原版 TypeScript 行为一致：按 \n 分割，每个子串作为一行写入
    // TS 的 data.split("\n") 行为：
    //   "a\nb"     → ["a", "b"]      （2行）
    //   "a\nb\n"   → ["a", "b", ""]  （3行，尾部空串也写入）
    //   "\n"       → ["", ""]        （2行）
    //   "single"   → ["single"]      （1行）
    //   ""         → 跳过（原版 if (line === "" && lines.length === 1) continue）

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

    // 处理 \n 之后的剩余部分
    std::string_view remaining = data.substr(start);

    // 原版 TS 的关键逻辑：
    // if (line === "" && lines.length === 1) continue;
    // 即：只有当整个输入不含 \n 且为空字符串时才跳过
    // 其他情况（含 \n 产生的空串，或非空尾部）都要写入
    if (!remaining.empty()) {
        m_buffer[static_cast<size_t>(m_cursor)] = std::string(remaining);
        m_cursor = (m_cursor + 1) % m_maxSize;
        if (m_count < m_maxSize) {
            m_count++;
        }
    } else if (lineCount > 0) {
        // 数据以 \n 结尾，尾部空串需要写入
        // 这与 TS split 的行为一致："a\nb\n" → ["a", "b", ""]
        m_buffer[static_cast<size_t>(m_cursor)] = "";
        m_cursor = (m_cursor + 1) % m_maxSize;
        if (m_count < m_maxSize) {
            m_count++;
        }
    }
    // else: remaining 为空且 lineCount == 0
    // 即 data 是空字符串，已在开头 return
}

std::vector<std::string> OutputBuffer::getRecentLines(i32 n) const {
    std::vector<std::string> lines;
    i32 targetCount = (n <= 0) ? m_count : std::min(n, m_count);

    // 计算起始位置：从最近 targetCount 行中最旧的那行开始
    // 当 n < count 时，需要跳过 count - n 行
    i32 start = 0;
    if (m_count >= m_maxSize) {
        // 缓冲区已满，cursor 指向最旧行
        start = m_cursor;
    }
    // 跳过多余的旧行，只取最近 targetCount 行
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
