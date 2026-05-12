#pragma once
#include <vector>
#include <string>
#include "Scenario.hpp"

namespace Sagax::SCG {

    struct ValidateScenarioResult {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidateScenarioResult ValidateScenario(Scenario &scen);

}


