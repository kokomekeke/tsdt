#pragma once
#include <string>

namespace Sagax::SCG::Lint {

    // the paths a command does not take are left empty. Command::none means the arguments
    // could not be read, error says why
    struct Parameters {
        enum class Command { none, scenario, config, rid, tsd };

        Command command;
        std::string error;
        std::string help;
        std::string scenarioPath;
        std::string configPath;
        std::string ridPath;
        std::string tsdPath;
    };

    // parses and validates scenario, config, rid and tsd json files with the same calls the
    // other tools use. touches no hardware or broker and builds no pipeline
    struct Program {
        int Main(Parameters params);
    };

}
