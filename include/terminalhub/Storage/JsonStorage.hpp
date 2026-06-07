#pragma once

#include "terminalhub/Core/Result.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

namespace th {

/**
 * @brief Generic JSON file storage
 *
 * Provides read, write, update and delete operations on JSON files.
 * Automatically creates parent directories on write.
 *
 * @tparam T Data type to store (requires nlohmann_json from_json/to_json support)
 */
template<typename T>
class JsonStorage {
public:
    /**
     * @brief Construct JSON storage
     * @param filePath JSON file path
     */
    explicit JsonStorage(std::filesystem::path filePath)
        : m_filePath(std::move(filePath)) {
    }

    /**
     * @brief Read data
     * @return Data, or error if file not found or parse failure
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
     * @brief Write data
     * @param data Data to write
     * @return Success or error
     */
    Result<void> write(const T& data) {
        try {
            // Auto-create parent directories
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
     * @brief Read-modify-write
     * @param updater Receives current data (may be null), returns updated data
     * @return Success or error
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
     * @brief Delete the file
     * @return Success (also succeeds if file does not exist) or error
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
     * @brief Check if the file exists
     */
    [[nodiscard]] bool exists() const {
        return std::filesystem::exists(m_filePath);
    }

    /**
     * @brief Get the file path
     */
    [[nodiscard]] const std::filesystem::path& filePath() const {
        return m_filePath;
    }

private:
    std::filesystem::path m_filePath;
};

} // namespace th
