#pragma once
#include <vector>
#include <functional>
#include "Parser/Parser.hpp"
#include "Parser/JsonParser.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/OutputVariant.hpp"
#include "SCG/Scenario/SignalVariant.hpp"
#include "SCG/Json/JsonParser.hpp"

namespace Sagax::SCG {


    Scenario ParseScenario(
        const std::string &str,
        std::optional<std::string> scenarioNameFallback
    );

    std::string SerializeScenario(
        const Scenario &scen
    );

};