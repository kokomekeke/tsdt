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
                        auto arrH = AddParam(&ServerConfig::Radio::Sidekiq::handlers, JsonArrayParser<std::string>{}, "handlers");
                        arrH->SetItemParser(JsonPrimitiveParser<std::string>{});
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
                AddParam(&ServerConfig::Mqtt::signalLogTopic, JsonPrimitiveParser<std::string>{}, "signal_log_topic");
                AddParam(&ServerConfig::Mqtt::testLogTopic, JsonPrimitiveParser<std::string>{}, "test_log_topic");
                AddParam(&ServerConfig::Mqtt::errorLogTopic, JsonPrimitiveParser<std::string>{}, "error_log_topic");
            }
        };

        struct ServerConfigParser : JsonObjectParser<ServerConfig> {
            ServerConfigParser() {
                AddParam(&ServerConfig::recordingDir, JsonPrimitiveParser<std::string>{}, "recording_directory");
                AddParam(&ServerConfig::mockRadios, JsonPrimitiveParser<bool>{}, "mock_radios");
                AddParamDef(&ServerConfig::mockGnss, JsonPrimitiveParser<bool>{}, false, "mock_gnss");
                AddParamDef(&ServerConfig::prerecordGnss, JsonPrimitiveParser<bool>{}, true, "prerecord_gnss");
                AddParamDef(&ServerConfig::gnssPrepareRate, JsonPrimitiveParser<double>{}, 1.0, "gnss_prepare_rate");
                AddParamDef(&ServerConfig::logMqtt, JsonPrimitiveParser<bool>{}, true, "log_mqtt");
                AddParam(&ServerConfig::statusUpdatePeriod_s, JsonPrimitiveParser<double>{}, "status_update_period_s");
                AddParam(&ServerConfig::startAhead_s, JsonPrimitiveParser<double>{}, "start_ahead_s");
                AddParamDef(&ServerConfig::scenarioFetchTimeout_s, JsonPrimitiveParser<double>{}, 2.0, "scenario_fetch_timeout_s");
                AddParamDef(&ServerConfig::gnssEphemerisFile, JsonPrimitiveParser<std::string>{}, std::string(""), "gnss_ephemeris_file");
                auto p0 = AddParam(&ServerConfig::radios, JsonArrayParser<ServerConfig::Radio>{}, "radios");
                AddParam(&ServerConfig::mqtt, MqttParser{}, "mqtt");

                p0->SetItemParser(RadioParser{});
            }
        };

        ServerConfigParser serverConfigParser;
    }

    bool ParseServerConfig(const std::string &s, ServerConfig &out, Json::JsonIssues &e) {
        return Json::ParseDocument<ServerConfig>(serverConfigParser, s, out, e);
    }

    std::optional<std::string> SerializeServerConfig(const ServerConfig &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<ServerConfig>(serverConfigParser, t, true, e);
    }



}



