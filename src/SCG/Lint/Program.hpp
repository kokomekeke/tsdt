#pragma once
#include <string>

namespace Sagax::SCG::Lint {

    // the paths a command does not take are left empty. Command::none means the arguments
    // could not be read, error says why
    struct Parameters {
        enum class Command { none, scenario, config, rid, tsd };

        Command command;
        std::string error;
        // written after a parse error
        std::string help;
        std::string scenarioPath;
        std::string configPath;
        std::string ridPath;
        std::string tsdPath;
    };

    // scglint: parses and validates scenario/config/rid/tsd json files without touching any
    // hardware (USRP/Sidekiq) or MQTT broker, and without building or running a pipeline.
    // It only exercises the same Parse*/Validate* calls the real tools (scg.exe, scgserver.exe)
    // run before they go anywhere near hardware.
    struct Program {
        // sets up logging, then reports a parse error or runs the command
        int Main(Parameters params);
    };

}
