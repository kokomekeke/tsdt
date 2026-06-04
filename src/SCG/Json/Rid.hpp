#pragma once
#include <string>
#include "SCG/Sisi/Rid.hpp"
#include "SCG/Sisi/Messages.hpp"

namespace Sagax::SCG {


    Rid ParseRid(const std::string &s);
    std::string SerializeRid(const Rid &t);

    std::string SerializeRidInterferenceSource(const Rid &t, std::int32_t i);


    // these are here because log messages need to include part of the RID

    CommandMessage ParseCommandMessage(const std::string &s);
    std::string SerializeCommandMessage(const CommandMessage &t);

    LogMessage ParseLogMessage(const std::string &s);
    std::string SerializeLogMessage(const LogMessage &t);
}



