#pragma once
#include <span>
#include <vector>
#include "SCG/Signal/Sample.hpp"

namespace Sagax::SCG {

    struct OutputDevice {
        virtual ~OutputDevice() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        // one span per channel, all of the same size
        virtual void Take(const std::vector<std::span<const SCGSample>> &channels) = 0;
    };

}
