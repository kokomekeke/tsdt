#pragma once
#include <vector>
#include <memory>
#include "Pipeline/Pipeline.hpp"
#include "Pipeline/Controller.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Utility/Resource.hpp"

namespace Sagax::SCG {

    struct ScenarioContext {
        Scenario scenario;
        Pipeline::Pipeline pipeline;
        std::unique_ptr<Pipeline::Controller> controller;
        std::vector<Resource*> resources;

        void Init() {
            for(auto p : resources) {
                p->Init();
            }
        }

        void Dispose() {
            for(auto p : resources) {
                p->Dispose();
            }
        }
    };
}




