#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
#include <numbers>
#include <utility>

#include "SignalParserRegistry.hpp"
#include "OutputParserRegistry.hpp"
#include "Wrappers/JsonSane.hpp"

#include "SCG/Parsers.hpp"

#include "Common/Logger.hpp"
#include "Common/Assert.hpp"
#include "Common/Defer.hpp"
#include "Common/Timer.hpp"
#include "Pipeline/Pipeline.hpp"
#include "Pipeline/Controller.hpp"
#include "Pipeline/PpController.hpp"
#include "Pipeline/RtcController.hpp"

#include "SSG/argparse/argparse.hpp"
#include "SSG/Packets/Packets.hpp"

using namespace Sagax::Common;
using namespace Sagax::SSG::Modules;
using namespace Sagax::Pipeline;
using namespace Sagax::SCG;

int main(int argc, char **argv) {
    // SetCurrentThreadName("main");
    StdLogStream logStream(std::cerr, Logger::Level::Trace, {});
    Logger::AddStream(&logStream);
    Defer disposeLogger([&](){
        Logger::RemoveStream(&logStream);
    });

    auto signalRegistry = GetSignalRegistry();
    auto outputRegistry = GetOutputRegistry();

    // record: scenario.json --> write to files
    // playback: scenario.json: recorded files --> stream to radio
    // stream: scenario.json -> stream to device

    argparse::ArgumentParser program("scenariogenerator");

    program.add_argument("-h", "--help")
        .help("Shows general help, or help for a specific signal.")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(std::string(""));

    program.add_argument("json_path")
        .nargs(argparse::nargs_pattern::optional)
        .help("Path to the scenario JSON file");

    auto &mode = program.add_mutually_exclusive_group(false); // not required
    mode.add_argument("-r", "--rec", "--record")
        .help("Record mode: -r <output format>")
        .implicit_value(true);

    mode.add_argument("-p", "--play", "--playback")
        .help("Playback mode")
        .default_value(false)
        .implicit_value(true);

    mode.add_argument("-s", "--stream")
        .help("Stream mode (default)")
        .default_value(true)     // if user omits all mode args
        .implicit_value(true);   // if user writes --stream

    try {
        program.parse_args(argc, argv);
    } catch(const std::exception &e) {
        Logger::Error("error: {}", e.what());
        std::cout << program.help().str();
        return 1;
    }

    if (program.get<bool>("--version")) {
        Logger::Info("SCG version 1.0.0");
        return 0;
    }

    if (program.is_used("--help")) {
        std::string name = program.get<std::string>("--help");
        if (name.empty()) {
            std::stringstream ss;
            ss << program.help().str() << std::endl << "Available Signals (use -h <name> for details):" << std::endl;

            std::vector<std::string> names;
            for (const auto& pair : signalRegistry.entries) {
                names.push_back(pair.first);
            }
            std::sort(names.begin(), names.end());
            for (const auto& name_ : names) {
                ss << "    " << name_ << std::endl;
            }
            Logger::Info("{}", ss.str());
        } else {
            if (signalRegistry.entries.contains(name)) {
                auto parser = signalRegistry.entries.at(name).ctor();
                Logger::Info("{}", parser->Help());
            } else {
                Logger::Error("Signal '{}' not found.", name);
                return 1;
            }
        }
        return 0;
    }

    if (!program.is_used("json_path")) {
        Logger::Error("Argument error: 'json_path' is required.");
        Logger::Info("{}", program.help().str());
        return 1;
    }

    Pipeline p;

    try {
        std::string json_path = program.get("json_path");
        std::filesystem::path path = std::filesystem::absolute(json_path);
        Assert(path.has_filename(), std::format("Specify a filename, instead of {}", json_path));
        std::string scen_name = path.filename().stem().string();

        Assert(std::filesystem::exists(path), std::format("File does not exist: {}", path.string()));
        std::ifstream file(path);
        Assert(file.is_open(), std::format("Failed to open file: {}", path.string()));

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json_str = buffer.str();

        ScenarioParser parser;
        Scenario scen = parser.Parse(scen_name, json_str, signalRegistry, outputRegistry);
        ScenarioTransformer transformer(scen);

        if (program.is_used("-r")) {
            Logger::Info("SCG - Recording mode");
            p = transformer.ToRecordingPipeline().Build();
        } else if (program.is_used("-p")) {
            p = transformer.ToPlaybackPipeline().Build();
            Logger::Info("SCG - Playback mode");
        } else if (program.get<bool>("--stream")) { // defaulted
            Logger::Info("SCG - Streaming mode");
            p = transformer.ToStreamingPipeline().Build();
        }
    }
    catch(const std::exception &e) {
        Logger::Error("could not create pipeline: {}", e.what());
        return 1;
    }

    PpController controller;
    controller.Setup(p, 8);
    // RtcController controller;
    // controller.Setup(p, 3);
    Timer timer;
    timer.Set();
    controller.Start();
    controller.Join();
    auto time_ms = timer.Get_ms();

    try {
        controller.Throw();
    }
    catch(...) {
    }
    // catch(const std::exception &e) {
    //     // Logger::Error("error running pipeline: {}", e.what());
    // }

    Logger::Info("time: {} ms", time_ms);

    return 0;
}
