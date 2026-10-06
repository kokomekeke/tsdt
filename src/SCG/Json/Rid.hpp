#pragma once
#include <string>
#include "SCG/Sisi/Rid.hpp"
#include "SCG/Sisi/Messages.hpp"
#include "SCG/Sisi/SavedTest.hpp"

namespace Sagax::SCG {


    Rid ParseRid(const std::string &s);
    std::string SerializeRid(const Rid &t);

    std::string SerializeRidInterferenceSource(const Rid &t, std::int32_t i);


    // these are here because log messages need to include part of the RID

    CommandMessage ParseCommandMessage(const std::string &s);
    std::string SerializeCommandMessage(const CommandMessage &t);

    TestStatusMessage ParseTestStatusMessage(const std::string &s);
    std::string SerializeTestStatusMessage(const TestStatusMessage &s);

    SignalStatusMessage ParseSignalStatusMessage(const std::string &s);
    std::string SerializeSignalStatusMessage(const SignalStatusMessage &s);


    SavedTest ParseSavedTest(const std::string &s);
    std::string SerializeSavedTest(const SavedTest &t);

}



