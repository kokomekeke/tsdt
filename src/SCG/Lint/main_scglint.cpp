#include <exception>
#include <string>
#include "Vendored/argparse/argparse.hpp"
#include "Program.hpp"

namespace {

    using Sagax::SCG::Lint::Parameters;

    struct ProgramParser {
        Parameters params;
        argparse::ArgumentParser program;
        argparse::ArgumentParser scenarioCmd;
        argparse::ArgumentParser configCmd;
        argparse::ArgumentParser ridCmd;
        argparse::ArgumentParser tsdCmd;

        ProgramParser() :
            program("scglint.exe", "1.0", argparse::default_arguments::all),
            scenarioCmd("scenario"),
            configCmd("config"),
            ridCmd("rid"),
            tsdCmd("tsd")
        {
            program.add_description(
                "Parses and validates SCG scenario/config/rid/tsd json files without requiring hardware "
                "or executing anything. Exit codes: 0 = valid, 1 = validation errors, 2 = parse/usage error."
            );

            scenarioCmd.add_description("parse and validate a scenario json file");
            scenarioCmd.add_argument("scenario_path").help("path to scenario .json");

            configCmd.add_description("parse and validate a server config json file");
            configCmd.add_argument("config_path").help("path to server config .json");

            ridCmd.add_description("parse and validate a rid json file against a server config json");
            ridCmd.add_argument("config_path").help("path to server config .json");
            ridCmd.add_argument("rid_path").help("url of the rid .json, file:// or http://");

            tsdCmd.add_description(
                "read a tsd json and the rid of each of its steps, merge them and validate the result"
            );
            tsdCmd.add_argument("config_path").help("path to server config .json");
            tsdCmd.add_argument("tsd_path").help("url of the tsd .json, file:// or http://");

            program.add_subparser(scenarioCmd);
            program.add_subparser(configCmd);
            program.add_subparser(ridCmd);
            program.add_subparser(tsdCmd);
        }

        void Parse(int argc, char **argv) {
            params.command = Parameters::Command::none;
            params.help = program.help().str();

            try {
                program.parse_args(argc, argv);
            }
            catch(const std::exception &e) {
                params.error = e.what();
                return;
            }

            if(program.is_subcommand_used(scenarioCmd)) {
                params.command = Parameters::Command::scenario;
                params.scenarioPath = scenarioCmd.get<std::string>("scenario_path");
            }
            else if(program.is_subcommand_used(configCmd)) {
                params.command = Parameters::Command::config;
                params.configPath = configCmd.get<std::string>("config_path");
            }
            else if(program.is_subcommand_used(ridCmd)) {
                params.command = Parameters::Command::rid;
                params.configPath = ridCmd.get<std::string>("config_path");
                params.ridPath = ridCmd.get<std::string>("rid_path");
            }
            else if(program.is_subcommand_used(tsdCmd)) {
                params.command = Parameters::Command::tsd;
                params.configPath = tsdCmd.get<std::string>("config_path");
                params.tsdPath = tsdCmd.get<std::string>("tsd_path");
            }
            else {
                params.error = "no subcommand given";
            }
        }
    };

}

int main(int argc, char **argv) {
    ProgramParser parser;
    parser.Parse(argc, argv);

    Sagax::SCG::Lint::Program program;
    return program.Main(parser.params);
}
