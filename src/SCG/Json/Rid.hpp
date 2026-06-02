#pragma once
#include "SCG/Sisi/Rid.hpp"

namespace Sagax::SCG {


    Rid ParseRid(const std::string &s);
    std::string SerializeRid(const Rid &t);

    std::string SerializeRidInterferenceSource(const Rid &t, std::int32_t i);
}



