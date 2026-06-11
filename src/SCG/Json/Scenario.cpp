#include <variant>
#include <stdexcept>
#include <string>
#include "Json/JsonParser.hpp"
#include "SSG/Modules/IQSink.hpp"
#include "SSG/Modules/PacketSource.hpp"
#include "SSG/Modules/IQFileSink.hpp"
#include "SSG/Modules/IQFileSource.hpp"
#include "Scenario.hpp"

using namespace Sagax::Json;

namespace Sagax::SCG {

    namespace {

        struct OutputParser : public JsonObjectParser<Scenario::Output> {
            struct FileIQParser : public JsonObjectParser<OutputVariant::FileIQ> {
                FileIQParser() {
                    AddParam(&OutputVariant::FileIQ::fileName, JsonPrimitiveParser<std::string>{}, "fileName");
                    AddParam(&OutputVariant::FileIQ::nChannels, JsonPrimitiveParser<std::size_t>{}, "nChannels");
                    AddParam(&OutputVariant::FileIQ::sampleRate, JsonPrimitiveParser<double>{}, "samplerate");
                    AddParam(&OutputVariant::FileIQ::center, JsonPrimitiveParser<double>{}, "center");
                }
            };
            struct RadioUsrpB200Parser : public JsonObjectParser<OutputVariant::RadioUsrpB200> {
                RadioUsrpB200Parser() {
                    AddParam(&OutputVariant::RadioUsrpB200::serial, JsonPrimitiveParser<std::string>{}, "serial");
                    AddParam(&OutputVariant::RadioUsrpB200::subdevSpec, JsonPrimitiveParser<std::string>{}, "subdevSpec");
                    AddParam(&OutputVariant::RadioUsrpB200::sampleRate, JsonPrimitiveParser<double>{}, "samplerate");
                    AddParam(&OutputVariant::RadioUsrpB200::center, JsonPrimitiveParser<double>{}, "frequency");
                    AddParam(&OutputVariant::RadioUsrpB200::gain, JsonPrimitiveParser<double>{}, "gain");
                }
            };
            struct RadioSidekiqParser : public JsonObjectParser<OutputVariant::RadioSidekiq> {
                RadioSidekiqParser() {
                    AddParam(&OutputVariant::RadioSidekiq::serial, JsonPrimitiveParser<std::string>{}, "serial");
                    AddParam(&OutputVariant::RadioSidekiq::sampleRate, JsonPrimitiveParser<double>{}, "samplerate");
                    AddParam(&OutputVariant::RadioSidekiq::center, JsonPrimitiveParser<double>{}, "frequency");
                    AddParam(&OutputVariant::RadioSidekiq::gain, JsonPrimitiveParser<double>{}, "gain");
                }
            };
            struct OutputVariantParser : public JsonObjectParser<OutputVariant> {
                struct VariantParser : public JsonVariantParser<OutputVariant::T> {
                    VariantParser() {
                        typeTagKey = "type";
                        AddAlt<OutputVariant::FileIQ>("File", std::nullopt, FileIQParser{});
                        AddAlt<OutputVariant::RadioUsrpB200>("UsrpB200", std::nullopt, RadioUsrpB200Parser{});
                        AddAlt<OutputVariant::RadioSidekiq>("Sidekiq", std::nullopt, RadioSidekiqParser{});
                    }
                };
                OutputVariantParser() {
                    AddParamFlat(&OutputVariant::variant, VariantParser{});
                }
            };

            OutputParser() {
                AddParamFlat(&Scenario::Output::variant, OutputVariantParser{});
                AddParam(&Scenario::Output::name, JsonPrimitiveParser<std::string>{}, "name");
            }
        };

        struct SignalParser : public JsonObjectParser<Scenario::Signal> {
            struct NoiseParser : public JsonObjectParser<SignalVariant::Noise> {
                NoiseParser() {
                    AddParam(&SignalVariant::Noise::span, JsonPrimitiveParser<double>{}, "span");
                }
            };
            struct ToneParser : public JsonObjectParser<SignalVariant::Tone> {
                ToneParser() {
                    // no parameters
                }
            };
            struct ChirpParser : public JsonObjectParser<SignalVariant::Chirp> {
                ChirpParser() {
                    AddParam(&SignalVariant::Chirp::span, JsonPrimitiveParser<double>{}, "span");
                    AddParam(&SignalVariant::Chirp::rate, JsonPrimitiveParser<double>{}, "rate");
                }
            };
            struct PulseParser : public JsonObjectParser<SignalVariant::Pulse> {
                PulseParser() {
                    AddParam(&SignalVariant::Pulse::duty, JsonPrimitiveParser<double>{}, "duty");
                    AddParam(&SignalVariant::Pulse::rate, JsonPrimitiveParser<double>{}, "rate");
                }
            };
            struct AudioFileParser : public JsonObjectParser<SignalVariant::AudioFile> {
                struct ModulationParser : public JsonEnumParser<SignalVariant::AudioFile::Modulation> {
                    ModulationParser() {
                        AddEnumerator(SignalVariant::AudioFile::Modulation::IQ, "IQ");
                        AddEnumerator(SignalVariant::AudioFile::Modulation::FM, "FM");
                        AddEnumerator(SignalVariant::AudioFile::Modulation::AM, "AM");
                        AddEnumerator(SignalVariant::AudioFile::Modulation::PM, "PM");
                    }
                };
                AudioFileParser() {
                    AddParam(&SignalVariant::AudioFile::fileName, JsonPrimitiveParser<std::string>{}, "file");
                    AddParam(&SignalVariant::AudioFile::mod, ModulationParser{}, "modulation");
                }
            };
            struct IQFileParser : public JsonObjectParser<SignalVariant::IQFile> {
                IQFileParser() {
                    AddParam(&SignalVariant::IQFile::fileName, JsonPrimitiveParser<std::string>{}, "file");
                }
            };

            struct SignalVariantParser : public JsonObjectParser<SignalVariant> {
                struct VariantParser : public JsonVariantParser<SignalVariant::T> {
                    VariantParser() {
                        typeTagKey = "type";
                        AddAlt<SignalVariant::Tone>("Tone", std::nullopt, ToneParser{});
                        AddAlt<SignalVariant::Chirp>("Chirp", std::nullopt, ChirpParser{});
                        AddAlt<SignalVariant::Pulse>("Pulse", std::nullopt, PulseParser{});
                        AddAlt<SignalVariant::Noise>("Noise", std::nullopt, NoiseParser{});
                        AddAlt<SignalVariant::IQFile>("IQFile", std::nullopt, IQFileParser{});
                        AddAlt<SignalVariant::AudioFile>("AudioFile", std::nullopt, AudioFileParser{});
                    }
                };
                SignalVariantParser() {
                    AddParamFlat(&SignalVariant::variant, VariantParser{});
                }
            };

            SignalParser() {
                AddParam(&Scenario::Signal::name, JsonPrimitiveParser<std::string>{}, "name");
                AddParamFlat(&Scenario::Signal::variant, SignalVariantParser{});
            }
        };

        struct EventParser : public JsonObjectParser<Scenario::Event> {
            EventParser() {
                AddParam(&Scenario::Event::signalName, JsonPrimitiveParser<std::string>{}, "signal");
                AddParam(&Scenario::Event::outputName, JsonPrimitiveParser<std::string>{}, "output");
                AddParam(&Scenario::Event::iChannel, JsonPrimitiveParser<std::size_t>{}, "channel");
                AddParam(&Scenario::Event::amplitude, JsonPrimitiveParser<double>{}, "amplitude");
                AddParam(&Scenario::Event::frequency, JsonPrimitiveParser<double>{}, "frequency");
                AddParam(&Scenario::Event::start, JsonPrimitiveParser<double>{}, "start");
                AddParam(&Scenario::Event::end, JsonPrimitiveParser<double>{}, "end");

                // should add back this logic
                // Common::Assert(outputP.data.find('.') != std::string::npos, "output specifier must have a \'.\' as separator");
                // std::string oName = outputP.data.substr(0, outputP.data.find('.'));
                // std::string cName = outputP.data.substr(outputP.data.find('.') + 1);
                // int iChannel = std::stoi(cName);
                // Common::Assert(iChannel >= 0, std::format("negative channel index", iChannel));

                // e.outputName = oName;
                // e.iChannel = std::size_t(iChannel);
            }
        };



        struct ScenarioParser : public JsonObjectParser<Scenario> {
            ScenarioParser() {
                AddParamDef(&Scenario::name, JsonPrimitiveParser<std::string>{}, std::string(""), "name");
                AddParam(&Scenario::duration, JsonPrimitiveParser<double>{}, "duration");
                auto pO = AddParam(&Scenario::outputs, JsonArrayParser<Scenario::Output>{}, "outputs");
                auto pS = AddParam(&Scenario::signals, JsonArrayParser<Scenario::Signal>{}, "signals");
                auto pE = AddParam(&Scenario::events, JsonArrayParser<Scenario::Event>{}, "events");

                pO->SetItemParser(OutputParser{});
                pS->SetItemParser(SignalParser{});
                pE->SetItemParser(EventParser{});
            }
        };

        ScenarioParser scenarioParser;
    }


    Scenario ParseScenario(
        const std::string &str,
        std::optional<std::string> scenarioNameFallback
    ) {
        Scenario scen;

        JsonHandle j = ParseJson(str);
        scenarioParser.Parse(j, scen);

        if(scen.name == "" && scenarioNameFallback.has_value()) {
            scen.name = scenarioNameFallback.value();
        }

        return scen;
    };


    std::string SerializeScenario(
        const Scenario &scen
    ) {
        std::string str;

        JsonHandle j;
        scenarioParser.Serialize(scen, j);
        str = DumpJson(j, true);

        return str;
    }


};