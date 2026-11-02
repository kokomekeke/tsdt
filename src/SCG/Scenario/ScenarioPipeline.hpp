#pragma once
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "Common/ThreadPool.hpp"
#include "Pipeline3/PipelineEngine.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Output/OutputDevice.hpp"
#include "SCG/Utility/Resource.hpp"
#include "PipelineConfig.hpp"

namespace Sagax::SCG {

    // what a scenario plays, in samples of its outputs. sample ranges are [iStart, iEnd)
    struct ScenarioLayout {
        enum class Mode { stream, record, playback };

        struct Event {
            std::int32_t id; // index of the event in the scenario
            std::size_t iOutput;
            std::size_t iChannel;
            std::int64_t iStart;
            std::int64_t iEnd;
            double frequency;
            double amplitude;
            std::unique_ptr<SignalGenerator> generator; // null in playback mode
        };

        // events of one channel of an output that have the same WorkType and do not overlap, in order
        struct Line {
            std::size_t iOutput;
            std::size_t iChannel;
            SignalGenerator::WorkType workType;
            std::vector<std::size_t> events;
        };

        struct Output {
            double sampleRate;
            std::size_t nChannels;
            std::vector<double> centers;
            std::int64_t nSamples;
            std::vector<std::size_t> lines;
            std::vector<std::size_t> events; // every event on the output, ordered by iStart
            std::unique_ptr<OutputDevice> device; // stream and playback mode
            std::vector<std::string> files; // one per channel, written in record mode, read in playback mode
        };

        Mode mode;
        // events without samples are left out
        std::vector<Event> events;
        std::vector<Line> lines; // empty in playback mode
        std::vector<Output> outputs;
        PipelineConfig::EventLogConfig callbacks; // stream and playback mode
    };

    // at least 1
    std::int32_t GetDefaultCpuThreadCount();

    // runs one scenario. the order of the calls is Setup, Init, Start, Join or Stop, Dispose.
    // the outputs advance together in passes of scgBlockSize samples of the fastest output
    struct ScenarioPipeline : public Resource {
        struct Impl;

        Scenario scenario;
        std::vector<Resource*> resources;
        std::vector<std::unique_ptr<Resource>> ownedResources;

        ScenarioPipeline();
        ~ScenarioPipeline();
        ScenarioPipeline(ScenarioPipeline &&other);
        ScenarioPipeline &operator=(ScenarioPipeline &&other);

        void SetLayout(ScenarioLayout &&layout);
        // the system time of the first sample, stream and playback mode need one before Start.
        // replaces the one the layout came with
        void SetStartTime(std::chrono::system_clock::time_point startTime);
        // nPackets is the larger of 16 and 2 * nCpuThreads
        void Setup(Common::ThreadPool &threadPool, std::int32_t nCpuThreads);

        void Start();
        // returns without blocking. flush=false interrupts the wait for the start time
        void Stop(bool flush);
        void Join();
        Pipeline3::PipelineEngine::RunState GetState();
        std::exception_ptr GetError(); // null if there is none

    protected:
        void Init_() override;
        void Dispose_() override;

    private:
        std::unique_ptr<Impl> impl;
    };
}
