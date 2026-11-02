#pragma once
#include <vector>
#include <memory>
#include <tuple>
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<SignalGenerator>,
        std::vector<Resource*>
    >
    // the generator plays v for event
    GetSignalGenerator(
        const PipelineConfig &config,
        const SignalVariant &v,
        const Scenario::Event &event
    );

}
