#include "Json/JsonParser.hpp"
#include "ServerConfig.hpp"

using namespace Sagax::Json;

namespace Sagax::SCG {
    namespace {

        struct RadioParser : public JsonObjectParser<ServerConfig::Radio> {
            struct RadioVariantParser : public JsonVariantParser<ServerConfig::Radio::Variant> {
                struct UsrpB200Parser : public JsonObjectParser<ServerConfig::Radio::UsrpB200> {
                    UsrpB200Parser() {
                        AddParam(&ServerConfig::Radio::UsrpB200::serial, JsonPrimitiveParser<std::string>{}, "serial");
                    }
                };
                struct SidekiqParser : public JsonObjectParser<ServerConfig::Radio::Sidekiq> {
                    SidekiqParser() {
                        AddParam(&ServerConfig::Radio::Sidekiq::serial, JsonPrimitiveParser<std::string>{}, "serial");
                    }
                };

                RadioVariantParser() {
                    this->typeTagKey = "type";
                    AddAlt<ServerConfig::Radio::UsrpB200>("UsrpB200", "parameters", UsrpB200Parser{});
                    AddAlt<ServerConfig::Radio::Sidekiq>("Sidekiq", "parameters", SidekiqParser{});
                }
            };

            RadioParser() {
                AddParam(&ServerConfig::Radio::id, JsonPrimitiveParser<std::string>{}, "id");
                AddParamFlat(&ServerConfig::Radio::variant, RadioVariantParser{});
            }
        };

        struct MqttParser : public JsonObjectParser<ServerConfig::Mqtt> {
            MqttParser() {
                AddParam(&ServerConfig::Mqtt::brokerHost, JsonPrimitiveParser<std::string>{}, "broker_host");
                AddParam(&ServerConfig::Mqtt::brokerPort, JsonPrimitiveParser<std::int32_t>{}, "broker_port");
                AddParam(&ServerConfig::Mqtt::commandTopic, JsonPrimitiveParser<std::string>{}, "command_topic");
                AddParam(&ServerConfig::Mqtt::logTopic, JsonPrimitiveParser<std::string>{}, "log_topic");
            }
        };

        struct ServerConfigParser : JsonObjectParser<ServerConfig> {
            ServerConfigParser() {
                auto p0 = AddParam(&ServerConfig::radios, JsonArrayParser<ServerConfig::Radio>{}, "radios");
                AddParam(&ServerConfig::mqtt, MqttParser{}, "mqtt");

                p0->SetItemParser(RadioParser{});
            }
        };

        ServerConfigParser serverConfigParser;
    }

    ServerConfig ParseServerConfig(const std::string &s) {
        JsonHandle j = Json::ParseJson(s);
        ServerConfig cfg;
        serverConfigParser.Parse(j, cfg);
        return cfg;
    }

    std::string SerializeServerConfig(const ServerConfig &t) {
        JsonHandle j;
        serverConfigParser.Serialize(t, j);
        return Json::DumpJson(j, true);
    }



}



