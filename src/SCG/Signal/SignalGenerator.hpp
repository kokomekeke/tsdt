#pragma once
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "SSG/Modulators/Waveforms.hpp"
#include "SSG/Modulators/Analog.hpp"
#include "SCG/Scenario/Sample.hpp"
#include "SCG/Scenario/SignalVariant.hpp"
#include "Gnss.hpp"

namespace Sagax::SCG {

    // a generator plays one event. the order of the calls is SetSampleRate, Init, Start, then Read and
    // Generate for each block of the event, Stop, Dispose.
    // io generators take their samples from a file in Read and finish them in Generate. cpu generators
    // make their samples in Generate. Read and Generate of the same generator can run at the same time
    // for different blocks, so they share no state
    struct SignalGenerator {
        enum class WorkType { cpu, io };

        virtual ~SignalGenerator() = default;

        virtual WorkType GetWorkType() = 0;
        // the rate of the output the event plays on
        virtual void SetSampleRate(double sampleRate_) = 0;
        // the rate of the samples Read and Generate give, valid after Init
        virtual double GetSampleRate() = 0;

        virtual void Init() = 0;
        virtual void Dispose() = 0;
        virtual void Start() = 0;
        virtual void Stop() = 0;

        // io only, fills buf with the next samples
        virtual void Read(std::span<SCGSample> buf) = 0;
        // io: turns the samples Read put into buf into the signal. cpu: fills buf with the next samples
        virtual void Generate(std::span<SCGSample> buf) = 0;
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

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

    struct ToneGenerator : public SignalGenerator {
        double sampleRate;

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

    struct ChirpGenerator : public SignalGenerator {
        SSG::Modulators::SawGenerator<SCGSampleBase> gen;
        SSG::Modulators::FMModulator<SCGSampleBase> mod;

        double sampleRate;
        double span;
        double rate;
        std::vector<SCGSampleBase> waveBuf;

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

    struct PulseGenerator : public SignalGenerator {
        SSG::Modulators::SquareGenerator<SCGSampleBase> gen;

        double sampleRate;
        double rate;
        double duty;
        std::vector<SCGSampleBase> waveBuf;

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

    // plays the file in a loop, modulated, at the rate of the file
    struct AudioFileGenerator : public SignalGenerator {
        struct Impl;
        std::unique_ptr<Impl> impl;

        std::string fileName;
        std::unique_ptr<SSG::Modulators::AnalogModulator<SCGSampleBase>> mod;
        double sampleRate;

        AudioFileGenerator();
        ~AudioFileGenerator();

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

    // plays the first channel of the file at the rate of the file
    struct IQFileGenerator : public SignalGenerator {
        struct Impl;
        std::unique_ptr<Impl> impl;

        std::string fileName;
        bool loop = true;
        double sampleRate;

        IQFileGenerator();
        ~IQFileGenerator();

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

    struct ZeroGenerator : public SignalGenerator {
        double sampleRate;

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

    // the engine is opened in Init, which builds the sky. it generates with one thread, so it keeps up
    // only as long as it is faster than real time
    struct GnssSpoofingGenerator : public SignalGenerator {
        GnssEngine engine;

        SignalVariant::GnssSpoofing spoofing;
        double duration_s;
        double sampleRate;
        bool isOpen = false;

        // duration_s is how long the signal plays, past it the generator gives silence
        void Setup(const SignalVariant::GnssSpoofing &spoofing_, double duration_s_);

        WorkType GetWorkType() override;
        void SetSampleRate(double sampleRate_) override;
        double GetSampleRate() override;
        void Init() override;
        void Dispose() override;
        void Start() override;
        void Stop() override;
        void Read(std::span<SCGSample> buf) override;
        void Generate(std::span<SCGSample> buf) override;
    };

}
