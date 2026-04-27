#pragma once
#include <vector>
#include "Pipeline/PipelineBuilder.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {

    struct PipelineBuildResult {
        Sagax::Pipeline::SimplePipelineBuilder builder;
        std::vector<Resource*> resources;
    };

    PipelineBuildResult BuildPlaybackPipeline(Scenario &scen);  // File --> Radio
    PipelineBuildResult BuildRecordingPipeline(Scenario &scen); // Signal --> [...] --> File
    PipelineBuildResult BuildStreamingPipeline(Scenario &scen); // Signal --> [...] --> Radio
}


