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
 * @brief Session persistent storage
 *
 * Stores session metadata using a JSON file.
 * Path: ~/.terminalhub/sessions.json
 */
class SessionRegistry {
public:
    SessionRegistry();

    /**
     * @brief Load all session metadata
     */
    std::vector<SessionMetadata> loadAll();

    /**
     * @brief Save session metadata
     */
    void save(const SessionMetadata& metadata);

    /**
     * @brief Remove session metadata
     */
    void remove(const std::string& sessionId);

    /**
     * @brief Get a single session
     */
    std::optional<SessionMetadata> get(const std::string& sessionId);

    /**
     * @brief Get session list (with alive status)
     */
    std::vector<SessionListItem> list();

    /**
     * @brief Clean up dead sessions
     * @return List of removed session IDs
     */
    std::vector<std::string> cleanup();

private:
    /**
     * @brief Check if a process is alive
     */
    static bool _isProcessAlive(DWORD pid);

    std::filesystem::path m_filePath;
};

} // namespace th
