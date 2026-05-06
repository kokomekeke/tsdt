#pragma once
#include <memory>
#include <vector>
#include "Pipeline/PipelineBuilder.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {

    struct PipelineBuildResult {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
        std::vector<std::unique_ptr<Resource>> ownedResources;
    };

    PipelineBuildResult BuildPlaybackPipeline(Scenario &scen, const std::string &recordingDirectory);  // File --> Radio
    PipelineBuildResult BuildRecordingPipeline(Scenario &scen, const std::string &recordingDirectory); // Signal --> [...] --> File
    PipelineBuildResult BuildStreamingPipeline(Scenario &scen); // Signal --> [...] --> Radio
}


