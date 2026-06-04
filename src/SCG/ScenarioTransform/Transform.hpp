#pragma once
#include <vector>
#include <string>
#include <memory>
#include <tuple>
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Output/OutputDevice.hpp"
#include "PreinitConfig.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<OutputDevice>,
        std::vector<Resource*>
    >
    GetOutputDevice(
        const OutputVariant &v,
        const PreinitConfig &preinit
    );

    std::tuple<
        std::unique_ptr<SignalGenerator>,
        std::vector<Resource*>
    >
    GetSignalGenerator(
        const SignalVariant &v,
        const PreinitConfig &preinit
    );

}

