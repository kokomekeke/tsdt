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
#include "SCG/ScenarioTransform/ScenarioRunnerContext.hpp"
#include "SCG/ScenarioTransform/Transform.hpp"
#include "SCG/ScenarioTransform/BuildPipeline.hpp"
#include "SCG/ScenarioTransform/PipelineConfig.hpp"

#include "ProgramParser.hpp"

using namespace Sagax;
using namespace Sagax::SCG;


struct Program {

    ProgramParser parser;
    PipelineConfig pipelineConfig;

    Scenario ParseFile(std::string jsonPath) {
        Scenario scenario;

        try {
            std::filesystem::path path = std::filesystem::absolute(jsonPath);
            Common::Assert(path.has_filename(), std::format("wrong filename: {}", jsonPath));
            std::string scenName = path.filename().stem().string();
            Common::Assert(std::filesystem::exists(path), std::format("file missing: {}", path.string()));
            std::ifstream file(path);
            Common::Assert(file.is_open(), std::format("failed to open file: {}", path.string()));

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string jsonStr = buffer.str();

            scenario = ParseScenario(jsonStr, scenName);
        }
        catch(const std::exception &e) {
            throw std::runtime_error(std::format("could not parse scenario file:\n{}", e.what()));
        }

        {
            Common::Logger::Info("validating scenario..");
            auto problems = ValidateScenario(scenario);
            for(auto &e : problems.warnings) {
                Common::Logger::Warn("{}", e.c_str());
            }
            if(!problems.errors.empty()) {
                for(auto &e : problems.errors) {
                    Common::Logger::Error("{}", e.c_str());
                }
                throw std::runtime_error("validation failed");
            }
            else {
                Common::Logger::Info("validation successful");
            }
        }

        return scenario;
    }

    ScenarioRunnerContext SetupRecording(std::string jsonPath, std::string dirPath) {
        ScenarioRunnerContext ctx;
        ctx.scenario = ParseFile(jsonPath);
        auto b = BuildRecordingPipeline(pipelineConfig, ctx.scenario, dirPath);
        ctx.pipeline = b.builder.Build();
        ctx.resources = b.resources;
        ctx.ownedResources.emplace_back(std::move(b.dir));
        return ctx;
    }

    ScenarioRunnerContext SetupPlayback(std::string dirPath) {
        ScenarioRunnerContext ctx;
        auto b = BuildPlaybackPipeline(pipelineConfig, dirPath);
        ctx.pipeline = b.builder.Build();
        ctx.resources = b.resources;
        ctx.scenario = b.dir->GetScenario();
        ctx.ownedResources.emplace_back(std::move(b.dir));
        return ctx;
    }

    ScenarioRunnerContext SetupStreaming(std::string jsonPath) {
        ScenarioRunnerContext ctx;
        ctx.scenario = ParseFile(jsonPath);
        auto b = BuildStreamingPipeline(pipelineConfig, ctx.scenario);
        ctx.pipeline = b.builder.Build();
        ctx.resources = b.resources;
        return ctx;
    }

    void Configure() {
        #ifdef HAVE_USRP
            pipelineConfig.preinit.usrp.usePreinitOnly = false;
        #endif
    }

    void Run(Parameters params) {
        Common::Logger::Info("SCG");

        Configure();

        ScenarioRunnerContext ctx;
        try {
            switch(params.mode) {
                case Parameters::Mode::playback: { ctx = SetupPlayback(params.recordingDirectory); } break;
                case Parameters::Mode::record: { ctx = SetupRecording(params.jsonPath, params.recordingDirectory); } break;
                case Parameters::Mode::stream: { ctx = SetupStreaming(params.jsonPath); } break;
                default: {
                    Common::Assert(false, "wrong mode");
                }
            }
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\ncould not set up pipeline:\n{}", e.what());
            return;
        }

        // this could maybe be controlled individually for each mode
        auto controller = std::make_unique<Pipeline::PpController>();
        controller->Setup(ctx.pipeline, 32);
        controller->Reset();
        ctx.controller = std::move(controller);

        try {
            ctx.Init();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\ncould not initialize resources:\n{}", e.what());
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
            Common::Logger::Error("\nerror running pipeline:\n{}", e.what());
        }

        Common::Logger::Info("time: {} ms", time_ms);
        ctx.Dispose();
    }

    void Main(int argc, char **argv) {
        // SetCurrentThreadName("main");
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .file = true,
            .function = true
        });
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&]() {
            Common::Logger::RemoveStream(&logStream);
        });

        parser.Parse(argc, argv);
        Parameters params = parser.params;
        if(!params.success) {
            Common::Logger::Error("\ncould not parse parameters:\n{}", params.error.c_str());
            Common::Logger::Info("\n{}", parser.Help().c_str());
            return;
        }

        Run(params);
    }

};

int main(int argc, char **argv) {
    Program program;
    program.Main(argc, argv);
    return 0;
}
