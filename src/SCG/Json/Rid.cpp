#include "JsonParser.hpp"
#include "Rid.hpp"

using namespace Sagax::SCG::Json;

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
            struct TypeParser : public JsonEnumParser<Rid::RadioSettings::Type> {
                TypeParser() {
                    AddEnumerator(Rid::RadioSettings::Type::transmitter, "transmitter");
                    AddEnumerator(Rid::RadioSettings::Type::receiver, "receiver");
                }
            };

            RadioSettingsParser() {
                AddParam(&Rid::RadioSettings::type, TypeParser{}, "type");
                AddParam(&Rid::RadioSettings::radioId, JsonPrimitiveParser<std::string>{}, "radio_id");
                AddParam(&Rid::RadioSettings::center_hz, JsonPrimitiveParser<double>{}, "center_frequency_hz");
                AddParam(&Rid::RadioSettings::bandwidth_hz, JsonPrimitiveParser<double>{}, "bandwidth_hz");
                AddParam(&Rid::RadioSettings::gain_db, JsonPrimitiveParser<double>{}, "gain_db");
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
                            auto p0 = AddParam(&Rid::InterferenceSource::Spoofing::TrajectoryItem::position_m, JsonArrayParser<double>{}, "position_m");

                            p0->SetItemParser(JsonPrimitiveParser<double>{});
                            p0->SetSizeRange(3, 3);
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
                    subKey = "parameters";
                    AddAlt<Rid::InterferenceSource::Cw>("cw_jammer", CwParser{});
                    AddAlt<Rid::InterferenceSource::Noise>("noise_jammer", NoiseParser{});
                    AddAlt<Rid::InterferenceSource::Chirp>("chirp_jammer", ChirpParser{});
                    AddAlt<Rid::InterferenceSource::Spoofing>("gnss_spoofer", SpoofingParser{});
                }
            };

            InterferenceSourceParser() {
                AddParam(&Rid::InterferenceSource::radioId, JsonPrimitiveParser<std::string>{}, "radio_id");
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

        InterferenceSourceParser interferenceSourceParser;
        RidParser ridParser;
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

}



