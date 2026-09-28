#pragma once
#include <complex>
#include <vector>
#include <string>
#include "SSG/Modulators/Waveforms.hpp"
#include "SSG/Modulators/Analog.hpp"
#include "SCG/Modules/Packets.hpp"
#include "SCG/Scenario/SignalVariant.hpp"

namespace Sagax::SCG {

    struct SignalGenerator {
        virtual ~SignalGenerator() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void Generate(SCGIQPacket &packet, std::size_t nSamplesMax) = 0;
        virtual void SetSampleRate(double sampleRate_) = 0;
    };


    struct NoiseGenerator : public SignalGenerator {
        struct Impl;
        std::unique_ptr<Impl> impl;

        double sampleRate;
        double span;
        double psd;

        NoiseGenerator();
        ~NoiseGenerator();

        void Setup(double span_, double psd_, std::size_t bufSize, unsigned int seed);

        void Start() override;
        void Stop() override;
        void Generate(SCGIQPacket &packet, std::size_t nSamplesMax) override;
        void SetSampleRate(double sampleRate_) override;
    };

    struct ToneGenerator : public SignalGenerator {
        double sampleRate;

        void Start() override;
        void Stop() override;
        void Generate(SCGIQPacket &packet, std::size_t nSamplesMax) override;
        void SetSampleRate(double sampleRate_) override;
    };

    struct ChirpGenerator : public SignalGenerator {
        SSG::Modulators::SawGenerator<SCGSampleBase> gen;
        SSG::Modulators::FMModulator<SCGSampleBase> mod;

        double sampleRate;
        double span;
        double rate;
        std::vector<SCGSampleBase> waveBuf;

        void Start() override;
        void Stop() override;
        void Generate(SCGIQPacket &packet, std::size_t nSamplesMax) override;
        void SetSampleRate(double sampleRate_) override;
    };

    struct PulseGenerator : public SignalGenerator {
        SSG::Modulators::SquareGenerator<SCGSampleBase> gen;

        double sampleRate;
        double rate;
        double duty;

        std::vector<SCGSampleBase> waveBuf;

        void Start() override;
        void Stop() override;
        void Generate(SCGIQPacket &packet, std::size_t nSamplesMax) override;
        void SetSampleRate(double sampleRate_) override;
    };

    struct AudioFileGenerator : public SignalGenerator {
        struct Impl;
        std::unique_ptr<Impl> impl;

        std::string fileName;
        std::unique_ptr<SSG::Modulators::AnalogModulator<SCGSampleBase>> mod;
        double sampleRate;

        AudioFileGenerator();
        ~AudioFileGenerator();

        void Start() override;
        void Stop() override;
        void Generate(SCGIQPacket &packet, std::size_t nSamplesMax) override;
        void SetSampleRate(double sampleRate_) override;
    };

    struct IQFileGenerator : public SignalGenerator {
        struct Impl;
        std::unique_ptr<Impl> impl;

        std::string fileName;
        bool loop = true;
        double sampleRate;

        IQFileGenerator();
        ~IQFileGenerator();

        void Start() override;
        void Stop() override;
        void Generate(SCGIQPacket &packet, std::size_t nSamplesMax) override;
        void SetSampleRate(double sampleRate_) override;
    };

}

