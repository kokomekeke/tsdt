#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
#include <numbers>
#include <utility>

#include "Wrappers/JsonSane.hpp"
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

#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"
// #include "SCG/JsonParsing/OutputParserRegistry.hpp"
// #include "SCG/JsonParsing/SignalParserRegistry.hpp"
#include "SCG/ScenarioTransform/ScenarioContext.hpp"
#include "SCG/ScenarioTransform/Transform.hpp"
#include "SCG/ScenarioTransform/BuildPipeline.hpp"

#include "ProgramParser.hpp"

using namespace Sagax;
using namespace Sagax::SCG;


struct Program {

    ProgramParser parser;

    // Sagax::Parser::Registry<Sagax::SCG::OutputParser> outputRegistry;
    // Sagax::Parser::Registry<Sagax::SCG::SignalParser> signalRegistry;

    void PrintHelp(
        Parameters::HelpCategory category,
        std::optional<std::string> subject
    ) {
        switch(category) {
            case Parameters::HelpCategory::general: {
                Common::Logger::Info("\n{}", parser.Help().c_str());
            } break;
            case Parameters::HelpCategory::output: {
                // if(!subject.has_value()) {
                //     std::stringstream ss;
                //     ss << "available Outputs (use -h output <name> for details):\n";
                //     std::vector<std::string> names;
                //     for (const auto& pair : outputRegistry.entries) {
                //         names.push_back(pair.first);
                //     }
                //     std::sort(names.begin(), names.end());
                //     for (const auto& name_ : names) {
                //         ss << "    " << name_ << std::endl;
                //     }
                //     Common::Logger::Info("\n{}", ss.str());
                // }
                // else {
                //     std::string name = subject.value();
                //     if (outputRegistry.entries.contains(name)) {
                //         auto parser_ = outputRegistry.entries.at(name).ctor();
                //         Common::Logger::Info("\n{}", parser_->Help());
                //     } else {
                //         Common::Logger::Error("missing Output: {}", name);
                //     }
                // }
            } break;
            case Parameters::HelpCategory::signal: {
                // if(!subject.has_value()) {
                //     std::stringstream ss;
                //     ss << "available Signals (use -h signal <name> for details):\n";
                //     std::vector<std::string> names;
                //     for (const auto& pair : signalRegistry.entries) {
                //         names.push_back(pair.first);
                //     }
                //     std::sort(names.begin(), names.end());
                //     for (const auto& name_ : names) {
                //         ss << "    " << name_ << std::endl;
                //     }
                //     Common::Logger::Info("\n{}", ss.str());
                // }
                // else {
                //     std::string name = subject.value();
                //     if (signalRegistry.entries.contains(name)) {
                //         auto parser_ = signalRegistry.entries.at(name).ctor();
                //         Common::Logger::Info("\n{}", parser_->Help());
                //     } else {
                //         Common::Logger::Error("missing Signal: {}", name);
                //     }
                // }
            } break;
        }
    }

    void Run(Parameters params) {
        Common::Logger::Info("SCG");

        Scenario scenario;
        try {
            std::filesystem::path path = std::filesystem::absolute(params.jsonPath);
            Common::Assert(path.has_filename(), std::format("wrong filename: {}", params.jsonPath));
            std::string scenName = path.filename().stem().string();
            Common::Assert(std::filesystem::exists(path), std::format("file missing: {}", path.string()));
            std::ifstream file(path);
            Common::Assert(file.is_open(), std::format("failed to open file: {}", path.string()));

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string jsonStr = buffer.str();

            // scenario = ParseScenario(jsonStr, scenName, signalRegistry, outputRegistry);
            scenario = ParseScenario(jsonStr, scenName);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("could not parse scenario:\n{}", e.what());
            return;
        }

        {
            auto problems = ValidateScenario(scenario);
            for(auto &e : problems.warnings) {
                Common::Logger::Warn("{}", e.c_str());
            }
            if(!problems.errors.empty()) {
                for(auto &e : problems.errors) {
                    Common::Logger::Error("{}", e.c_str());
                }
                Common::Logger::Info("pipeline invalid");
                return;
            }
            else {
                Common::Logger::Info("scenario validated");
            }
        }

        ScenarioContext ctx = {
            .scenario = scenario
        };
        try {
            switch(params.mode) {
                case Parameters::Mode::playback: {
                    auto b = BuildPlaybackPipeline(scenario, params.recordingDirectory);
                    ctx.pipeline = b.builder.Build();
                    ctx.resources = b.resources;
                    ctx.ownedResources = std::move(b.ownedResources);
                } break;
                case Parameters::Mode::record: {
                    auto b = BuildRecordingPipeline(scenario, params.recordingDirectory);
                    ctx.pipeline = b.builder.Build();
                    ctx.resources = b.resources;
                    ctx.ownedResources = std::move(b.ownedResources);
                } break;
                case Parameters::Mode::stream: {
                    auto b = BuildStreamingPipeline(scenario);
                    ctx.pipeline = b.builder.Build();
                    ctx.resources = b.resources;
                    ctx.ownedResources = std::move(b.ownedResources);
                } break;
                default: {
                    Common::Assert(false, "wrong mode");
                }
            }
        }
        catch(const std::exception &e) {
            Common::Logger::Error("could not build pipeline:\n{}", e.what());
            return;
        }

        auto controller = std::make_unique<Pipeline::PpController>();
        controller->Setup(ctx.pipeline, 8);
        controller->Reset();
        ctx.controller = std::move(controller);

        try {
            ctx.Init();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("could not initialize resources:\n{}", e.what());
            return;
        }


        Common::Timer timer;
        timer.Set();
        ctx.controller->Start();
        ctx.controller->Join();
        auto time_ms = timer.Get_ms();

        try {
            ctx.controller->Throw();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("error running pipeline:\n{}", e.what());
        }

        Common::Logger::Info("time: {} ms", time_ms);
        ctx.Dispose();
    }

    void Main(int argc, char **argv) {
        // SetCurrentThreadName("main");
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, {});
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&](){
            Common::Logger::RemoveStream(&logStream);
        });

        // outputRegistry = GetOutputParserRegistry();
        // signalRegistry = GetSignalParserRegistry();

        parser.Parse(argc, argv);
        Parameters params = parser.params;
        if(!params.success) {
            Common::Logger::Error("failed to parse parameters: {}", params.error.c_str());
            Common::Logger::Info("\n{}", parser.Help().c_str());
            return;
        }

        if(params.mode == Parameters::Mode::help) {
            PrintHelp(parser.params.helpCategory, parser.params.helpSubject);
        }
        else {
            Run(params);
        }
    }

};

int main(int argc, char **argv) {
    Program program;
    program.Main(argc, argv);
    return 0;
}
