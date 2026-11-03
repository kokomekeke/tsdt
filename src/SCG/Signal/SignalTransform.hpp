#pragma once
#include <vector>
#include <memory>
#include <tuple>
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/SignalVariant.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<SignalGenerator>,
        std::vector<Resource*>
    >
    // the generator plays v for an event of duration_s
    GetSignalGenerator(
        const PipelineConfig &config,
        const SignalVariant &v,
        double duration_s
    );

}
