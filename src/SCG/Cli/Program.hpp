#pragma once
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include "Common/ThreadPool.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/ScenarioPipeline.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"

namespace Sagax::SCG::Cli {

    struct Parameters {
        enum struct Mode { stream, record, playback };

        // could use variants but why
        bool success;
        // --help and --version write their text and end the run, there is nothing else to do
        bool helpRequested;
        std::string error;
        // written after a parse error
        std::string help;
        Mode mode;
        std::string jsonPath;
        std::string recordingDirectory;
        std::optional<std::chrono::system_clock::time_point> startTime;
    };

    struct Program {
        PipelineConfig pipelineConfig;

        // the engine of ctx dispatches its workers here. Run starts the threads, after the
        // signal handler is in place, and ReleaseResources stops them once ctx is gone
        Common::ThreadPool threadPool;
        ScenarioPipeline ctx;

        // Stop() runs on the signal thread while Run() is still putting ctx together, so ctx
        // and started are only read and written under this. stopRequested carries a signal
        // that arrived before ctx was started
        std::mutex mtxStop;
        bool stopRequested = false;
        bool started = false;

        Scenario ParseFile(std::string jsonPath);
        ScenarioPipeline SetupRecording(std::string jsonPath, std::string dirPath);
        ScenarioPipeline SetupPlayback(std::string dirPath);
        ScenarioPipeline SetupStreaming(std::string jsonPath);
        void Configure();
        void ReleaseResources();
        bool Run(Parameters params);
        void Stop();
        // sets up logging, then reports a parse error or runs
        int Main(Parameters params);
    };

}
