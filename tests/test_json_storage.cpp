#include <gtest/gtest.h>
#include "terminalhub/Storage/JsonStorage.hpp"
#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Core/Types.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace th;

// 测试用的简单数据结构
struct TestData {
    std::string name;
    i32 value{0};

    // nlohmann_json 序列化支持
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TestData, name, value)
};

class JsonStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_testDir = std::filesystem::temp_directory_path() / "terminalhub_test_json";
        std::filesystem::create_directories(m_testDir);
        m_filePath = m_testDir / "test.json";
    }

    void TearDown() override {
        std::filesystem::remove_all(m_testDir);
    }

    std::filesystem::path m_testDir;
    std::filesystem::path m_filePath;
};

TEST_F(JsonStorageTest, WriteAndRead) {
    JsonStorage<TestData> storage(m_filePath);

    TestData data{"hello", 42};
    auto writeResult = storage.write(data);
    EXPECT_TRUE(writeResult.success());

    auto readResult = storage.read();
    EXPECT_TRUE(readResult.success());
    EXPECT_EQ(readResult.value().name, "hello");
    EXPECT_EQ(readResult.value().value, 42);
}

TEST_F(JsonStorageTest, ReadNonExistent) {
    JsonStorage<TestData> storage(m_filePath);

    auto result = storage.read();
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error().code(), Error::Code::NotFound);
}

TEST_F(JsonStorageTest, Exists) {
    JsonStorage<TestData> storage(m_filePath);

    EXPECT_FALSE(storage.exists());

    TestData data{"test", 1};
    storage.write(data);

    EXPECT_TRUE(storage.exists());
}

TEST_F(JsonStorageTest, DeleteFile) {
    JsonStorage<TestData> storage(m_filePath);

    TestData data{"test", 1};
    storage.write(data);
    EXPECT_TRUE(storage.exists());

    auto result = storage.deleteFile();
    EXPECT_TRUE(result.success());
    EXPECT_FALSE(storage.exists());
}

TEST_F(JsonStorageTest, DeleteNonExistent) {
    JsonStorage<TestData> storage(m_filePath);

    // 删除不存在的文件应该成功
    auto result = storage.deleteFile();
    EXPECT_TRUE(result.success());
}

TEST_F(JsonStorageTest, Update) {
    JsonStorage<TestData> storage(m_filePath);

    // 初始写入
    TestData data{"initial", 10};
    storage.write(data);

    // 更新
    auto updateResult = storage.update([](const TestData* current) -> TestData {
        TestData updated;
        if (current != nullptr) {
            updated.name = current->name + "_updated";
            updated.value = current->value + 5;
        }
        return updated;
    });
    EXPECT_TRUE(updateResult.success());

    // 验证
    auto readResult = storage.read();
    EXPECT_TRUE(readResult.success());
    EXPECT_EQ(readResult.value().name, "initial_updated");
    EXPECT_EQ(readResult.value().value, 15);
}

TEST_F(JsonStorageTest, UpdateWithNoExistingData) {
    JsonStorage<TestData> storage(m_filePath);

    auto updateResult = storage.update([](const TestData* current) -> TestData {
        EXPECT_EQ(current, nullptr);
        return TestData{"fresh", 99};
    });
    EXPECT_TRUE(updateResult.success());

    auto readResult = storage.read();
    EXPECT_TRUE(readResult.success());
    EXPECT_EQ(readResult.value().name, "fresh");
    EXPECT_EQ(readResult.value().value, 99);
}

TEST_F(JsonStorageTest, WriteCreatesParentDirectories) {
    auto nestedPath = m_testDir / "sub1" / "sub2" / "data.json";
    JsonStorage<TestData> storage(nestedPath);

    TestData data{"nested", 1};
    auto result = storage.write(data);
    EXPECT_TRUE(result.success());
    EXPECT_TRUE(storage.exists());
}

TEST_F(JsonStorageTest, WriteOverwrites) {
    JsonStorage<TestData> storage(m_filePath);

    TestData first{"first", 1};
    storage.write(first);

    TestData second{"second", 2};
    storage.write(second);

    auto readResult = storage.read();
    EXPECT_TRUE(readResult.success());
    EXPECT_EQ(readResult.value().name, "second");
    EXPECT_EQ(readResult.value().value, 2);
}

TEST_F(JsonStorageTest, FilePathAccessor) {
    JsonStorage<TestData> storage(m_filePath);
    EXPECT_EQ(storage.filePath(), m_filePath);
}

TEST_F(JsonStorageTest, ReadInvalidJson) {
    // 手动写入无效 JSON
    std::ofstream file(m_filePath);
    file << "not valid json {{{";

    JsonStorage<TestData> storage(m_filePath);
    auto result = storage.read();
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error().code(), Error::Code::IOError);
}

TEST_F(JsonStorageTest, JsonFormatIsPretty) {
    JsonStorage<TestData> storage(m_filePath);

    TestData data{"formatted", 42};
    storage.write(data);

    // 验证文件格式是缩进的
    std::ifstream file(m_filePath);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // 应该有换行和缩进
    EXPECT_NE(content.find('\n'), std::string::npos);
    EXPECT_NE(content.find("  "), std::string::npos); // 2空格缩进
}
