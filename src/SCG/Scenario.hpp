#pragma once
#include <memory>
#include <vector>
#include "Pipeline/Pipeline.hpp"
#include "Pipeline/PipelineBuilder.hpp"

namespace Sagax::SCG {
    using  Sagax::Pipeline::Module;
    using  Sagax::Pipeline::Pipeline;
    using  Sagax::Common::Graph_el;

    struct Output {
        std::string name;
        std::size_t nChannels;
        double sampleRate;
        std::vector<double> frequencies;
        Sagax::Pipeline::PipelineBuilder builder;
    };

    struct Signal {
        std::string name;
        Sagax::Pipeline::PipelineBuilder builder;
    };

    struct Event {
        double start;
        double end;
        double amplitude;
        double frequency;

        std::size_t iSignal;
        std::size_t iOutput;
        std::size_t iChannel;
    };

    struct Scenario {
        std::string name; // used for naming output/input files
        std::vector<Output> outputs;
        std::vector<Signal> signals;
        std::vector<Event> events;
        double duration = 0;
    };


    struct ScenarioTransformer {
        struct Impl;
        std::unique_ptr<Impl> impl;

        ScenarioTransformer(Scenario&);
        ~ScenarioTransformer();

        // File --> Radio
        Sagax::Pipeline::PipelineBuilder ToPlaybackPipeline();

        // Signal --> [...] --> File
        Sagax::Pipeline::PipelineBuilder ToRecordingPipeline();

        // Signal --> [...] --> Radio
        Sagax::Pipeline::PipelineBuilder ToStreamingPipeline();
    };
};