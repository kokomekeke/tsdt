#pragma once
#include <memory>
#include <vector>
#include "Pipeline/PipelineBuilder.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "RecordingDirectoryResource.hpp"
#include "ScenarioRunnerContext.hpp"

namespace Sagax::SCG {

    struct PlaybackPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
        std::unique_ptr<RecordingDirectoryResource> dir;
    };
    PlaybackPipelineBuild BuildPlaybackPipeline(const std::string &recordingDirectory);  // Recording --> Output

    struct RecordingPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
        std::unique_ptr<RecordingDirectoryResource> dir;
    };
    RecordingPipelineBuild BuildRecordingPipeline(Scenario &scen, const std::string &recordingDirectory); // Signal --> Recording

    struct StreamingPipelineBuild {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
    };
    StreamingPipelineBuild BuildStreamingPipeline(Scenario &scen); // Signal --> Output
}


