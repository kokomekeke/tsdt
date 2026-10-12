#pragma once
#include <optional>
#include <string>
#include "Json/JsonParser.hpp"
#include "SCG/Sisi/ServerConfig.hpp"

namespace Sagax::SCG {

    bool ParseServerConfig(const std::string &s, ServerConfig &out, Json::JsonIssues &e);

    std::optional<std::string> SerializeServerConfig(const ServerConfig &t, Json::JsonIssues &e);

}
