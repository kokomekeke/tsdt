#pragma once
#include <chrono>
#include <string>
#include "CommandError.hpp"

namespace Sagax::SCG {

    // 2026-09-02T14:30:00.000+02:00. the fraction is optional and can have any length,
    // Z means a zero offset and the colon in the offset is optional
    CommandError ParseStartTime(const std::string &s, std::chrono::system_clock::time_point &out);

    // utc to the second, for example 2026-09-02T12:30:00Z
    std::string FormatIsoTime(std::chrono::system_clock::time_point t);

}
