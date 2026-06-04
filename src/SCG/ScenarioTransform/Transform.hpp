#pragma once
#include <vector>
#include <string>
#include <memory>
#include <tuple>
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Output/OutputDevice.hpp"
#include "PipelineConfig.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<OutputDevice>,
        std::vector<Resource*>
    >
    GetOutputDevice(
        const PipelineConfig &config,
        const OutputVariant &v
    );

    std::tuple<
        std::unique_ptr<SignalGenerator>,
        std::vector<Resource*>
    >
    GetSignalGenerator(
        const PipelineConfig &config,
        const SignalVariant &v
    );

}

