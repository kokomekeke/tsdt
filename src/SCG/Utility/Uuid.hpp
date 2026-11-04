#pragma once
#include <random>
#include <string>

namespace Sagax::SCG {

    // random uuid v4 in lowercase hex, for example 879d3ec1-0147-4d53-81e3-267b6b9bdb54
    std::string GenerateUuid(std::mt19937_64 &generator);

}
