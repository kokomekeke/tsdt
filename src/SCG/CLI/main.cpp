#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
#include <numbers>
#include <utility>
#include <csignal>
#include <mutex>
#include <chrono>

#include "Wrappers/JsonSane.hpp"
#include "Common/Logger.hpp"
#include "Common/Assert.hpp"
#include "Common/Defer.hpp"
#include "Common/Timer.hpp"
#include "Common/ThreadPool.hpp"
#include "Vendored/argparse/argparse.hpp"

#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"
#include "SCG/ScenarioTransform/ScenarioPipeline.hpp"
#include "SCG/ScenarioTransform/Transform.hpp"
#include "SCG/ScenarioTransform/BuildPipeline.hpp"
#include "SCG/ScenarioTransform/PipelineConfig.hpp"
#include "SCG/Utility/SignalHandler.hpp"
#include "SCG/Utility/TimeFormat.hpp"

#include "ProgramParser.hpp"

using namespace Sagax;
using namespace Sagax::SCG;



struct Program {

    ProgramParser parser;
    PipelineConfig pipelineConfig;

    // the engine of ctx dispatches its workers here. it is declared before ctx so that it
    // is still alive when ctx goes away. the pool grows on demand, the count is only the
    // number of threads it keeps around
    Common::ThreadPool threadPool{4};
    ScenarioPipeline ctx;

    // Stop() runs on the signal thread while Run() is still putting ctx together, so ctx
    // and started are only read and written under this. stopRequested carries a signal
    // that arrived before ctx was started
    std::mutex mtxStop;
    bool stopRequested = false;
    bool started = false;

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

            Json::JsonIssues issues;
            if(!ParseScenario(jsonStr, scenName, scenario, issues)) {
                throw std::runtime_error(issues.Format());
            }
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

    ScenarioPipeline SetupRecording(std::string jsonPath, std::string dirPath) {
        return BuildRecordingPipeline(pipelineConfig, ParseFile(jsonPath), dirPath);
    }

    ScenarioPipeline SetupPlayback(std::string dirPath) {
        return BuildPlaybackPipeline(pipelineConfig, dirPath);
    }

    ScenarioPipeline SetupStreaming(std::string jsonPath) {
        return BuildStreamingPipeline(pipelineConfig, ParseFile(jsonPath));
    }

    void Configure() {
        #ifdef HAVE_USRP
            pipelineConfig.preinit.usrp.usePreinitOnly = false;
        #endif
    }

    // the pipeline owns the devices, so it goes first. uhd keeps dispatching log records
    // on a thread of its own after that, and libsidekiq outlives the devices it handed
    // out. mtxStop is held because clearing ctx destroys the engine Stop() reads
    void ReleaseResources() {
        std::unique_lock lck(mtxStop);
        ctx.Dispose();
        ctx = ScenarioPipeline{};
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
            ScenarioPipeline res;
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

        {
            std::unique_lock lck(mtxStop);
            ctx.Setup(threadPool, GetDefaultCpuThreadCount());
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
        {
            std::unique_lock lck(mtxStop);
            if(stopRequested) {
                Common::Logger::Info("stopped before start");
                return true;
            }
            // without a start time on the command line the run starts 1 second from here
            if(params.mode != Parameters::Mode::record) {
                auto startTime = params.startTime.has_value() ? params.startTime.value() : std::chrono::system_clock::now() + std::chrono::seconds(1);
                ctx.SetStartTime(startTime);
                Common::Logger::Info("start time: {}", FormatIsoTime(startTime));
            }
            ctx.Start();
            started = true;
        }
        ctx.Join();
        auto time_ms = timer.Get_ms();

        bool ok = true;
        try {
            if(std::exception_ptr error = ctx.GetError()) {
                std::rethrow_exception(error);
            }
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
        if(started) {
            ctx.Stop(false);
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


