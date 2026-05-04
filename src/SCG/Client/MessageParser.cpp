#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include "MessageParser.hpp"

namespace Sagax::SCG {

    namespace {
        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return char(std::tolower(c)); });
            return s;
        }
    }


    CommandMessage CommandMessageParser::Parse(const std::string &s) {
        nlohmann::json j = nlohmann::json::parse(s);
        CommandMessage msg;
        std::string type = ToLower(j.value("type", ""));

        if (type == "teststart" || type == "test_start") {
            msg.variant = CommandMessage::TestStart{
                j.value("scenario", ""),
                j.value("vehicle", ""),
                j.value("customer", "")
            };
            return msg;
        }

        if (type == "testend" || type == "test_end") {
            msg.variant = CommandMessage::TestEnd{};
            return msg;
        }

        if (type == "testupdate" || type == "test_update") {
            msg.variant = CommandMessage::TestUpdate{};
            return msg;
        }

        if (type == "intstart" || type == "int_start") {
            msg.variant = CommandMessage::IntStart{
                j.value("profile", "")
            };
            return msg;
        }

        if (type == "intstop" || type == "int_stop") {
            msg.variant = CommandMessage::IntStop{};
            return msg;
        }

        // fallback to TestUpdate for unknown/empty types
        msg.variant = CommandMessage::TestUpdate{};
        return msg;
    }

    std::string CommandMessageParser::Serialize(const CommandMessage &t) {
        nlohmann::json j;
        std::visit([&](auto &&v){
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, CommandMessage::TestStart>) {
                j["type"] = "TestStart";
                j["scenario"] = v.scenario;
                j["vehicle"] = v.vehicle;
                j["customer"] = v.customer;
            }
            else if constexpr (std::is_same_v<V, CommandMessage::TestEnd>) {
                j["type"] = "TestEnd";
            }
            else if constexpr (std::is_same_v<V, CommandMessage::TestUpdate>) {
                j["type"] = "TestUpdate";
            }
            else if constexpr (std::is_same_v<V, CommandMessage::IntStart>) {
                j["type"] = "IntStart";
                j["profile"] = v.profile;
            }
            else if constexpr (std::is_same_v<V, CommandMessage::IntStop>) {
                j["type"] = "IntStop";
            }
        }, t.variant);
        return j.dump();
    }

    LogMessage LogMessageParser::Parse(const std::string &s) {
        nlohmann::json j = nlohmann::json::parse(s);
        LogMessage m;
        std::string type = ToLower(j.value("type", "none"));
        std::string status = ToLower(j.value("status", "inactive"));

        if (type == "spoofing") m.type = LogMessage::Type::spoofing;
        else if (type == "jamming") m.type = LogMessage::Type::jamming;
        else m.type = LogMessage::Type::none;

        if (status == "active") m.status = LogMessage::Status::active;
        else m.status = LogMessage::Status::inactive;

        return m;
    }

    std::string LogMessageParser::Serialize(const LogMessage &t) {
        nlohmann::json j;
        switch (t.type) {
            case LogMessage::Type::spoofing: j["type"] = "spoofing"; break;
            case LogMessage::Type::jamming: j["type"] = "jamming"; break;
            default: j["type"] = "none"; break;
        }
        j["status"] = (t.status == LogMessage::Status::active) ? "active" : "inactive";
        return j.dump();
    }

} // namespace Sagax::SCG
