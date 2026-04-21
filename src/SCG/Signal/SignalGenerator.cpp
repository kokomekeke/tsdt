#include <algorithm>
#include <random>
#include <numbers>
#include <cmath>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/Math/Math.hpp"
#include "SSG/IO/SampleFile.hpp"
#include "SSG/IO/IQFileIn.hpp"
#include "SSG/IO/RealFileIn.hpp"
#include "SSG/Resampler/Resampler.hpp"
#include "SCG/Utility/BlockAggregator.hpp"
#include "SignalGenerator.hpp"

namespace Sagax::SCG {

    namespace {
        struct ResamplerWrapper {
            double sampleRateIn;
            double sampleRateOut;
            SCGIQPacket packetIn;
            BlockAggregator<SCGSample> aggregator;
            std::unique_ptr<Sagax::SSG::Resampler::Resampler<SCGSample>> resampler;

            void Setup(double sampleRateIn_, double sampleRateOut_) {
                sampleRateIn = sampleRateIn_;
                sampleRateOut = sampleRateOut_;
                resampler = std::make_unique<Sagax::SSG::Resampler::Resampler<SCGSample>>(
                    Common::Ratio(std::uint64_t(sampleRateOut), std::uint64_t(sampleRateIn)),
                    0.9f,
                    150.f
                );
            }

            std::size_t SamplesToRead(std::size_t nSamplesOutMax) {
                if(nSamplesOutMax <= aggregator.Cnt()) {
                    return 0;
                }
                else {
                    std::size_t nSamplesOut = nSamplesOutMax - aggregator.Cnt();
                    std::size_t nSamplesIn = std::size_t(std::ceil((sampleRateIn / sampleRateOut) * nSamplesOut));
                    // just read some more samples because the BlockAggregator mechanics work funny
                    return nSamplesIn + 64;
                }
            }

            void Reset() {
                resampler.reset();
                packetIn.SetNull(true);
            }

            void Push() {
                Common::Assert(packetIn.nChannels == 1, "only 1 channel is allowed");
                Common::Assert(packetIn.sampleRate == sampleRateIn, "samplerate mismatch");
                auto s1 = resampler->Process(packetIn.data);
                auto s2 = aggregator.Push(std::int32_t(s1.size()));
                std::copy_n(s1.begin(), s1.size(), s2.begin());
            }

            void PullMax(SCGIQPacket &packetOut, std::size_t n) {
                auto s = aggregator.PullMax(std::int32_t(n));
                packetOut.Reshape(sampleRateOut, 1, s.size(), false);
                packetOut.meta.Clear();
                std::copy_n(s.begin(), s.size(), packetOut.data.begin());
            }
        };
    }



    struct NoiseGenerator::Impl {
        unsigned int seed;
        std::mt19937 gen;
        std::vector<SCGSampleBase> buf; // pre generated random number buffer
        ResamplerWrapper resampler;
    };

    NoiseGenerator::NoiseGenerator() : impl(std::make_unique<Impl>()) {
    }

    NoiseGenerator::~NoiseGenerator() = default;

    void NoiseGenerator::Setup(double span_, double psd_, std::size_t bufSize, unsigned int seed_) {
        span = span_;
        psd = psd_;
        impl->seed = seed_;
        impl->gen.seed(seed_);
        impl->buf.resize(bufSize);

        // fill the buffer with random numbers from a normal distribution
        // faster than generating them on the fly
        std::normal_distribution<SCGSampleBase> dist(SCGSampleBase(0.0), SCGSampleBase(1.0));
        for(std::size_t i = 0; i < bufSize; ++i) {
            impl->buf[i] = dist(impl->gen);
        }
    }

    void NoiseGenerator::Start() {
        impl->resampler.Setup(span, sampleRate);
        impl->gen.seed(impl->seed); // do this for every run so that we always generate the same output
    }

    void NoiseGenerator::Stop() {
        impl->resampler.Reset();
    }

    void NoiseGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        std::size_t nSamplesRead = impl->resampler.SamplesToRead(nSamplesMax);
        impl->resampler.packetIn.Reshape(span, 1, nSamplesRead, false);

        double sigma = std::sqrt(psd * sampleRate / 2.0);
        for(std::size_t i = 0; i < nSamplesRead; ++i) {
            auto z0 = SCGSampleBase(impl->buf[impl->gen() % impl->buf.size()] * sigma);
            auto z1 = SCGSampleBase(impl->buf[impl->gen() % impl->buf.size()] * sigma);
            impl->resampler.packetIn.data[i] = SCGSample{ z0, z1 };
        }

        impl->resampler.Push();
        impl->resampler.PullMax(packet, nSamplesMax);
    }

    void NoiseGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }





    void ToneGenerator::Start() {
    }

    void ToneGenerator::Stop() {
    }

    void ToneGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);
        packet.meta.Clear();
        for(auto &s : packet.data) {
            s = { SCGSampleBase(1.0), SCGSampleBase(0.0) };
        }
    }

    void ToneGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }





    void ChirpGenerator::Start() {
        gen.ampl = span / 2.0;
        gen.freq = rate;
        gen.offset = 0.0;
        mod.freq = 0.0;
        mod.ampl = 1.0;
        mod.deviation = 1.0;
        gen.Reset();
        mod.Reset();
    }

    void ChirpGenerator::Stop() {
        waveBuf.clear();
        waveBuf.shrink_to_fit();
    }

    void ChirpGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);
        packet.meta.Clear();
        packet.Fill({ SCGSampleBase(0.0), SCGSampleBase(0.0) });
        waveBuf.assign(nSamplesMax, SCGSampleBase(0.0));

        gen.Process(sampleRate, waveBuf);
        mod.Process(sampleRate, waveBuf, packet.data);
    }

    void ChirpGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }





    void PulseGenerator::Start() {
        gen.ampl = 0.5;
        gen.freq = rate;
        gen.offset = 0.5;
        gen.duty = duty;
        gen.Reset();
    }

    void PulseGenerator::Stop() {
        waveBuf.clear();
        waveBuf.shrink_to_fit();
    }

    void PulseGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);
        packet.meta.Clear();

        waveBuf.assign(nSamplesMax, SCGSampleBase(0.0));
        gen.Process(sampleRate, waveBuf);

        for(std::size_t i = 0; i < nSamplesMax; ++i) {
            packet.data[i] = SCGSample{
                SCGSampleBase(waveBuf[i]),
                SCGSampleBase(0.0)
            };
        }
    }

    void PulseGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }





    struct AudioFileGenerator::Impl {
        ResamplerWrapper resampler;
        SCGRealPacket realPacketBuf;
        SSG::IO::RealFileIn<SCGSampleBase> file;
        SSG::IO::SampleFileMeta meta;
    };

    AudioFileGenerator::AudioFileGenerator() : impl(std::make_unique<Impl>()) {
    }

    AudioFileGenerator::~AudioFileGenerator() = default;

    void AudioFileGenerator::Start() {
        impl->file.Open(fileName);
        Common::Assert(impl->file.IsOpen(), "failed to open file");
        impl->meta = impl->file.GetMeta();
        Common::Assert(impl->meta.sampleRate.has_value(), "file has no samplerate info");
        Common::Assert(impl->meta.nSamples.has_value() && impl->meta.nSamples.value() > 0, "file has no samples or missing samplecount info");
        impl->resampler.Setup(impl->meta.sampleRate.value(), sampleRate);
        mod->Reset();
    }

    void AudioFileGenerator::Stop() {
        if(impl->file.IsOpen()) {
            impl->file.Close();
        }
        impl->resampler.Reset();
        impl->realPacketBuf.SetNull(true);
        mod->Reset();
    }

    void AudioFileGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        if(impl->file.Eof()) {
            Common::Logger::Debug("file wraparound");
            impl->file.WrapAround();
        }

        std::size_t nSamplesRead = impl->resampler.SamplesToRead(nSamplesMax);

        impl->realPacketBuf.data.clear();
        impl->file.ReadPacket(impl->realPacketBuf, nSamplesRead);
        impl->realPacketBuf.Reshape(impl->realPacketBuf.sampleRate, 1, impl->realPacketBuf.nSamples, false); // keep only first channel
        impl->resampler.packetIn.Reshape(impl->realPacketBuf.sampleRate, 1, impl->realPacketBuf.nSamples, false);
        impl->resampler.packetIn.Fill({ SCGSampleBase(0.0), SCGSampleBase(0.0) });
        mod->Process(impl->realPacketBuf.sampleRate, impl->realPacketBuf.data, impl->resampler.packetIn.data);
        impl->resampler.Push();
        impl->resampler.PullMax(packet, nSamplesMax);
    }

    void AudioFileGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }





    struct IQFileGenerator::Impl {
        ResamplerWrapper resampler;
        SSG::IO::IQFileIn<SCGSampleBase> file;
        SSG::IO::SampleFileMeta meta;
    };

    IQFileGenerator::IQFileGenerator() : impl(std::make_unique<Impl>()) {
    }

    IQFileGenerator::~IQFileGenerator() = default;

    void IQFileGenerator::Start() {
        impl->file.Open(fileName);
        Common::Assert(impl->file.IsOpen(), "failed to open file");
        impl->meta = impl->file.GetMeta();
        Common::Assert(impl->meta.sampleRate.has_value(), "file has no samplerate info");
        Common::Assert(impl->meta.nSamples.has_value() && impl->meta.nSamples.value() > 0, "file has no samples or missing samplecount info");
        impl->resampler.Setup(impl->meta.sampleRate.value(), sampleRate);
    }

    void IQFileGenerator::Stop() {
        if(impl->file.IsOpen()) {
            impl->file.Close();
        }
        impl->resampler.Reset();
    }

    void IQFileGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        if(impl->file.Eof()) {
            Common::Logger::Debug("file wraparound");
            impl->file.WrapAround();
        }

        std::size_t nSamplesRead = impl->resampler.SamplesToRead(nSamplesMax);
        impl->resampler.packetIn.data.clear();
        impl->file.ReadPacket(impl->resampler.packetIn, nSamplesRead);
        impl->resampler.packetIn.Reshape(impl->resampler.packetIn.sampleRate, 1, impl->resampler.packetIn.nSamples, false); // keep only first channel
        impl->resampler.Push();
        impl->resampler.PullMax(packet, nSamplesMax);
    }

    void IQFileGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }


}

