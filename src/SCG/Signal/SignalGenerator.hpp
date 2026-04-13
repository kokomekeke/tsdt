#pragma once
#include <complex>
#include <vector>
#include <string>
#include <random>
#include "SSG/IO/SampleFile.hpp"
#include "SSG/IO/IQFileIn.hpp"
#include "SSG/IO/RealFileIn.hpp"
#include "SSG/Modulators/Waveforms.hpp"
#include "SSG/Modulators/Analog.hpp"
#include "SSG/Packets/Packets.hpp"
#include "SCG/Scenario/SignalVariant.hpp"

namespace Sagax::SCG {

    struct SignalGenerator {
        virtual ~SignalGenerator() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) = 0;
        virtual double GetSampleRate() = 0;
    };


    struct NoiseGenerator : public SignalGenerator {

        double sampleRate;
        double psd;
        std::mt19937 gen;
        std::vector<double> buf;

        void Setup(double sampleRate_, double psd_, std::size_t bufSize, unsigned int seed);

        void Start() override;
        void Stop() override;
        void Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) override;
        double GetSampleRate() override;

    };

    struct ToneGenerator : public SignalGenerator {
        double sampleRate;

        void Start() override;
        void Stop() override;
        void Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) override;
        double GetSampleRate() override;
    };

    struct ChirpGenerator : public SignalGenerator {
        SSG::Modulators::SawGenerator gen;
        SSG::Modulators::FMModulator mod;

        double sampleRate;
        double span;
        double rate;
        std::vector<double> waveBuf;

        void Start() override;
        void Stop() override;
        void Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) override;
        double GetSampleRate() override;
    };

    struct PulseGenerator : public SignalGenerator {
        SSG::Modulators::SquareGenerator gen;

        double sampleRate;
        double duty;

        std::vector<double> waveBuf;

        void Start() override;
        void Stop() override;
        void Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) override;
        double GetSampleRate() override;
    };

    struct AudioFileGenerator : public SignalGenerator {
        std::string fileName;
        std::unique_ptr<SSG::Modulators::AnalogModulator> mod;

        SSG::IO::RealFileIn file;
        SSG::IO::SampleFileMeta meta;
        SSG::Packets::RealPacket packetBuf;

        void Start() override;
        void Stop() override;
        void Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) override;
        double GetSampleRate() override;
    };

    struct IQFileGenerator : public SignalGenerator {

        std::string fileName;
        SSG::IO::IQFileIn file;

        SSG::IO::SampleFileMeta meta;

        void Start() override;
        void Stop() override;
        void Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) override;
        double GetSampleRate() override;
    };

}

