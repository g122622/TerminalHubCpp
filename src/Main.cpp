#include "terminalhub/Core/Logger.hpp"
#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Daemon/DaemonMain.hpp"
#include "terminalhub/CLI/CliApp.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Detect daemon mode
    if (th::DaemonMain::isDaemonProcess()) {
        return th::DaemonMain::run();
    }

    // CLI mode
    return th::CliApp::run(argc, argv);
}
