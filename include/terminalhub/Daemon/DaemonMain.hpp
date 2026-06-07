#pragma once

#include "terminalhub/Core/Types.hpp"

namespace th {

/**
 * @brief Daemon entry point
 *
 * Identified by the environment variable TERMINALHUB_DAEMON=1.
 * Load config -> initialize session manager -> start IPC server -> register command handlers -> run.
 */
class DaemonMain {
public:
    /**
     * @brief Run the daemon main loop
     * @return Exit code
     */
    static int run();

    /**
     * @brief Check if the current process is a daemon
     */
    static bool isDaemonProcess();
};

} // namespace th
