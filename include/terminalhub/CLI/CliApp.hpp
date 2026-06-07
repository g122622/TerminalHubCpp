#pragma once

#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Core/Types.hpp"

#include <string>

namespace th {

/**
 * @brief CLI application entry
 *
 * Parses CLI arguments using CLI11 and executes the corresponding command.
 */
class CliApp {
public:
    /**
     * @brief Run the CLI application
     * @param argc Argument count
     * @param argv Argument array
     * @return Exit code
     */
    static int run(int argc, char* argv[]);
};

} // namespace th
