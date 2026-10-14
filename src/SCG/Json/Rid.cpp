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
                            AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::longitude, JsonPrimitiveParser<double>{}, "longitude_deg");
                            AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::latitude, JsonPrimitiveParser<double>{}, "latitude_deg");
                            AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::altitude, JsonPrimitiveParser<double>{}, "altitude_m");
                        }
                    };

                    SpoofingParser() {
                        AddParam(&Rid::InterferenceSource::Spoofing::constellation, ConstellationParser{}, "constellation");
                        auto p0 = AddParam(&Rid::InterferenceSource::Spoofing::prnList, JsonArrayParser<std::int32_t>{}, "prn_list");
                        auto p1 = AddParam(&Rid::InterferenceSource::Spoofing::trajectory, JsonArrayParser<Rid::InterferenceSource::Spoofing::TrajectoryItem>{}, "trajectory");

                        p0->SetItemParser(JsonPrimitiveParser<std::int32_t>{});
                        p0->SetSizeRange(0, 210);

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
                    AddParam(&CommandMessage::TestSetup::scenarioUrl, Json::JsonPrimitiveParser<std::string>{}, "scenario_url");
                    AddParam(&CommandMessage::TestSetup::startTime, Json::JsonPrimitiveParser<std::string>{}, "start_time");
                    AddParam(&CommandMessage::TestSetup::testId, Json::JsonPrimitiveParser<std::string>{}, "test_id");
                }
            };

            struct TestCancelParser : Json::JsonObjectParser<CommandMessage::TestCancel> {
                TestCancelParser() {
                    AddParam(&CommandMessage::TestCancel::testId, Json::JsonPrimitiveParser<std::string>{}, "test_id");
                }
            };

            // the commands that carry nothing all read the same way
            template<typename T>
            struct EmptyParser : Json::JsonObjectParser<T> {
                EmptyParser() {
                }
            };

            struct CommandMessageVariantParser : Json::JsonVariantParser<CommandMessage::Variant> {
                CommandMessageVariantParser() {
                    typeTagKey = "command";
                    AddAlt<CommandMessage::TestSetup>("TEST_SETUP", "extras", TestSetupParser{});
                    AddAlt<CommandMessage::TestCancel>("TEST_CANCEL", "extras", TestCancelParser{});
                    AddAlt<CommandMessage::IntStop>("INT_STOP", "extras", EmptyParser<CommandMessage::IntStop>{});
                    AddAlt<CommandMessage::IntStart>("INT_START", "extras", EmptyParser<CommandMessage::IntStart>{});
                    AddAlt<CommandMessage::TestStart>("TEST_START", "extras", EmptyParser<CommandMessage::TestStart>{});
                    AddAlt<CommandMessage::TestUpdate>("TEST_UPDATE", "extras", EmptyParser<CommandMessage::TestUpdate>{});
                    AddAlt<CommandMessage::TestEnd>("TEST_END", "extras", EmptyParser<CommandMessage::TestEnd>{});
                }
            };

            CommandMessageJsonParser() {
                AddParamFlat(&CommandMessage::variant, CommandMessageVariantParser{});
                // addparam timestamp
                // addparam id
            }
        };

        struct TestStatusMessageJsonParser : Json::JsonObjectParser<TestStatusMessage> {
            struct TestStatusParser : Json::JsonEnumParser<TestStatusMessage::TestStatus> {
                TestStatusParser() {
                    AddEnumerator(TestStatusMessage::TestStatus::TEST_INIT, "TEST_INIT");
                    AddEnumerator(TestStatusMessage::TestStatus::TEST_PREPARE, "TEST_PREPARE");
                    AddEnumerator(TestStatusMessage::TestStatus::TEST_READY, "TEST_READY");
                    AddEnumerator(TestStatusMessage::TestStatus::TEST_RUNNING, "TEST_RUNNING");
                    AddEnumerator(TestStatusMessage::TestStatus::TEST_COMPLETED, "TEST_COMPLETED");
                    AddEnumerator(TestStatusMessage::TestStatus::TEST_FAILED, "TEST_FAILED");
                }
            };

            TestStatusMessageJsonParser() {
                AddParam(&TestStatusMessage::timestamp, Json::JsonPrimitiveParser<std::string>{}, "timestamp");
                AddParam(&TestStatusMessage::testId, Json::JsonPrimitiveParser<std::string>{}, "test_id");
                AddParam(&TestStatusMessage::testStatus, TestStatusParser{}, "test_status");
            }
        };

        struct SignalStatusMessageJsonParser : Json::JsonObjectParser<SignalStatusMessage> {
            struct SignalStatusParser : Json::JsonEnumParser<SignalStatusMessage::SignalStatus> {
                SignalStatusParser() {
                    AddEnumerator(SignalStatusMessage::SignalStatus::none, "none");
                    AddEnumerator(SignalStatusMessage::SignalStatus::jamming, "jamming");
                    AddEnumerator(SignalStatusMessage::SignalStatus::spoofing, "spoofing");
                }
            };

            // serializes as an empty object, and any content it arrives with is ignored
            struct DescriptionParser : Json::JsonObjectParser<SignalStatusMessage::Description> {};

            SignalStatusMessageJsonParser() {
                AddParam(&SignalStatusMessage::timestamp, Json::JsonPrimitiveParser<std::string>{}, "timestamp");
                AddParam(&SignalStatusMessage::type, SignalStatusParser{}, "type");
                AddParam(&SignalStatusMessage::description, DescriptionParser{}, "description");
            }
        };

        struct ErrorMessageJsonParser : Json::JsonObjectParser<ErrorMessage> {
            ErrorMessageJsonParser() {
                AddParam(&ErrorMessage::message, Json::JsonPrimitiveParser<std::string>{}, "message");
            }
        };

        struct SavedTestJsonParser : Json::JsonObjectParser<SavedTest> {
            SavedTestJsonParser() {
                AddParam(&SavedTest::command, CommandMessageJsonParser::TestSetupParser{}, "command");
                AddParam(&SavedTest::status, TestStatusMessageJsonParser::TestStatusParser{}, "status");
            }
        };

        InterferenceSourceParser interferenceSourceParser;
        RidParser ridParser;
        CommandMessageJsonParser commandMessageParser;
        TestStatusMessageJsonParser testStatusMessageParser;
        SignalStatusMessageJsonParser signalStatusMessageParser;
        ErrorMessageJsonParser errorMessageParser;
        SavedTestJsonParser savedTestParser;

    }


    bool ParseRid(const std::string &s, Rid &out, Json::JsonIssues &e) {
        return Json::ParseDocument<Rid>(ridParser, s, out, e);
    }

    std::optional<std::string> SerializeRid(const Rid &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<Rid>(ridParser, t, true, e);
    }

    std::optional<std::string> SerializeRidInterferenceSource(
        const Rid &t,
        std::int32_t i,
        Json::JsonIssues &e
    ) {
        if(i < 0 || std::size_t(i) >= t.interferenceSources.size()) {
            e.AddError(std::format("no interference source at index {}", i));
            return std::nullopt;
        }
        return Json::SerializeDocument<Rid::InterferenceSource>(
            interferenceSourceParser, t.interferenceSources[i], false, e
        );
    }



    bool ParseCommandMessage(const std::string &s, CommandMessage &out, Json::JsonIssues &e) {
        return Json::ParseDocument<CommandMessage>(commandMessageParser, s, out, e);
    }

    std::optional<std::string> SerializeCommandMessage(const CommandMessage &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<CommandMessage>(commandMessageParser, t, false, e);
    }

    bool ParseTestStatusMessage(const std::string &s, TestStatusMessage &out, Json::JsonIssues &e) {
        return Json::ParseDocument<TestStatusMessage>(testStatusMessageParser, s, out, e);
    }

    std::optional<std::string> SerializeTestStatusMessage(const TestStatusMessage &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<TestStatusMessage>(testStatusMessageParser, t, false, e);
    }

    bool ParseSignalStatusMessage(const std::string &s, SignalStatusMessage &out, Json::JsonIssues &e) {
        return Json::ParseDocument<SignalStatusMessage>(signalStatusMessageParser, s, out, e);
    }

    std::optional<std::string> SerializeSignalStatusMessage(const SignalStatusMessage &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<SignalStatusMessage>(signalStatusMessageParser, t, false, e);
    }

    bool ParseErrorMessage(const std::string &s, ErrorMessage &out, Json::JsonIssues &e) {
        return Json::ParseDocument<ErrorMessage>(errorMessageParser, s, out, e);
    }

    std::optional<std::string> SerializeErrorMessage(const ErrorMessage &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<ErrorMessage>(errorMessageParser, t, false, e);
    }

    bool ParseSavedTest(const std::string &s, SavedTest &out, Json::JsonIssues &e) {
        return Json::ParseDocument<SavedTest>(savedTestParser, s, out, e);
    }

    std::optional<std::string> SerializeSavedTest(const SavedTest &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<SavedTest>(savedTestParser, t, true, e);
    }

}



