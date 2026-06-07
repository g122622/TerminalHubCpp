#pragma once

#include "Error.hpp"

#include <string>
#include <variant>

namespace th {

/**
 * @brief Result type for error handling replacing exceptions
 *
 * Usage example:
 *   Result<int> result = someFunction();
 *   if (!result.success()) {
 *       // handle error
 *       return result; // propagate error
 *   }
 *   int value = result.value();
 *
 *   // Using TRY macro to propagate errors
 *   TRY(someFunction());
 */
template<typename T>
class Result {
public:
    // Success construction
    static Result ok(T value) {
        Result r;
        r.m_data = std::move(value);
        return r;
    }

    // Error construction
    static Result err(Error error) {
        Result r;
        r.m_data = std::move(error);
        return r;
    }

    // Convenience: construct from error code and message
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

// Result<void> specialization
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
 * @brief TRY macro: if the expression returns an error, immediately propagate it
 *
 * Usage example:
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
 * @brief TRY_VALUE macro: extract the value from a Result, propagate if error
 *
 * Usage example:
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
