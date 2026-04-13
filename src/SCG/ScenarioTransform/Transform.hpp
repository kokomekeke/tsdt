#pragma once
#include <vector>
#include <string>
#include <memory>
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Output/OutputDevice.hpp"

namespace Sagax::SCG {

    std::unique_ptr<OutputDevice> GetOutputDevice(OutputVariant &v);
    std::unique_ptr<SignalGenerator> GetSignalGenerator(SignalVariant &v);

    struct ValidateScenarioResult {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidateScenarioResult ValidateScenario(Scenario &scen);

}

