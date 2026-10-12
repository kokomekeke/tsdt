#pragma once
#include <optional>
#include <string>
#include "Json/JsonParser.hpp"
#include "SCG/Sisi/Tsd.hpp"

namespace Sagax::SCG {

    bool ParseTsd(const std::string &s, Tsd &out, Json::JsonIssues &e);
    std::optional<std::string> SerializeTsd(const Tsd &t, Json::JsonIssues &e);

}
