#include "ServerArguments.hpp"

namespace Sagax::SCG {

    // exit_on_default_arguments is off, so --help and --version return here instead of
    // calling std::exit
    ProgramParser::ProgramParser() : program("scgserver.exe", "1.0", argparse::default_arguments::all, false) {
        program.add_argument("config_path")
            .help("path to configuration .json");
    }

    std::string ProgramParser::Help() {
        return program.help().str();
    }

    void ProgramParser::Parse(int argc, char **argv) {
        params.helpRequested = false;

        std::string parseError;
        try {
            program.parse_args(argc, argv);
            params.configPath = program.get<std::string>("config_path");
        }
        catch(const std::exception &e) {
            parseError = e.what();
        }

        // the help text is written during parsing, a missing argument does not matter then
        if(program.is_used("--help") || program.is_used("--version")) {
            params.helpRequested = true;
            params.success = true;
            return;
        }
        if(!parseError.empty()) {
            params.success = false;
            params.error = parseError;
            return;
        }

        params.success = true;
    }

}
