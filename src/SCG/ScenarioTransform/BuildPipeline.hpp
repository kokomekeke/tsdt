#pragma once
#include "Pipeline/PipelineBuilder.hpp"
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {
    Sagax::Pipeline::SimplePipelineBuilder BuildPlaybackPipeline(Scenario &scen);  // File --> Radio
    Sagax::Pipeline::SimplePipelineBuilder BuildRecordingPipeline(Scenario &scen); // Signal --> [...] --> File
    Sagax::Pipeline::SimplePipelineBuilder BuildStreamingPipeline(Scenario &scen); // Signal --> [...] --> Radio
}


