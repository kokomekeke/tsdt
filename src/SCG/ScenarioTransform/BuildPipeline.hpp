#pragma once
#include <memory>
#include <vector>
#include "Pipeline/PipelineBuilder.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "RecordingDirectoryResource.hpp"
#include "ScenarioRunnerContext.hpp"
#include "PreinitConfig.hpp"

namespace Sagax::SCG {

    // Recording --> Output
    struct PlaybackPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
        std::unique_ptr<RecordingDirectoryResource> dir;
    };
    PlaybackPipelineBuild BuildPlaybackPipeline(
        const std::string &recordingDirectory,
        const PreinitConfig &preinit
    );

    // Signal --> Recording
    struct RecordingPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
        std::unique_ptr<RecordingDirectoryResource> dir;
    };
    RecordingPipelineBuild BuildRecordingPipeline(
        const Scenario &scen,
        const std::string &recordingDirectory,
        const PreinitConfig &preinit
    );

    // Signal --> Output
    struct StreamingPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
    };
    StreamingPipelineBuild BuildStreamingPipeline(
        const Scenario &scen,
        const PreinitConfig &preinit
    );
}


