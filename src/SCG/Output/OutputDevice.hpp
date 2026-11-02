#pragma once
#include <span>
#include <vector>
#include <chrono>
#include <optional>
#include "SCG/Signal/Sample.hpp"

namespace Sagax::SCG {

    // the first Take passes the system time of its first sample, every later sample follows
    // it. samples whose time has passed when the device gets them are dropped.
    // Take blocks while the device buffer is full. Flush blocks until the device has sent
    // every sample it took. Stop stops at once and drops what the device still holds
    struct OutputDevice {
        virtual ~OutputDevice() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        // one span per channel, all of the same size
        virtual void Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) = 0;
        virtual void Flush() = 0;
    };

}
