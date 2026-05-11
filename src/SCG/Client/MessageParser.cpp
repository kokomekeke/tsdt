#include "MessageParser.hpp"
#include "SCG/Parser2/JsonParser.hpp"

namespace Sagax::SCG {

    namespace {

        struct CommandMessageJsonParser : Json::JsonObjectParser<CommandMessage> {
            struct TestStartParser : Json::JsonObjectParser<CommandMessage::TestStart> {
                TestStartParser() {
                    AddParam(&CommandMessage::TestStart::scenario, Json::JsonPrimitiveParser<std::string>{}, "scenario");
                    AddParam(&CommandMessage::TestStart::vehicle, Json::JsonPrimitiveParser<std::string>{}, "vehicle");
                    AddParam(&CommandMessage::TestStart::customer, Json::JsonPrimitiveParser<std::string>{}, "customer");
                }
            };

            struct TestEndParser : Json::JsonObjectParser<CommandMessage::TestEnd> {
                TestEndParser() {}
            };

            struct TestUpdateParser : Json::JsonObjectParser<CommandMessage::TestUpdate> {
                TestUpdateParser() {}
            };

            struct IntStartParser : Json::JsonObjectParser<CommandMessage::IntStart> {
                IntStartParser() {
                    AddParam(&CommandMessage::IntStart::profile, Json::JsonPrimitiveParser<std::string>{}, "profile");
                }
            };

            struct IntStopParser : Json::JsonObjectParser<CommandMessage::IntStop> {
                IntStopParser() {}
            };

            struct CommandMessageVariantParser : Json::JsonVariantParser<CommandMessage::Variant> {
                CommandMessageVariantParser() {
                    typeTagKey = "type";
                    AddVariant<CommandMessage::TestStart>("TestStart", TestStartParser{});
                    AddVariant<CommandMessage::TestEnd>("TestEnd", TestEndParser{});
                    AddVariant<CommandMessage::TestUpdate>("TestUpdate", TestUpdateParser{});
                    AddVariant<CommandMessage::IntStart>("IntStart", IntStartParser{});
                    AddVariant<CommandMessage::IntStop>("IntStop", IntStopParser{});
                }
            };

            CommandMessageJsonParser() {
                AddParam(&CommandMessage::variant, CommandMessageVariantParser{}, "variant");
            }
        };

        struct LogMessageJsonParser : Json::JsonObjectParser<LogMessage> {
            struct LogTypeParser : Json::JsonEnumParser<LogMessage::Type> {
                LogTypeParser() {
                    AddEnumerator(LogMessage::Type::spoofing, "spoofing");
                    AddEnumerator(LogMessage::Type::jamming, "jamming");
                    AddEnumerator(LogMessage::Type::none, "none");
                }
            };

            struct LogStatusParser : Json::JsonEnumParser<LogMessage::Status> {
                LogStatusParser() {
                    AddEnumerator(LogMessage::Status::active, "active");
                    AddEnumerator(LogMessage::Status::inactive, "inactive");
                }
            };

            LogMessageJsonParser() {
                AddParam(&LogMessage::type, LogTypeParser{}, "type");
                AddParam(&LogMessage::status, LogStatusParser{}, "status");
            }
        };

    }

    CommandMessage ParseCommandMessage(const std::string &s) {
        Json::JsonHandle j = Json::ParseJson(s);
        CommandMessage msg;
        CommandMessageJsonParser parser;
        parser.Parse(j, msg);
        return msg;
    }

    std::string SerializeCommandMessage(const CommandMessage &t) {
        Json::JsonHandle j;
        CommandMessageJsonParser parser;
        parser.Serialize(t, j);
        return Json::DumpJson(j);
    }

    LogMessage ParseLogMessage(const std::string &s) {
        Json::JsonHandle j = Json::ParseJson(s);
        LogMessage m;
        LogMessageJsonParser parser;
        parser.Parse(j, m);
        return m;
    }

    std::string SerializeLogMessage(const LogMessage &t) {
        Json::JsonHandle j;
        LogMessageJsonParser parser;
        parser.Serialize(t, j);
        return Json::DumpJson(j);
    }

}
