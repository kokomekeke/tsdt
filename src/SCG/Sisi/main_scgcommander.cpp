#include <exception>
#include <string>
#include "Vendored/argparse/argparse.hpp"
#include "Commander.hpp"

namespace {

    using Sagax::SCG::Commander::Parameters;

    struct ProgramParser {
        Parameters params;
        argparse::ArgumentParser program;

        ProgramParser() : program("scgcommander.exe", "1.0", argparse::default_arguments::all) {
            program.add_argument("config_path")
                .help("path to configuration .json");
        }

        void Parse(int argc, char **argv) {
            params.help = program.help().str();
            try {
                program.parse_args(argc, argv);
            }
            catch(const std::exception &e) {
                params.success = false;
                params.error = e.what();
                return;
            }

            params.configPath = program.get<std::string>("config_path");
            params.success = true;
        }
    };

}

int main(int argc, char **argv) {
    ProgramParser parser;
    parser.Parse(argc, argv);

    Sagax::SCG::Commander::Program program;
    return program.Main(parser.params);
}
