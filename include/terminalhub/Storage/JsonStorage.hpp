#pragma once

#include "terminalhub/Core/Result.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

namespace th {

/**
 * @brief 通用 JSON 文件存储
 *
 * 提供对 JSON 文件的读写、更新和删除操作。
 * 写入时自动创建父目录。
 *
 * @tparam T 存储的数据类型（需要 nlohmann_json 的 from_json/to_json 支持）
 */
template<typename T>
class JsonStorage {
public:
    /**
     * @brief 构造 JSON 存储
     * @param filePath JSON 文件路径
     */
    explicit JsonStorage(std::filesystem::path filePath)
        : m_filePath(std::move(filePath)) {
    }

    /**
     * @brief 读取数据
     * @return 数据，文件不存在或解析失败时返回错误
     */
    [[nodiscard]] Result<T> read() const {
        if (!std::filesystem::exists(m_filePath)) {
            return Result<T>::err(Error::notFound(
                "File not found: " + m_filePath.string(),
                "JsonStorage::read"));
        }

        try {
            std::ifstream file(m_filePath);
            if (!file.is_open()) {
                return Result<T>::err(Error::ioError(
                    "Failed to open file: " + m_filePath.string(),
                    "JsonStorage::read"));
            }

            nlohmann::json j = nlohmann::json::parse(file);
            return Result<T>::ok(j.get<T>());
        } catch (const nlohmann::json::parse_error& e) {
            return Result<T>::err(Error::ioError(
                "JSON parse error in " + m_filePath.string() + ": " + e.what(),
                "JsonStorage::read"));
        } catch (const std::exception& e) {
            return Result<T>::err(Error::ioError(
                "Failed to read " + m_filePath.string() + ": " + e.what(),
                "JsonStorage::read"));
        }
    }

    /**
     * @brief 写入数据
     * @param data 要写入的数据
     * @return 成功或错误
     */
    Result<void> write(const T& data) {
        try {
            // 自动创建父目录
            auto parentDir = m_filePath.parent_path();
            if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
                std::filesystem::create_directories(parentDir);
            }

            nlohmann::json j = data;
            std::ofstream file(m_filePath);
            if (!file.is_open()) {
                return Result<void>::err(Error::ioError(
                    "Failed to open file for writing: " + m_filePath.string(),
                    "JsonStorage::write"));
            }

            file << j.dump(2);
            return Result<void>::ok();
        } catch (const std::exception& e) {
            return Result<void>::err(Error::ioError(
                "Failed to write " + m_filePath.string() + ": " + e.what(),
                "JsonStorage::write"));
        }
    }

    /**
     * @brief 读取-修改-写入
     * @param updater 接收当前数据（可能为空），返回更新后的数据
     * @return 成功或错误
     */
    Result<void> update(std::function<T(const T*)> updater) {
        T current{};
        auto readResult = read();
        bool hasCurrent = readResult.success();
        if (hasCurrent) {
            current = std::move(readResult.value());
        }

        T updated = updater(hasCurrent ? &current : nullptr);
        return write(updated);
    }

    /**
     * @brief 删除文件
     * @return 成功（文件不存在也返回成功）或错误
     */
    Result<void> deleteFile() {
        try {
            if (std::filesystem::exists(m_filePath)) {
                std::filesystem::remove(m_filePath);
            }
            return Result<void>::ok();
        } catch (const std::exception& e) {
            return Result<void>::err(Error::ioError(
                "Failed to delete " + m_filePath.string() + ": " + e.what(),
                "JsonStorage::deleteFile"));
        }
    }

    /**
     * @brief 检查文件是否存在
     */
    [[nodiscard]] bool exists() const {
        return std::filesystem::exists(m_filePath);
    }

    /**
     * @brief 获取文件路径
     */
    [[nodiscard]] const std::filesystem::path& filePath() const {
        return m_filePath;
    }

private:
    std::filesystem::path m_filePath;
};

} // namespace th
