#pragma once
#include <string>
#include "SCG/Sisi/Messages.hpp"

namespace Sagax::SCG {

    CommandMessage ParseCommandMessage(const std::string &s);
    std::string SerializeCommandMessage(const CommandMessage &t);

    LogMessage ParseLogMessage(const std::string &s);
    std::string SerializeLogMessage(const LogMessage &t);
}


