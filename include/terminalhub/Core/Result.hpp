#pragma once

#include "Error.hpp"

#include <string>
#include <variant>

namespace th {

/**
 * @brief Result 类型，用于替代异常的错误处理
 *
 * 使用示例：
 *   Result<int> result = someFunction();
 *   if (!result.success()) {
 *       // 处理错误
 *       return result; // 传播错误
 *   }
 *   int value = result.value();
 *
 *   // 使用 TRY 宏传播错误
 *   TRY(someFunction());
 */
template<typename T>
class Result {
public:
    // 成功构造
    static Result ok(T value) {
        Result r;
        r.m_data = std::move(value);
        return r;
    }

    // 错误构造
    static Result err(Error error) {
        Result r;
        r.m_data = std::move(error);
        return r;
    }

    // 便捷：从错误码和消息构造
    static Result err(Error::Code code, std::string message, std::string source = "") {
        return err(Error(code, std::move(message), std::move(source)));
    }

    [[nodiscard]] bool success() const {
        return std::holds_alternative<T>(m_data);
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(m_data);
    }

    [[nodiscard]] T& value() {
        return std::get<T>(m_data);
    }

    [[nodiscard]] const Error& error() const {
        return std::get<Error>(m_data);
    }

    [[nodiscard]] Error& error() {
        return std::get<Error>(m_data);
    }

private:
    Result() = default;
    std::variant<T, Error> m_data;
};

// Result<void> 特化
template<>
class Result<void> {
public:
    static Result ok() {
        Result r;
        r.m_success = true;
        return r;
    }

    static Result err(Error error) {
        Result r;
        r.m_success = false;
        r.m_error = std::move(error);
        return r;
    }

    static Result err(Error::Code code, std::string message, std::string source = "") {
        return err(Error(code, std::move(message), std::move(source)));
    }

    [[nodiscard]] bool success() const {
        return m_success;
    }

    [[nodiscard]] const Error& error() const {
        return m_error;
    }

private:
    Result() = default;
    bool m_success{false};
    Error m_error;
};

} // namespace th

/**
 * @brief TRY 宏：如果表达式返回错误，立即传播错误
 *
 * 使用示例：
 *   Result<void> result = TRY(someFunction());
 *   Result<int> value = TRY(getValue());
 */
#define TRY(expr)                                                                                                      \
    do {                                                                                                               \
        auto _result = (expr);                                                                                         \
        if (!_result.success()) {                                                                                      \
            return decltype(_result)::err(std::move(_result.error()));                                                 \
        }                                                                                                              \
    } while (0)

/**
 * @brief TRY_VALUE 宏：提取 Result 中的值，如果错误则传播
 *
 * 使用示例：
 *   auto value = TRY_VALUE(getValue());
 */
#define TRY_VALUE(expr)                                                                                                \
    ({                                                                                                                 \
        auto _result = (expr);                                                                                         \
        if (!_result.success()) {                                                                                      \
            return decltype(_result)::err(std::move(_result.error()));                                                 \
        }                                                                                                              \
        std::move(_result.value());                                                                                    \
    })
