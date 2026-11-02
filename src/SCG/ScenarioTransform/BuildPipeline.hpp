#pragma once
#include <string>
#include "SCG/Scenario/Scenario.hpp"
#include "ScenarioPipeline.hpp"
#include "PipelineConfig.hpp"

namespace Sagax::SCG {

    // the returned pipelines still need Setup

    // Recording --> Output
    ScenarioPipeline BuildPlaybackPipeline(
        const PipelineConfig &config,
        const std::string &recordingDirectory
    );

    // Signal --> Recording
    ScenarioPipeline BuildRecordingPipeline(
        const PipelineConfig &config,
        const Scenario &scen,
        const std::string &recordingDirectory
    );

    // Signal --> Output
    ScenarioPipeline BuildStreamingPipeline(
        const PipelineConfig &config,
        const Scenario &scen
    );
}
