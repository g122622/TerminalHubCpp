#pragma once

#include "terminalhub/Core/Types.hpp"

#include <string>
#include <vector>

namespace th {

/**
 * @brief 环形缓冲区，用于存储会话输出历史
 *
 * 按 \n 分行存储，当缓冲区满时覆盖最旧的行。
 * 读取时从最旧行开始，保持时间顺序。
 */
class OutputBuffer {
public:
    /**
     * @brief 构造环形缓冲区
     * @param maxSize 最大行数，必须为正整数
     */
    explicit OutputBuffer(i32 maxSize);

    /**
     * @brief 写入数据，按 \n 分行存储
     * @param data 原始输出数据
     */
    void write(std::string_view data);

    /**
     * @brief 获取最近 N 行
     * @param n 行数，0 表示获取全部
     * @return 从最旧到最新的行列表
     */
    [[nodiscard]] std::vector<std::string> getRecentLines(i32 n = 0) const;

    /**
     * @brief 获取所有内容，用 \n 连接
     */
    [[nodiscard]] std::string getAll() const;

    /**
     * @brief 清空缓冲区
     */
    void clear();

    /**
     * @brief 获取当前行数
     */
    [[nodiscard]] i32 size() const;

    /**
     * @brief 获取最大行数
     */
    [[nodiscard]] i32 maxSize() const;

private:
    std::vector<std::string> m_buffer;
    i32 m_maxSize;
    i32 m_cursor{0};
    i32 m_count{0};
};

} // namespace th
