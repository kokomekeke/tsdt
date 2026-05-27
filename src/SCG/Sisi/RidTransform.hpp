#pragma once
#include "ServerConfig.hpp"
#include "Rid.hpp"

namespace Sagax::SCG {

    struct ValidationResult {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    ValidationResult ValidateConfig(const ServerConfig &cfg);
    ValidationResult ValidateRid(const ServerConfig &cfg, const Rid &rid);



    struct ScenarioRidMap {
        Scenario scen;
        std::vector<std::int32_t> eventMap;
    };
    ScenarioRidMap MapRidToScenario(const ServerConfig &cfg, const Rid &rid);


}



