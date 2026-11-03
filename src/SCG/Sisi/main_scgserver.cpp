#include <exception>
#include <string>
#include "Vendored/argparse/argparse.hpp"
#include "Server.hpp"

namespace {

    using Sagax::SCG::Server::Parameters;

    struct ProgramParser {
        Parameters params;
        argparse::ArgumentParser program;

        // exit_on_default_arguments is off, so --help and --version return here instead of
        // calling std::exit
        ProgramParser() : program("scgserver.exe", "1.0", argparse::default_arguments::all, false) {
            program.add_argument("config_path")
                .help("path to configuration .json");
        }

        void Parse(int argc, char **argv) {
            params.helpRequested = false;
            params.help = program.help().str();

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
    };

}

int main(int argc, char **argv) {
    ProgramParser parser;
    parser.Parse(argc, argv);

    // the mqtt client of the server uses the library from its configuration to its destruction
    Sagax::SCG::MqttClient::LibraryInit();
    int code = 0;
    {
        Sagax::SCG::Server::Server server;
        code = server.Main(parser.params);
    }
    Sagax::SCG::MqttClient::LibraryCleanup();
    return code;
}
