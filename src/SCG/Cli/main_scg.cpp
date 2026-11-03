#include <chrono>
#include <csignal>
#include <exception>
#include <string>
#include <vector>
#include "Common/Logger.hpp"
#include "Vendored/argparse/argparse.hpp"
#include "SCG/Utility/SignalHandler.hpp"
#include "SCG/Utility/TimeFormat.hpp"
#include "Program.hpp"

namespace {

    using Sagax::SCG::Cli::Parameters;

    struct ProgramParser {

        Parameters params;
        argparse::ArgumentParser program;

        // exit_on_default_arguments is off, --help and --version return here instead of calling
        // std::exit
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

            program.add_argument("-t", "--start-time")
                .help("the time of the first sample in stream and playback mode, for example 2026-09-02T14:30:00.000+02:00. without it the start is 1 second after the pipeline is set up")
                .nargs(1);
        }

        void Parse(int argc, char **argv) {
            params.helpRequested = false;
            params.help = program.help().str();

            std::string parseError;
            try {
                program.parse_args(argc, argv);
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

            params.startTime.reset();
            if(program.is_used("--start-time")) {
                std::chrono::system_clock::time_point startTime;
                std::string error = Sagax::SCG::ParseStartTime(program.get<std::vector<std::string>>("--start-time")[0], startTime);
                if(!error.empty()) {
                    params.success = false;
                    params.error = error;
                    return;
                }
                params.startTime = startTime;
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

}

int main(int argc, char **argv) {
    ProgramParser parser;
    parser.Parse(argc, argv);

    Sagax::SCG::Cli::Program program;

    // constructed before anything else starts a thread, on linux a thread inherits the
    // signal mask and the blocking has to be in place by then
    Sagax::SCG::SignalHandler signalHandler({ SIGINT, SIGTERM }, [&program](int sig){
        Sagax::Common::Logger::Info("signal {} received, stopping", sig);
        program.Stop();
    });

    return program.Main(parser.params);
}
