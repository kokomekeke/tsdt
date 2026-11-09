#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
#include <numbers>
#include <utility>
#include <mutex>
#include <chrono>

#include "Wrappers/JsonSane.hpp"
#include "Common/Logger.hpp"
#include "Common/Assert.hpp"
#include "Common/Defer.hpp"
#include "Common/Timer.hpp"

#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"
#include "SCG/Scenario/BuildPipeline.hpp"
#include "SCG/Utility/TimeFormat.hpp"

#include "Program.hpp"

namespace Sagax::SCG::Cli {

    Scenario Program::ParseFile(std::string jsonPath) {
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

    ScenarioPipeline Program::SetupRecording(std::string jsonPath, std::string dirPath) {
        return BuildRecordingPipeline(pipelineConfig, ParseFile(jsonPath), dirPath);
    }

    ScenarioPipeline Program::SetupPlayback(std::string dirPath) {
        return BuildPlaybackPipeline(pipelineConfig, dirPath);
    }

    ScenarioPipeline Program::SetupStreaming(std::string jsonPath) {
        return BuildStreamingPipeline(pipelineConfig, ParseFile(jsonPath));
    }

    void Program::Configure() {
        pipelineConfig.radioDoLog = true;
        pipelineConfig.radioLogInterval = std::chrono::milliseconds(5000);
        #ifdef HAVE_USRP
            pipelineConfig.preinit.usrp.usePreinitOnly = false;
        #endif
    }

    // the pipeline owns the devices and goes first, then the thread pool its engine used
    // uhd keeps dispatching log records on a thread of its own after that
    // libsidekiq outlives the devices it handed out
    // mtxStop is held, clearing ctx destroys the engine Stop() reads
    void Program::ReleaseResources() {
        std::unique_lock lck(mtxStop);
        ctx.Dispose();
        ctx = ScenarioPipeline{};
        threadPool.Dispose();
        #ifdef HAVE_USRP
            SSG::UsrpB200::DisposeLogging();
        #endif
        #ifdef HAVE_SIDEKIQ
            SSG::Sidekiq::LibraryCleanup();
        #endif
    }

    bool Program::Run(Parameters params) {
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

        // the pool keeps 4 threads around and grows on demand
        threadPool.Init(4);
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
            ctx.Authorize();
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
        Common::Logger::Info("time: {}ms", time_ms);
        return ok;
    }

    void Program::Stop() {
        std::unique_lock lck(mtxStop);
        stopRequested = true;
        if(started) {
            ctx.Stop(false);
        }
    }

    int Program::Main(Parameters params) {
        // SetCurrentThreadName("main");
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .file = true,
            .function = true
        });
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&]() {
            Common::Logger::RemoveStream(&logStream);
        });

        Common::Defer releaseResources([this]() {
            ReleaseResources();
        });

        if(!params.success) {
            Common::Logger::Error("\ncould not parse parameters:\n{}", params.error.c_str());
            Common::Logger::Info("\n{}", params.help.c_str());
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

}
