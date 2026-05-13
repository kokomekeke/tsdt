#pragma once
#include <vector>
#include <memory>
#include "Pipeline/Pipeline.hpp"
#include "Pipeline/Controller.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Utility/Resource.hpp"

namespace Sagax::SCG {

    struct ScenarioRunnerContext : public Resource {
        Scenario scenario;
        Pipeline::Pipeline pipeline;
        std::unique_ptr<Pipeline::Controller> controller;
        std::vector<Resource*> resources;
        std::vector<std::unique_ptr<Resource>> ownedResources;

        void Init_() {
            for(auto &p : ownedResources) {
                p->Init();
            }
            for(auto p : resources) {
                p->Init();
            }
        }

        void Dispose_() {
            for(auto p : resources) {
                p->Dispose();
            }
            for(auto &p : ownedResources) {
                p->Dispose();
            }
        }
    };
}




