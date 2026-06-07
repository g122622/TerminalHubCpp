#include "terminalhub/Session/SessionRegistry.hpp"
#include "terminalhub/Storage/ConfigManager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <nlohmann/json.hpp>

namespace th {

// SessionMetadata 的 JSON 序列化
static nlohmann::json metadataToJson(const SessionMetadata& m) {
    return {
        {"id", m.id},
        {"title", m.title},
        {"shell", m.shell},
        {"cwd", m.cwd},
        {"pid", static_cast<i64>(m.pid)},
        {"createdAt", m.createdAt},
        {"lastActivityAt", m.lastActivityAt},
        {"connectedClients", m.connectedClients},
    };
}

static SessionMetadata jsonToMetadata(const nlohmann::json& j) {
    SessionMetadata m;
    m.id = j.value("id", "");
    m.title = j.value("title", "");
    m.shell = j.value("shell", "");
    m.cwd = j.value("cwd", "");
    m.pid = static_cast<DWORD>(j.value("pid", 0));
    m.createdAt = j.value("createdAt", i64(0));
    m.lastActivityAt = j.value("lastActivityAt", i64(0));
    m.connectedClients = j.value("connectedClients", 0);
    return m;
}

// ============================================================
// 构造
// ============================================================

SessionRegistry::SessionRegistry()
    : m_filePath(Paths::sessionsPath()) {
}

// ============================================================
// 加载所有
// ============================================================

std::vector<SessionMetadata> SessionRegistry::loadAll() {
    JsonStorage<nlohmann::json> storage(m_filePath);
    auto result = storage.read();
    if (!result.success()) {
        return {};
    }

    std::vector<SessionMetadata> sessions;
    auto& data = result.value();
    if (data.is_object()) {
        for (auto& [key, value] : data.items()) {
            sessions.push_back(jsonToMetadata(value));
        }
    }
    return sessions;
}

// ============================================================
// 保存
// ============================================================

void SessionRegistry::save(const SessionMetadata& metadata) {
    JsonStorage<nlohmann::json> storage(m_filePath);

    auto result = storage.update([&metadata](const nlohmann::json* current) -> nlohmann::json {
        nlohmann::json data = current ? *current : nlohmann::json::object();
        data[metadata.id] = metadataToJson(metadata);
        return data;
    });
}

// ============================================================
// 删除
// ============================================================

void SessionRegistry::remove(const std::string& sessionId) {
    JsonStorage<nlohmann::json> storage(m_filePath);

    storage.update([&sessionId](const nlohmann::json* current) -> nlohmann::json {
        nlohmann::json data = current ? *current : nlohmann::json::object();
        data.erase(sessionId);
        return data;
    });
}

// ============================================================
// 获取单个
// ============================================================

std::optional<SessionMetadata> SessionRegistry::get(const std::string& sessionId) {
    auto all = loadAll();
    for (const auto& m : all) {
        if (m.id == sessionId) {
            return m;
        }
    }
    return std::nullopt;
}

// ============================================================
// 列表（带存活状态）
// ============================================================

std::vector<SessionListItem> SessionRegistry::list() {
    auto sessions = loadAll();
    std::vector<SessionListItem> items;

    for (const auto& m : sessions) {
        SessionListItem item;
        item.id = m.id;
        item.title = m.title;
        item.shell = m.shell;
        item.pid = m.pid;
        item.createdAt = m.createdAt;
        item.lastActivityAt = m.lastActivityAt;
        item.connectedClients = m.connectedClients;
        item.alive = _isProcessAlive(m.pid);
        items.push_back(item);
    }

    // 按最后活动时间降序排序
    std::sort(items.begin(), items.end(),
              [](const SessionListItem& a, const SessionListItem& b) {
                  return a.lastActivityAt > b.lastActivityAt;
              });

    return items;
}

// ============================================================
// 清理已死亡的会话
// ============================================================

std::vector<std::string> SessionRegistry::cleanup() {
    auto sessions = loadAll();
    std::vector<std::string> removed;

    for (const auto& m : sessions) {
        if (!_isProcessAlive(m.pid)) {
            removed.push_back(m.id);
        }
    }

    if (!removed.empty()) {
        JsonStorage<nlohmann::json> storage(m_filePath);
        storage.update([&removed](const nlohmann::json* current) -> nlohmann::json {
            nlohmann::json data = current ? *current : nlohmann::json::object();
            for (const auto& id : removed) {
                data.erase(id);
            }
            return data;
        });
    }

    return removed;
}

// ============================================================
// 检查进程是否存活
// ============================================================

bool SessionRegistry::_isProcessAlive(DWORD pid) {
    if (pid == 0) {
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return false;
    }

    DWORD exitCode = 0;
    BOOL ok = GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);

    return ok && (exitCode == STILL_ACTIVE);
}

} // namespace th
