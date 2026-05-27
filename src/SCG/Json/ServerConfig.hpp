#pragma once
#include "SCG/Sisi/ServerConfig.hpp"

namespace Sagax::SCG {

    ServerConfig ParseServerConfig(const std::string &s);

    std::string SerializeServerConfig(const ServerConfig &t);

}


