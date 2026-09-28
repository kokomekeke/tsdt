#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "SCG/Utility/Job.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "ServerConfig.hpp"
#include "Rid.hpp"

namespace Sagax::SCG {

    ValidationResult ValidateConfig(const ServerConfig &cfg);
    ValidationResult ValidateRid(const ServerConfig &cfg, const Rid &rid);



    struct ScenarioRidMap {
        Scenario scen;
        // sink event id -> index into rid.interferenceSources
        std::vector<std::int32_t> eventMap;
        // the scenario reads the files these write, they have to finish before it plays
        std::vector<std::unique_ptr<Job>> jobs;
    };

    // startTime is the time the scenario starts at, the jobs write their files into directory
    ScenarioRidMap MapRidToScenario(
        const ServerConfig &cfg,
        const Rid &rid,
        std::chrono::system_clock::time_point startTime,
        const std::string &directory
    );


}



