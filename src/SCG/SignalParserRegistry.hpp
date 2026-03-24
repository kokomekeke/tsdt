#pragma once
#include <numbers>
#include "Parser/Parser.hpp"
#include "Parsers.hpp"
#include "Pipeline/PipelineBuilder.hpp"
#include "SSG/Modules/SineGenerator.hpp"
#include "SSG/Modules/WaveGenerator.hpp"
#include "SSG/Modules/PacketSource.hpp"
#include "SSG/Modules/IQFileSource.hpp"
#include "SSG/Modules/RealFileSource.hpp"
#include "SSG/Modules/Hilbert.hpp"
#include "SSG/Modules/AnalogModulator.hpp"
#include "SSG/Modules/NoiseGenerator.hpp"

namespace Sagax::SCG {

    struct ToneParser : SignalParser {
        std::string GetDescription() const override {
            return "A sine wave. No special parameters";
        }

        Sagax::Pipeline::PipelineBuilder Setup() override {
            using namespace SSG::Modules;
            Sagax::Pipeline::PipelineBuilder b;
            auto iPs = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<IQPacketSource>();
                    mdl->sampleRate = 100000.0;
                    mdl->nChannels = 1;
                    mdl->nSamples = (1<<12);
                    mdl->infinite = true;
                    return mdl;
                },
                0,
                1
            );
            auto iGen = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<SineGenerator>();
                    mdl->ampl = 1.0;
                    mdl->freq = 0.0;
                    return mdl;
                },
                1,
                1
            );
            b.AddEdge({ iPs, 0 }, { iGen, 0 });
            return b;
        }
    };

    struct ChirpParser : SignalParser {
        Parser::Json::BasicParameter<double> spanP = AddParameter(&spanP, "double", "span");
        Parser::Json::BasicParameter<double> rateP = AddParameter(&rateP, "double", "rate");

        std::string GetDescription() const override {
            return "Sweeping chirp signal";
        }

        Sagax::Pipeline::PipelineBuilder Setup() override {
            using namespace SSG::Modules;
            Sagax::Pipeline::PipelineBuilder b;
            Common::Assert(!spanP.defaulted, "span must be specified");
            Common::Assert(!rateP.defaulted, "rate must be specified");
            auto iPsIQ = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<IQPacketSource>();
                    mdl->nChannels = 1;
                    mdl->nSamples = (1<<12);
                    mdl->infinite = true;
                    mdl->sampleRate = spanP.data;
                    return mdl;
                },
                0,
                1
            );
            auto iPsR = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<RealPacketSource>();
                    mdl->nChannels = 1;
                    mdl->nSamples = (1<<12);
                    mdl->infinite = true;
                    mdl->sampleRate = spanP.data;
                    return mdl;
                },
                0,
                1
            );
            auto iGen = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<WaveGenerator>();
                    auto gen = std::make_unique<SSG::Modulators::SawGenerator>();
                    gen->ampl = spanP.data / 2.0;
                    gen->freq = rateP.data;
                    gen->offset = 0.0;
                    mdl->gen = std::move(gen);
                    return mdl;
                },
                1,
                1
            );
            auto iMod = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<AnalogModulator>();
                    auto mod = std::make_unique<SSG::Modulators::FMModulator>();
                    mod->freq = 0.0;
                    mod->ampl = 1.0;
                    mod->deviation = 1.0;
                    mdl->mod = std::move(mod);
                    return mdl;
                },
                2,
                1
            );
            b.AddEdge({ iPsIQ, 0 }, { iMod, 0 });
            b.AddEdge({ iPsR, 0 }, { iGen, 0 });
            b.AddEdge({ iGen, 0 }, { iMod, 1 });
            return b;
        }
    };

    struct NoiseParser : SignalParser {
        Parser::Json::BasicParameter<double> spanP = AddParameter(&spanP, "sampleRate", "span");
        std::string GetDescription() const override {
            return "Generates noise with constant psd";
        }
        Sagax::Pipeline::PipelineBuilder Setup() override {
            using namespace SSG::Modules;
            Common::Assert(!spanP.defaulted, "span must be specified");
            Sagax::Pipeline::PipelineBuilder b;
            auto iPs = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<IQPacketSource>();
                    mdl->nChannels = 1;
                    mdl->nSamples = (1<<12);
                    mdl->infinite = true;
                    mdl->sampleRate = spanP.data;
                    return mdl;
                },
                0,
                1
            );
            auto iGen = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<NoiseGenerator>();
                    mdl->Setup(1e-6, 1<<16, 0);
                    return mdl;
                },
                1,
                1
            );
            b.AddEdge({ iPs, 0 }, { iGen, 0 });
            return b;
        }
    };

    struct FileIQParser : SignalParser {
        Parser::Json::BasicParameter<std::string> fileP = AddParameter(&fileP, "string", "file");

        std::string GetDescription() const override {
            return "Reads contents of iq file.\nAllowed formats: .wav, .sigmf-data, .iq";
        }

        Sagax::Pipeline::PipelineBuilder Setup() override {
            using namespace SSG::IO;
            using namespace SSG::Modules;
            Common::Assert(!fileP.defaulted, "file must be specified");
            Sagax::Pipeline::PipelineBuilder b;
            b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<IQFileSource>();
                    mdl->mode = FilePlaybackMode::Infinite{};
                    mdl->filename = fileP.data;
                    mdl->nSamples = (1<<12);
                    return mdl;
                },
                0,
                1
            );
            return b;
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

        Sagax::Pipeline::PipelineBuilder Setup() override {
            using namespace SSG::IO;
            using namespace SSG::Modules;
            Common::Assert(!fileP.defaulted, "file must be specified");
            Common::Assert(!modP.defaulted, "modulation must be specified");
            Common::Assert(
                (modP.data == "FM") ||
                (modP.data == "AM") ||
                (modP.data == "PM") ||
                (modP.data == "IQ"),
                "invalid modulation type"
            );
            Sagax::Pipeline::PipelineBuilder b;
            auto iSrc = b.AddVertex(
                [=, *this](){
                    auto mdl = std::make_unique<RealFileSource>();
                    mdl->mode = FilePlaybackMode::Infinite{};
                    mdl->filename = fileP.data;
                    mdl->nSamples = (1<<12);
                    mdl->mono = true;
                    return mdl;
                },
                0,
                1
            );
            if(modP.data == "IQ") {
                auto iTr = b.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<Hilbert>();
                        return mdl;
                    },
                    1,
                    1
                );
                b.AddEdge({ iSrc, 0 }, { iTr, 0 });
            }
            else {
                auto iPs = b.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<IQPacketSource>();
                        mdl->nChannels = 1;
                        mdl->nSamples = (1<<12);
                        mdl->infinite = true;
                        mdl->sampleRate = -1.0; // dummy value replaced by modulator
                        return mdl;
                    },
                    0,
                    1
                );
                auto iMod = b.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<AnalogModulator>();
                        mdl->ignoreIQSampleRate = true;
                        if(modP.data == "FM") {
                            auto mod = std::make_unique<SSG::Modulators::FMModulator>();
                            mod->freq = 0.0;
                            mod->ampl = 1.0;
                            mod->deviation = 50000.0;
                            mdl->mod = std::move(mod);
                        }
                        else if(modP.data == "AM") {
                            auto mod = std::make_unique<SSG::Modulators::AMModulator>();
                            mod->freq = 0.0;
                            mod->ampl = 0.5;
                            mod->deviation = 0.3;
                            mdl->mod = std::move(mod);
                        }
                        else if(modP.data == "PM") {
                            auto mod = std::make_unique<SSG::Modulators::PMModulator>();
                            mod->freq = 0.0;
                            mod->ampl = 1.0;
                            mod->deviation = std::numbers::pi;
                            mdl->mod = std::move(mod);
                        }
                        else {
                            Common::Assert(false, "wrong modulator type");
                        }
                        return mdl;
                    },
                    2,
                    1
                );
                b.AddEdge({ iPs, 0 }, { iMod, 0 });
                b.AddEdge({ iSrc, 0 }, { iMod, 1 });
            }
            return b;
        }
    };


    Sagax::Parser::Registry<SignalParser> GetSignalRegistry() {
        Sagax::Parser::Registry<SignalParser> registry;
        registry.Register<ToneParser>("Tone");
        registry.Register<ChirpParser>("Chirp");
        registry.Register<NoiseParser>("Noise");
        registry.Register<FileIQParser>("IQFile");
        registry.Register<AudioFileParser>("AudioFile");
        return registry;
    }

}
