#pragma once
#include <string>
#include "Vendored/argparse/argparse.hpp"

namespace Sagax::SCG {

    struct Parameters {
        bool success;
        // --help or --version was given, its text is already written
        bool helpRequested;
        std::string error;
        std::string configPath;
    };

    struct ProgramParser {
        Parameters params;
        argparse::ArgumentParser program;

        ProgramParser();
        std::string Help();
        void Parse(int argc, char **argv);
    };

}
