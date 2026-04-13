#pragma once
#include <vector>
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {
    struct MEventTag {
        // break packets such that events are active throughout the entire packet
        std::vector<Scenario::Event> events;
        auto operator<=>(const MEventTag &other) const = default;
    };
}

