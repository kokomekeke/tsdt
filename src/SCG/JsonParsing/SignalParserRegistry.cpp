#include "Common/Assert.hpp"
#include "SignalParserRegistry.hpp"

namespace Sagax::SCG {
    namespace {

        struct ToneParser : SignalParser {

            std::string GetDescription() const override {
                return "A sine wave. No special parameters";
            }

            SignalVariant GetVariant() override {
                SignalVariant::Tone v;
                return { v };
            }
        };

        struct ChirpParser : SignalParser {
            Parser::Json::BasicParameter<double> spanP = AddParameter(&spanP, "double", "span");
            Parser::Json::BasicParameter<double> rateP = AddParameter(&rateP, "double", "rate");

            std::string GetDescription() const override {
                return "Sweeping chirp signal";
            }

            SignalVariant GetVariant() override {
                Common::Assert(!spanP.defaulted, "span must be specified");
                Common::Assert(!rateP.defaulted, "rate must be specified");

                SignalVariant::Chirp v;
                v.rate = rateP.data;
                v.span = spanP.data;
                return { v };
            };
        };


        struct NoiseParser : SignalParser {
            Parser::Json::BasicParameter<double> spanP = AddParameter(&spanP, "sampleRate", "span");

            std::string GetDescription() const override {
                return "Generates noise with constant psd";
            }

            SignalVariant GetVariant() override {
                Common::Assert(!spanP.defaulted, "span must be specified");

                SignalVariant::Noise v;
                v.span = spanP.data;
                return { v };
            }
        };

        struct FileIQParser : SignalParser {
            Parser::Json::BasicParameter<std::string> fileP = AddParameter(&fileP, "string", "file");

            std::string GetDescription() const override {
                return "Reads contents of iq file.\nAllowed formats: .wav, .sigmf-data, .iq";
            }

            SignalVariant GetVariant() override {
                Common::Assert(!fileP.defaulted, "file must be specified");

                SignalVariant::IQFile v;
                v.fileName = fileP.data;
                return { v };
            }
        };

        struct AudioFileParser : SignalParser {
            Parser::Json::BasicParameter<std::string> fileP = AddParameter(&fileP, "string", "file");
            Parser::Json::BasicParameter<std::string> modP = AddParameter(&modP, "string", "modulation");

            std::string GetDescription() const override {
                return
                    "Reads contents of an audio file and modulates it onto the iq stream.\n"
                    "Allowed formats: .wav\n"
                    "Allowed modulations: FM, AM, PM, IQ";
            }

            SignalVariant GetVariant() override {
                Common::Assert(!fileP.defaulted, "file must be specified");
                Common::Assert(!modP.defaulted, "modulation must be specified");
                Common::Assert(
                    (modP.data == "FM") ||
                    (modP.data == "AM") ||
                    (modP.data == "PM") ||
                    (modP.data == "IQ"),
                    "invalid modulation type"
                );

                SignalVariant::AudioFile v;
                v.fileName = fileP.data;
                v.mod = (
                    modP.data == "FM" ? SignalVariant::AudioFile::Modulation::FM :
                    modP.data == "AM" ? SignalVariant::AudioFile::Modulation::AM :
                    modP.data == "PM" ? SignalVariant::AudioFile::Modulation::PM : SignalVariant::AudioFile::Modulation::IQ
                );
                return { v };
            }
        };

    }


    Sagax::Parser::Registry<SignalParser> GetSignalParserRegistry() {
        Sagax::Parser::Registry<SignalParser> registry;
        registry.Register<ToneParser>("Tone");
        registry.Register<ChirpParser>("Chirp");
        registry.Register<NoiseParser>("Noise");
        registry.Register<FileIQParser>("IQFile");
        registry.Register<AudioFileParser>("AudioFile");
        return registry;
    }
}


