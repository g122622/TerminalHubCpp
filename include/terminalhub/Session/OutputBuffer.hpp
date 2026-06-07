#pragma once

#include "terminalhub/Core/Types.hpp"

#include <string>
#include <vector>

namespace th {

/**
 * @brief Ring buffer for storing session output history
 *
 * Stores data split by \n, overwriting the oldest lines when full.
 * Reads start from the oldest line, preserving chronological order.
 */
class OutputBuffer {
public:
    /**
     * @brief Construct a ring buffer
     * @param maxSize Maximum number of lines, must be positive
     */
    explicit OutputBuffer(i32 maxSize);

    /**
     * @brief Write data, split by \n into lines
     * @param data Raw output data
     */
    void write(std::string_view data);

    /**
     * @brief Get the most recent N lines
     * @param n Number of lines, 0 means all
     * @return List of lines from oldest to newest
     */
    [[nodiscard]] std::vector<std::string> getRecentLines(i32 n = 0) const;

    /**
     * @brief Get all content joined by \n
     */
    [[nodiscard]] std::string getAll() const;

    /**
     * @brief Clear the buffer
     */
    void clear();

    /**
     * @brief Get the current number of lines
     */
    [[nodiscard]] i32 size() const;

    /**
     * @brief Get the maximum number of lines
     */
    [[nodiscard]] i32 maxSize() const;

private:
    std::vector<std::string> m_buffer;
    i32 m_maxSize;
    i32 m_cursor{0};
    i32 m_count{0};
};

} // namespace th
