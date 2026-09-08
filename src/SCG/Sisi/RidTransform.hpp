#pragma once
#include "SCG/Scenario/Validate.hpp"
#include "ServerConfig.hpp"
#include "Rid.hpp"

namespace Sagax::SCG {

    ValidationResult ValidateConfig(const ServerConfig &cfg);
    ValidationResult ValidateRid(const ServerConfig &cfg, const Rid &rid);



    struct ScenarioRidMap {
        Scenario scen;
        std::vector<std::int32_t> eventMap;
    };
    ScenarioRidMap MapRidToScenario(const ServerConfig &cfg, const Rid &rid);


}



