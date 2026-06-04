#pragma once
#include <vector>
#include <cstdint>

namespace Sagax::SCG {
    struct MEventTag {
        // contains active event ids for the packets _entire_ duration
        // break packets such that events are active throughout
        std::vector<std::int32_t> events;
        auto operator<=>(const MEventTag &other) const = default;
    };
}

