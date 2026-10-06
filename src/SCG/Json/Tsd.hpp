#pragma once
#include <string>
#include "SCG/Sisi/Tsd.hpp"

namespace Sagax::SCG {

    Tsd ParseTsd(const std::string &s);
    std::string SerializeTsd(const Tsd &t);

}
