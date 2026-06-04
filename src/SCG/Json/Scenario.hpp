#pragma once
#include <optional>
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {

    Scenario ParseScenario(
        const std::string &str,
        std::optional<std::string> scenarioNameFallback
    );

    std::string SerializeScenario(
        const Scenario &scen
    );

};