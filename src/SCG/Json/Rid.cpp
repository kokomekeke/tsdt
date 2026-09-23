#include "Json/JsonParser.hpp"
#include "Rid.hpp"

using namespace Sagax::Json;

namespace Sagax::SCG {
    namespace {

        struct GlobalSettingsParser : public JsonObjectParser<Rid::GlobalSettings> {
            GlobalSettingsParser() {
                AddParam(&Rid::GlobalSettings::duration_s, JsonPrimitiveParser<double>{}, "duration_s");
            }
        };

        struct MetadataParser : public JsonObjectParser<Rid::Metadata> {
            MetadataParser() {
                AddParam(&Rid::Metadata::version, JsonPrimitiveParser<std::string>{}, "version");
                AddParam(&Rid::Metadata::created_utc, JsonPrimitiveParser<std::string>{}, "created_utc");
                AddParam(&Rid::Metadata::name, JsonPrimitiveParser<std::string>{}, "scenario_name");
                AddParamDef(&Rid::Metadata::author, JsonPrimitiveParser<std::string>{}, std::string(""), "author");
                AddParamDef(&Rid::Metadata::description, JsonPrimitiveParser<std::string>{}, std::string(""), "description");
            }
        };

        struct RadioSettingsParser : public JsonObjectParser<Rid::RadioSettings> {
            RadioSettingsParser() {
                AddParam(&Rid::RadioSettings::radioId, JsonPrimitiveParser<std::string>{}, "radio_id");
                AddParam(&Rid::RadioSettings::bandwidth_hz, JsonPrimitiveParser<double>{}, "bandwidth_hz");
                AddParam(&Rid::RadioSettings::nChannels, JsonPrimitiveParser<std::int32_t>{}, "channels");
                auto arrC = AddParam(&Rid::RadioSettings::centers_hz, JsonArrayParser<double>{}, "center_frequencies_hz");
                auto arrG = AddParam(&Rid::RadioSettings::gains_db, JsonArrayParser<double>{}, "gains_db");

                arrC->SetItemParser(JsonPrimitiveParser<double>{});
                arrG->SetItemParser(JsonPrimitiveParser<double>{});
            }
        };

        struct InterferenceSourceParser : public JsonObjectParser<Rid::InterferenceSource> {

            struct InterferenceVariantParser : public JsonVariantParser<Rid::InterferenceSource::Variant> {
                struct CwParser : public JsonObjectParser<Rid::InterferenceSource::Cw> {
                    CwParser() {
                    }
                };
                struct NoiseParser : public JsonObjectParser<Rid::InterferenceSource::Noise> {
                    NoiseParser() {
                        AddParam(&Rid::InterferenceSource::Noise::span_hz, JsonPrimitiveParser<double>{}, "span_hz");
                    }
                };
                struct ChirpParser : public JsonObjectParser<Rid::InterferenceSource::Chirp> {
                    ChirpParser() {
                        AddParam(&Rid::InterferenceSource::Chirp::sweepSpan_hz, JsonPrimitiveParser<double>{}, "span_hz");
                        AddParam(&Rid::InterferenceSource::Chirp::sweepTime_s, JsonPrimitiveParser<double>{}, "sweep_time_s");
                    }
                };
                struct SpoofingParser : public JsonObjectParser<Rid::InterferenceSource::Spoofing> {
                    struct ConstellationParser : public JsonEnumParser<Rid::InterferenceSource::Spoofing::Constellation> {
                        ConstellationParser() {
                            AddEnumerator(Rid::InterferenceSource::Spoofing::Constellation::gps, "GPS");
                            AddEnumerator(Rid::InterferenceSource::Spoofing::Constellation::galileo, "GALILEO");
                            AddEnumerator(Rid::InterferenceSource::Spoofing::Constellation::glonass, "GLONASS");
                            AddEnumerator(Rid::InterferenceSource::Spoofing::Constellation::beidou, "BEIDOU");
                        }
                    };

                    struct TrajectoryItemParser : public JsonObjectParser<Rid::InterferenceSource::Spoofing::TrajectoryItem> {
                        TrajectoryItemParser() {
                            AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::t, JsonPrimitiveParser<double>{}, "t");
                            AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::longitude, JsonPrimitiveParser<double>{}, "longitude");
                            AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::latitude, JsonPrimitiveParser<double>{}, "latitude");
                            AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::altitude, JsonPrimitiveParser<double>{}, "altitude");
                        }
                    };

                    SpoofingParser() {
                        AddParam(&Rid::InterferenceSource::Spoofing::constellation, ConstellationParser{}, "constellation");
                        auto p0 = AddParam(&Rid::InterferenceSource::Spoofing::pnrList, JsonArrayParser<std::int32_t>{}, "pnr_list");
                        auto p1 = AddParam(&Rid::InterferenceSource::Spoofing::trajectory, JsonArrayParser<Rid::InterferenceSource::Spoofing::TrajectoryItem>{}, "trajectory");

                        p0->SetItemParser(JsonPrimitiveParser<std::int32_t>{});
                        p0->SetSizeRange(1, 210);

                        p1->SetItemParser(TrajectoryItemParser{});
                        p1->SetSizeRange(1, std::nullopt);

                    }
                };

                InterferenceVariantParser() {
                    typeTagKey = "type";
                    AddAlt<Rid::InterferenceSource::Cw>("cw_jammer", "parameters", CwParser{});
                    AddAlt<Rid::InterferenceSource::Noise>("noise_jammer", "parameters", NoiseParser{});
                    AddAlt<Rid::InterferenceSource::Chirp>("chirp_jammer", "parameters", ChirpParser{});
                    AddAlt<Rid::InterferenceSource::Spoofing>("gnss_spoofer", "parameters", SpoofingParser{});
                }
            };

            InterferenceSourceParser() {
                AddParam(&Rid::InterferenceSource::radioId, JsonPrimitiveParser<std::string>{}, "radio_id");
                AddParam(&Rid::InterferenceSource::channelId, JsonPrimitiveParser<std::int32_t>{}, "channel_id");
                AddParam(&Rid::InterferenceSource::center_hz, JsonPrimitiveParser<double>{}, "center_frequency_hz");
                AddParam(&Rid::InterferenceSource::startTime_s, JsonPrimitiveParser<double>{}, "start_time_s");
                AddParam(&Rid::InterferenceSource::endTime_s, JsonPrimitiveParser<double>{}, "end_time_s");
                AddParam(&Rid::InterferenceSource::power_dbm, JsonPrimitiveParser<double>{}, "power_dbm");
                AddParamFlat(&Rid::InterferenceSource::variant, InterferenceVariantParser{});
            }
        };

        struct RidParser : public JsonObjectParser<Rid> {
            RidParser() {
                AddParam(&Rid::metadata, MetadataParser{}, "metadata");
                AddParam(&Rid::globalSettings, GlobalSettingsParser{}, "global_settings");
                auto p0 = AddParam(&Rid::radioSettings, JsonArrayParser<Rid::RadioSettings>{}, "radio_settings");
                auto p1 = AddParam(&Rid::interferenceSources, JsonArrayParser<Rid::InterferenceSource>{}, "interference_sources");

                p0->SetItemParser(RadioSettingsParser{});
                p0->SetSizeRange(1, std::nullopt);

                p1->SetItemParser(InterferenceSourceParser{});
                p1->SetSizeRange(1, std::nullopt);
            }
        };



        struct CommandMessageJsonParser : Json::JsonObjectParser<CommandMessage> {
            struct TestSetupParser : Json::JsonObjectParser<CommandMessage::TestSetup> {
                TestSetupParser() {
                    AddParam(&CommandMessage::TestSetup::scenario, Json::JsonPrimitiveParser<std::string>{}, "scenario");
                    AddParam(&CommandMessage::TestSetup::time, Json::JsonPrimitiveParser<std::string>{}, "time");
                    AddParam(&CommandMessage::TestSetup::testId, Json::JsonPrimitiveParser<std::string>{}, "test_id");
                }
            };

            struct TestCancelParser : Json::JsonObjectParser<CommandMessage::TestCancel> {
                TestCancelParser() {
                    AddParam(&CommandMessage::TestCancel::testId, Json::JsonPrimitiveParser<std::string>{}, "test_id");
                }
            };

            struct CommandMessageVariantParser : Json::JsonVariantParser<CommandMessage::Variant> {
                CommandMessageVariantParser() {
                    typeTagKey = "command";
                    AddAlt<CommandMessage::TestSetup>("TEST_SETUP", "extras", TestSetupParser{});
                    AddAlt<CommandMessage::TestCancel>("TEST_CANCEL", "extras", TestCancelParser{});
                }
            };

            CommandMessageJsonParser() {
                AddParamFlat(&CommandMessage::variant, CommandMessageVariantParser{});
                // addparam timestamp
                // addparam id
            }
        };

        struct StatusMessageJsonParser : Json::JsonObjectParser<StatusMessage> {
            struct TestStatusParser : Json::JsonEnumParser<StatusMessage::TestStatus> {
                TestStatusParser() {
                    AddEnumerator(StatusMessage::TestStatus::TEST_INIT, "TEST_INIT");
                    AddEnumerator(StatusMessage::TestStatus::TEST_PREPARE, "TEST_PREPARE");
                    AddEnumerator(StatusMessage::TestStatus::TEST_READY, "TEST_READY");
                    AddEnumerator(StatusMessage::TestStatus::TEST_RUNNING, "TEST_RUNNING");
                    AddEnumerator(StatusMessage::TestStatus::TEST_COMPLETED, "TEST_COMPLETED");
                    AddEnumerator(StatusMessage::TestStatus::TEST_FAILED, "TEST_FAILED");
                }
            };

            struct TestInfoParser : Json::JsonObjectParser<StatusMessage::TestInfo> {
                TestInfoParser() {
                    AddParam(&StatusMessage::TestInfo::testId, Json::JsonPrimitiveParser<std::string>{}, "test_id");
                    AddParam(&StatusMessage::TestInfo::status, TestStatusParser{}, "status");
                }
            };

            // no members yet, it serializes as an empty object and accepts whatever
            // arrives in its place
            struct DetailsParser : Json::JsonObjectParser<StatusMessage::Details> {
                DetailsParser() {
                }
            };

            StatusMessageJsonParser() {
                AddParam(&StatusMessage::timestamp, Json::JsonPrimitiveParser<std::string>{}, "timestamp");
                auto p0 = AddParam(&StatusMessage::tests, JsonArrayParser<StatusMessage::TestInfo>{}, "tests");
                p0->SetItemParser(TestInfoParser{});
                AddParam(&StatusMessage::details, DetailsParser{}, "details");
            }
        };

        struct ErrorMessageJsonParser : Json::JsonObjectParser<ErrorMessage> {
            ErrorMessageJsonParser() {
                AddParam(&ErrorMessage::message, Json::JsonPrimitiveParser<std::string>{}, "message");
            }
        };

        InterferenceSourceParser interferenceSourceParser;
        RidParser ridParser;
        CommandMessageJsonParser commandMessageParser;
        StatusMessageJsonParser statusMessageParser;
        ErrorMessageJsonParser errorMessageParser;

    }


    Rid ParseRid(const std::string &s) {
        Json::JsonHandle j = Json::ParseJson(s);
        Rid rid;
        ridParser.Parse(j, rid);
        return rid;
    }

    std::string SerializeRid(const Rid &t) {
        Json::JsonHandle j;
        ridParser.Serialize(t, j);
        return Json::DumpJson(j, true);
    }

    std::string SerializeRidInterferenceSource(const Rid &t, std::int32_t i) {
        Json::JsonHandle j;
        interferenceSourceParser.Serialize(t.interferenceSources[i], j);
        return Json::DumpJson(j, false);
    }



    CommandMessage ParseCommandMessage(const std::string &s) {
        Json::JsonHandle j = Json::ParseJson(s);
        CommandMessage msg;
        commandMessageParser.Parse(j, msg);
        return msg;
    }

    std::string SerializeCommandMessage(const CommandMessage &t) {
        Json::JsonHandle j;
        commandMessageParser.Serialize(t, j);
        return Json::DumpJson(j, false);
    }

    StatusMessage ParseStatusMessage(const std::string &s) {
        Json::JsonHandle j = Json::ParseJson(s);
        StatusMessage msg;
        statusMessageParser.Parse(j, msg);
        return msg;
    }

    std::string SerializeStatusMessage(const StatusMessage &t) {
        Json::JsonHandle j;
        statusMessageParser.Serialize(t, j);
        return Json::DumpJson(j, false);
    }

    ErrorMessage ParseErrorMessage(const std::string &s) {
        Json::JsonHandle j = Json::ParseJson(s);
        ErrorMessage msg;
        errorMessageParser.Parse(j, msg);
        return msg;
    }

    std::string SerializeErrorMessage(const ErrorMessage &t) {
        Json::JsonHandle j;
        errorMessageParser.Serialize(t, j);
        return Json::DumpJson(j, false);
    }

}



