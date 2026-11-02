#pragma once
#include <vector>
#include <memory>
#include <tuple>
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/OutputVariant.hpp"
#include "SCG/Output/OutputDevice.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<OutputDevice>,
        std::vector<Resource*>
    >
    GetOutputDevice(
        const PipelineConfig &config,
        const OutputVariant &v
    );

}
