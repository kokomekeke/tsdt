#pragma once
#include <vector>
#include <string>
#include "Scenario.hpp"

namespace Sagax::SCG {

    struct ValidationResult {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidationResult ValidateScenario(Scenario &scen);

}


