#include "Common/Assert.hpp"
#include "OutputParserRegistry.hpp"

namespace Sagax::SCG {
    namespace {
        struct UsrpB200OutputParser : public OutputParser {
            Parser::Json::BasicParameter<std::string> serialP = AddParameter(&serialP, "string", "serial", "Serial number of the USRPB200 device");
            Parser::Json::BasicParameter<std::string> subdevP = AddParameter(&subdevP, "string", "subdevSpec");
            Parser::Json::BasicParameter<double> sampleRateP = AddParameter(&sampleRateP, "double", "samplerate");
            Parser::Json::BasicParameter<double> freqP = AddParameter(&freqP, "double", "frequency");
            Parser::Json::BasicParameter<double> gainP = AddParameter(&gainP, "double", "gain");

            OutputVariant GetVariant() override {
                Common::Assert(!serialP.defaulted, "serial must be specified");
                Common::Assert(!subdevP.defaulted, "subdevSpec must be specified");
                Common::Assert(!sampleRateP.defaulted, "sampleRate must be specified");
                Common::Assert(!freqP.defaulted, "frequency must be specified");
                Common::Assert(!gainP.defaulted, "gain must be specified");

                OutputVariant::RadioUsrpB200 v;
                v.serial = serialP.data;
                v.subdevSpec = subdevP.data;
                v.sampleRate = sampleRateP.data;
                v.center = freqP.data;
                v.gain = gainP.data;
                return { v };
            }
        };

        struct FileOutputParser : public OutputParser {
            Parser::Json::BasicParameter<std::string> fileP = AddParameter(&fileP, "string", "file", "file name, including the extension");
            Parser::Json::BasicParameter<double> sampleRateP = AddParameter(&sampleRateP, "double", "samplerate");
            Parser::Json::BasicParameter<double> freqP = AddParameter(&freqP, "double", "frequency");
            Parser::Json::BasicParameter<std::int32_t> nChannelsP = AddParameter(&nChannelsP, "int", "nChannels");

            OutputVariant GetVariant() override {
                Common::Assert(!fileP.defaulted, "file has to be specified");
                Common::Assert(!sampleRateP.defaulted, "samplerate has to be specified");
                Common::Assert(nChannelsP.defaulted || nChannelsP.data > 0 && nChannelsP.data < 10, "invalid or missing value for nChannels");

                OutputVariant::FileIQ v;
                v.fileName = fileP.data;
                v.center = freqP.ValueOr(0.0);
                v.sampleRate = sampleRateP.data;
                v.nChannels = std::size_t(nChannelsP.ValueOr(1));
                return { v };
            }
        };
    }


    Sagax::Parser::Registry<OutputParser> GetOutputParserRegistry() {
        Sagax::Parser::Registry<OutputParser> registry;
        registry.Register<UsrpB200OutputParser>("UsrpB200");
        registry.Register<FileOutputParser>("File");
        return registry;
    }
}

