#pragma once
#include <optional>
#include <string>
#include "Json/JsonParser.hpp"
#include "SCG/Sisi/Rid.hpp"
#include "SCG/Sisi/Messages.hpp"
#include "SCG/Sisi/SavedTest.hpp"

namespace Sagax::SCG {


    bool ParseRid(const std::string &s, Rid &out, Json::JsonIssues &e);
    std::optional<std::string> SerializeRid(const Rid &t, Json::JsonIssues &e);

    std::optional<std::string> SerializeRidInterferenceSource(
        const Rid &t,
        std::int32_t i,
        Json::JsonIssues &e
    );


    // these are here because log messages need to include part of the RID

    bool ParseCommandMessage(const std::string &s, CommandMessage &out, Json::JsonIssues &e);
    std::optional<std::string> SerializeCommandMessage(const CommandMessage &t, Json::JsonIssues &e);

    bool ParseTestStatusMessage(const std::string &s, TestStatusMessage &out, Json::JsonIssues &e);
    std::optional<std::string> SerializeTestStatusMessage(const TestStatusMessage &t, Json::JsonIssues &e);

    bool ParseSignalStatusMessage(const std::string &s, SignalStatusMessage &out, Json::JsonIssues &e);
    std::optional<std::string> SerializeSignalStatusMessage(const SignalStatusMessage &t, Json::JsonIssues &e);

    bool ParseSessionLogMessage(const std::string &s, SessionLogMessage &out, Json::JsonIssues &e);
    std::optional<std::string> SerializeSessionLogMessage(const SessionLogMessage &t, Json::JsonIssues &e);

    bool ParseSavedTest(const std::string &s, SavedTest &out, Json::JsonIssues &e);
    std::optional<std::string> SerializeSavedTest(const SavedTest &t, Json::JsonIssues &e);

}
