#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
#include <numbers>
#include <utility>
#include <csignal>
#include <mutex>

#include "Wrappers/JsonSane.hpp"
#include "Common/Logger.hpp"
#include "Common/Assert.hpp"
#include "Common/Defer.hpp"
#include "Common/Timer.hpp"
#include "Common/ThreadPool.hpp"
#include "Pipeline/Pipeline.hpp"
#include "Pipeline/Controller.hpp"
#include "Pipeline/PpController.hpp"
#include "Pipeline/RtcController.hpp"
#include "Vendored/argparse/argparse.hpp"
#include "SSG/Packets/Packets.hpp"

#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"
#include "SCG/ScenarioTransform/ScenarioRunnerContext.hpp"
#include "SCG/ScenarioTransform/Transform.hpp"
#include "SCG/ScenarioTransform/BuildPipeline.hpp"
#include "SCG/ScenarioTransform/PipelineConfig.hpp"
#include "SCG/Utility/SignalHandler.hpp"

#include "ProgramParser.hpp"

using namespace Sagax;
using namespace Sagax::SCG;



struct Program {

    ProgramParser parser;
    PipelineConfig pipelineConfig;

    // the controller dispatches its workers here. it is declared before ctx so that it
    // is still alive when the controller ctx holds goes away. the pool grows on demand,
    // the count is only the number of threads it keeps around
    Common::ThreadPool threadPool{4};
    ScenarioRunnerContext ctx;

    // Stop() runs on the signal thread while Run() is still putting ctx together, so the
    // controller pointer is only read and written under this. stopRequested carries a
    // signal that arrived before there was a controller to hand it to
    std::mutex mtxStop;
    bool stopRequested = false;

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
        Common::Logger::Info("scenario name: {}", scenario.name.c_str());
        return scenario;
    }

    ScenarioRunnerContext SetupRecording(std::string jsonPath, std::string dirPath) {
        ScenarioRunnerContext res;
        res.scenario = ParseFile(jsonPath);
        auto b = BuildRecordingPipeline(pipelineConfig, res.scenario, dirPath);
        res.pipeline = b.builder.Build();
        res.resources = b.resources;
        res.ownedResources.emplace_back(std::move(b.dir));
        return res;
    }

    ScenarioRunnerContext SetupPlayback(std::string dirPath) {
        ScenarioRunnerContext res;
        auto b = BuildPlaybackPipeline(pipelineConfig, dirPath);
        res.pipeline = b.builder.Build();
        res.resources = b.resources;
        res.scenario = b.dir->GetScenario();
        res.ownedResources.emplace_back(std::move(b.dir));
        return res;
    }

    ScenarioRunnerContext SetupStreaming(std::string jsonPath) {
        ScenarioRunnerContext res;
        res.scenario = ParseFile(jsonPath);
        auto b = BuildStreamingPipeline(pipelineConfig, res.scenario);
        res.pipeline = b.builder.Build();
        res.resources = b.resources;
        return res;
    }

    void Configure() {
        #ifdef HAVE_USRP
            pipelineConfig.preinit.usrp.usePreinitOnly = false;
        #endif
    }

    // the pipeline owns the devices, so it goes first. uhd keeps dispatching log records
    // on a thread of its own after that, and libsidekiq outlives the devices it handed
    // out. mtxStop is held because clearing ctx destroys the controller Stop() reads
    void ReleaseResources() {
        std::unique_lock lck(mtxStop);
        ctx.Dispose();
        ctx = ScenarioRunnerContext{};
        #ifdef HAVE_USRP
            SSG::UsrpB200::DisposeLogging();
        #endif
        #ifdef HAVE_SIDEKIQ
            SSG::Sidekiq::LibraryCleanup();
        #endif
    }

    bool Run(Parameters params) {
        Common::Logger::Info("SCG");

        Configure();
        try {
            ScenarioRunnerContext res;
            switch(params.mode) {
                case Parameters::Mode::playback: { res = SetupPlayback(params.recordingDirectory); } break;
                case Parameters::Mode::record: { res = SetupRecording(params.jsonPath, params.recordingDirectory); } break;
                case Parameters::Mode::stream: { res = SetupStreaming(params.jsonPath); } break;
                default: {
                    Common::Assert(false, "wrong mode");
                }
            }
            {
                std::unique_lock lck(mtxStop);
                ctx = std::move(res);
            }
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\ncould not set up pipeline:\n{}", e.what());
            return false;
        }

        // this could maybe be controlled individually for each mode
        auto controller = std::make_unique<Pipeline::PpController>();
        controller->SetThreadPool(threadPool);
        controller->Setup(ctx.pipeline, 32);
        controller->Reset();
        {
            std::unique_lock lck(mtxStop);
            ctx.controller = std::move(controller);
            if(stopRequested) {
                ctx.controller->Stop(false);
            }
        }

        try {
            ctx.Init();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\ncould not initialize resources:\n{}", e.what());
            return false;
        }

        Common::Timer timer;
        timer.Set();
        ctx.controller->Start();
        ctx.controller->Join();
        auto time_ms = timer.Get_ms();

        bool ok = true;
        try {
            ctx.controller->Throw();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nerror running pipeline:\n{}", e.what());
            ok = false;
        }
        Common::Logger::Info("time: {} ms", time_ms);
        return ok;
    }

    void Stop(){
        std::unique_lock lck(mtxStop);
        stopRequested = true;
        if(ctx.controller) {
            ctx.controller->Stop(false);
        }
    }

    int Main(int argc, char **argv) {
        // SetCurrentThreadName("main");
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .file = true,
            .function = true
        });
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&]() {
            Common::Logger::RemoveStream(&logStream);
        });

        // declared after the log stream, so it runs before the stream is taken away, on
        // every path out of Run including the early returns and a throw
        Common::Defer releaseResources([this]() {
            ReleaseResources();
        });

        parser.Parse(argc, argv);
        Parameters params = parser.params;
        if(!params.success) {
            Common::Logger::Error("\ncould not parse parameters:\n{}", params.error.c_str());
            Common::Logger::Info("\n{}", parser.Help().c_str());
            return 1;
        }
        if(params.helpRequested) {
            return 0;
        }

        bool ok = false;
        try {
            ok = Run(params);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nerror during run:\n{}", e.what());
        }
        return ok ? 0 : 1;
    }

};

Program program;

int main(int argc, char **argv) {
    // constructed before anything else starts a thread, on linux a thread inherits the
    // signal mask and the blocking has to be in place by then
    SignalHandler signalHandler({ SIGINT, SIGTERM }, [](int sig){
        Common::Logger::Info("signal {} received, stopping", sig);
        program.Stop();
    });

    return program.Main(argc, argv);
}


