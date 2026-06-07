#pragma once

#include "terminalhub/Session/Session.hpp"
#include "terminalhub/Storage/JsonStorage.hpp"
#include "terminalhub/Core/Result.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace th {

/**
 * @brief 会话持久化存储
 *
 * 使用 JSON 文件存储会话元数据。
 * 路径: ~/.terminalhub/sessions.json
 */
class SessionRegistry {
public:
    SessionRegistry();

    /**
     * @brief 加载所有会话元数据
     */
    std::vector<SessionMetadata> loadAll();

    /**
     * @brief 保存会话元数据
     */
    void save(const SessionMetadata& metadata);

    /**
     * @brief 删除会话元数据
     */
    void remove(const std::string& sessionId);

    /**
     * @brief 获取单个会话
     */
    std::optional<SessionMetadata> get(const std::string& sessionId);

    /**
     * @brief 获取会话列表（带存活状态）
     */
    std::vector<SessionListItem> list();

    /**
     * @brief 清理已死亡的会话
     * @return 被清理的会话 ID 列表
     */
    std::vector<std::string> cleanup();

private:
    /**
     * @brief 检查进程是否存活
     */
    static bool _isProcessAlive(DWORD pid);

    std::filesystem::path m_filePath;
};

} // namespace th
