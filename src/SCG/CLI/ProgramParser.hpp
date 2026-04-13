#pragma once
#include <string>
#include <vector>
#include <optional>

#include "Wrappers/JsonSane.hpp"
#include "SSG/argparse/argparse.hpp"

struct Parameters {
    enum struct Mode { help, stream, record, playback };
    enum struct HelpCategory { general, output, signal };

    // could use variants but why
    bool success;
    std::string error;
    Mode mode;
    std::string jsonPath;
    HelpCategory helpCategory;
    std::optional<std::string> helpSubject;
};

struct ProgramParser {

    Parameters params;
    argparse::ArgumentParser program;

    ProgramParser() : program("ScenarioGenerator", "1.0", argparse::default_arguments::version) {
        program.add_argument("-h", "--help")
            .help("prints general help, or help for a specific signal or output. (use \"-h signal\" or \"-h output\")")
            .nargs(0, 2)
            .default_value(std::vector<std::string>{ });

        program.add_argument("path")
            .nargs(argparse::nargs_pattern::optional)
            .help("path to the scenario JSON file");

        auto &mode = program.add_mutually_exclusive_group(false);
        mode.add_argument("-r", "--record")
            .help("record mode")
            .default_value(false)
            .implicit_value(true);

        mode.add_argument("-p", "--playback")
            .help("playback mode")
            .default_value(false)
            .implicit_value(true);

        mode.add_argument("-s", "--stream")
            .help("stream mode")
            .default_value(false)
            .implicit_value(true);
    }

    std::string Help() {
        return program.help().str();
    }

    void Parse(int argc, char **argv) {
        try {
            program.parse_args(argc, argv);
        } catch(const std::exception &e) {
            params.success = false;
            params.error = e.what();
            return;
        }

        if(program.is_used("-h")) {
            auto help = program.get<std::vector<std::string>>("-h");
            params.success = true;
            params.mode = Parameters::Mode::help;
            if(help.size() >= 1 && help.size() < 3 && help[0] == "output") {
                params.helpCategory = Parameters::HelpCategory::output;
            }
            else if(help.size() >= 1 && help.size() < 3 && help[0] == "signal") {
                params.helpCategory = Parameters::HelpCategory::signal;
            }
            else {
                params.helpCategory = Parameters::HelpCategory::general;
            }
            if(help.size() > 1) {
                params.helpSubject = help[1];
            }
            else {
                params.helpSubject = std::nullopt;
            }
            return;
        }

        auto jsonPath = program.present<std::string>("path");
        if(jsonPath.has_value()) {
            params.jsonPath = jsonPath.value();
        }
        else {
            params.success = false;
            params.error = "path to json is missing";
            return;
        }

        if(program.get<bool>("-r")) {
            params.success = true;
            params.mode = Parameters::Mode::record;
            return;
        }
        else if(program.get<bool>("-p")) {
            params.success = true;
            params.mode = Parameters::Mode::playback;
            return;
        }
        else if(program.get<bool>("-s")) {
            params.success = true;
            params.mode = Parameters::Mode::stream;
            return;
        }
        else {
            params.success = false;
            params.error = "no mode selected";
            return;
        }
    }

};


