#pragma once
#include <optional>
#include <string>
#include "Json/JsonParser.hpp"
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {

    bool ParseScenario(
        const std::string &str,
        std::optional<std::string> scenarioNameFallback,
        Scenario &out,
        Json::JsonIssues &e
    );

    std::optional<std::string> SerializeScenario(
        const Scenario &scen,
        Json::JsonIssues &e
    );

};
