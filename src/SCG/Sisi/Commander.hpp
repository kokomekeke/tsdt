#pragma once
#include <string>

namespace Sagax::SCG::Commander {

    struct Parameters {
        bool success;
        std::string error;
        std::string help;
        std::string configPath;
    };

    // console client that reads commands typed in the terminal and sends them through the
    // broker named in the config
    struct Program {
        int Main(Parameters params);
    };

}
