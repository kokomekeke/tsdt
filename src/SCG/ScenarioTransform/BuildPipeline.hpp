#pragma once
#include <memory>
#include <vector>
#include "Pipeline/PipelineBuilder.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "RecordingDirectoryResource.hpp"
#include "ScenarioRunnerContext.hpp"
#include "PipelineConfig.hpp"

namespace Sagax::SCG {

    // Recording --> Output
    struct PlaybackPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
        std::unique_ptr<RecordingDirectoryResource> dir;
    };
    PlaybackPipelineBuild BuildPlaybackPipeline(
        const PipelineConfig &config,
        const std::string &recordingDirectory
    );

    // Signal --> Recording
    struct RecordingPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
        std::unique_ptr<RecordingDirectoryResource> dir;
    };
    RecordingPipelineBuild BuildRecordingPipeline(
        const PipelineConfig &config,
        const Scenario &scen,
        const std::string &recordingDirectory
    );

    // Signal --> Output
    struct StreamingPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
    };
    StreamingPipelineBuild BuildStreamingPipeline(
        const PipelineConfig &config,
        const Scenario &scen
    );
}


