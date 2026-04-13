#pragma once
#include <string>
#include <vector>
#include "SignalVariant.hpp"
#include "OutputVariant.hpp"

namespace Sagax::SCG {

    struct Scenario {
        struct Output {
            std::string name;
            OutputVariant variant;
        };

        struct Signal {
            std::string name;
            SignalVariant variant;
        };

        struct Event {
            double start;
            double end;
            double amplitude;
            double frequency;

            std::string signalName;
            std::string outputName;
            std::size_t iChannel;

            auto operator<=>(const Event &other) const = default;
        };

        std::string name;
        double duration;
        std::vector<Output> outputs;
        std::vector<Signal> signals;
        std::vector<Event> events;
    };

};