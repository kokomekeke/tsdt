#pragma once
#include <string>
#include <vector>
#include <optional>

#include "Wrappers/JsonSane.hpp"
#include "Vendored/argparse/argparse.hpp"

struct Parameters {
    enum struct Mode { stream, record, playback };

    // could use variants but why
    bool success;
    // --help and --version write their text and end the run, there is nothing else to do
    bool helpRequested;
    std::string error;
    Mode mode;
    std::string jsonPath;
    std::string recordingDirectory;
};

struct ProgramParser {

    Parameters params;
    argparse::ArgumentParser program;

    // exit_on_default_arguments is off so that --help and --version come back here
    // instead of calling std::exit, which would skip every destructor on the way out
    ProgramParser() : program("scg.exe", "1.0", argparse::default_arguments::all, false) {
        auto &mode = program.add_mutually_exclusive_group(false);
        mode.add_argument("-r", "--record")
            .help("record mode: -r <jsonPath> <recordingDirectory>")
            .nargs(2);

        mode.add_argument("-p", "--playback")
            .help("playback mode: -p <recordingDirectory>")
            .nargs(1);

        mode.add_argument("-s", "--stream")
            .help("stream mode: -s <jsonPath>")
            .nargs(1);
    }

    std::string Help() {
        return program.help().str();
    }

    void Parse(int argc, char **argv) {
        params.helpRequested = false;

        std::string parseError;
        try {
            program.parse_args(argc, argv);
        }
        catch(const std::exception &e) {
            parseError = e.what();
        }

        // the help text is written while the arguments are read and the missing ones are
        // only reported after that, so the request is answered before the error is looked at
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

        if(program.is_used("-r")) {
            auto values = program.get<std::vector<std::string>>("-r");
            params.jsonPath = values[0];
            params.recordingDirectory = values[1];
            params.mode = Parameters::Mode::record;
            params.success = true;
            return;
        }
        else if(program.is_used("-p")) {
            auto values = program.get<std::vector<std::string>>("-p");
            params.recordingDirectory = values[0];
            params.mode = Parameters::Mode::playback;
            params.success = true;
            return;
        }
        else if(program.is_used("-s")) {
            auto values = program.get<std::vector<std::string>>("-s");
            params.jsonPath = values[0];
            params.recordingDirectory.clear();
            params.mode = Parameters::Mode::stream;
            params.success = true;
            return;
        }
        else {
            params.success = false;
            params.error = "no mode selected";
            return;
        }
    }

};


