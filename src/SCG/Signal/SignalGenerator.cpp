#include <algorithm>
#include <numbers>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/IO/SampleFile.hpp"
#include "SSG/IO/IQFileIn.hpp"
#include "SSG/IO/RealFileIn.hpp"
#include "SSG/Resampler/Resampler.hpp"
#include "SignalGenerator.hpp"

namespace Sagax::SCG {

    namespace {
        template<class Item>
        struct BlockAggregator2 {
        private:
            std::vector<Item> buffer;
            std::int32_t i = 0;
            std::int32_t cnt = 0;

        public:
            std::span<Item> Push(std::int32_t n) {
                if(i != 0) {
                    std::copy_n(
                        buffer.begin() + i,
                        cnt,
                        buffer.begin()
                    );
                    i = 0;
                }
                buffer.resize(cnt + n);
                cnt = cnt + n;
                return std::span(buffer.begin() + cnt - n, buffer.begin() + cnt);
            }

            std::span<Item> PullMax(std::int32_t n) {
                std::int32_t r = std::min(cnt, n);
                cnt = cnt - r;
                i = i + r;
                return std::span(buffer.begin() + i - r, buffer.begin() + i);
            }
        };

        struct ResamplerWrapper {
            double sampleRateIn;
            double sampleRateOut;
            SCGIQPacket packetIn;
            BlockAggregator2<SCGSample> aggregator;
            std::unique_ptr<Sagax::SSG::Resampler::Resampler<SCGSample>> resampler;

            void Setup(double sampleRateIn_, double sampleRateOut_) {
                sampleRateIn = sampleRateIn_;
                sampleRateOut = sampleRateOut_;
                resampler = std::make_unique<Sagax::SSG::Resampler::Resampler<SCGSample>>(
                    Common::Ratio(std::uint64_t(sampleRateOut), std::uint64_t(sampleRateIn)),
                    0.9f,
                    100.f
                );
            }

            void Reset() {
                resampler.reset();
                packetIn.SetNull(true);
            }

            void Push() {
                Common::Assert(packetIn.nChannels == 1, "only 1 channel is allowed");
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





    void NoiseGenerator::Setup(double sampleRate_, double psd_, std::size_t bufSize, unsigned int seed) {
        sampleRate = sampleRate_;
        psd = psd_;
        gen.seed(seed);
        buf.resize(bufSize);

        // fill the buffer with random numbers from a normal distribution
        // faster than generating them on the fly
        std::normal_distribution<SCGSampleBase> dist(SCGSampleBase(0.0), SCGSampleBase(1.0));
        for(std::size_t i = 0; i < bufSize; ++i) {
            buf[i] = dist(gen);
        }
    }

    void NoiseGenerator::Start() {
    }

    void NoiseGenerator::Stop() {
    }

    void NoiseGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);

        double sigma = std::sqrt(psd * packet.sampleRate / 2.0);
        for(std::size_t i = 0; i < packet.data.size(); ++i) {
            auto z0 = SCGSampleBase(buf[gen() % buf.size()] * sigma);
            auto z1 = SCGSampleBase(buf[gen() % buf.size()] * sigma);
            packet.data[i] = SCGSample{ z0, z1 };
        }
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
    }

    void ChirpGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);
        packet.Fill({ SCGSampleBase(0.0), SCGSampleBase(0.0) });
        waveBuf.assign(nSamplesMax, SCGSampleBase(0.0));

        gen.Process(sampleRate, waveBuf);
        mod.Process(sampleRate, waveBuf, packet.data);
    }

    void ChirpGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }



    void PulseGenerator::Start() {
        gen.ampl = 1.0;
        gen.freq = 0.0;
        gen.offset = 0.0;
        gen.duty = duty;
        gen.Reset();
    }

    void PulseGenerator::Stop() {
    }

    void PulseGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);

        waveBuf.resize(nSamplesMax);
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
    }

    void AudioFileGenerator::Generate(SCGIQPacket &packet, std::size_t nSamplesMax) {
        if(impl->file.Eof()) {
            Common::Logger::Debug("file wraparound");
            impl->file.WrapAround();
        }

        std::size_t nSamplesRead = std::size_t((impl->meta.sampleRate.value() / sampleRate) * nSamplesMax) + 100; // just read some more samples its buffered anyway

        impl->realPacketBuf.data.clear();
        impl->file.ReadPacket(impl->realPacketBuf, nSamplesRead);
        impl->realPacketBuf.Reshape(impl->realPacketBuf.sampleRate, 1, impl->realPacketBuf.nSamples, false); // keep only first channel
        impl->resampler.packetIn.Reshape(impl->realPacketBuf.sampleRate, 1, impl->realPacketBuf.nSamples, false);
        impl->resampler.packetIn.Fill({ SCGSampleBase(0.0), SCGSampleBase(0.0) });
        mod->Process(impl->realPacketBuf.sampleRate, impl->realPacketBuf.data, impl->resampler.packetIn.data);
        impl->resampler.Push();
        impl->resampler.PullMax(packet, nSamplesMax);
        packet.meta.Clear();
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

        std::size_t nSamplesRead = std::size_t((impl->meta.sampleRate.value() / sampleRate) * nSamplesMax) + 100; // just read some more samples its buffered anyway
        impl->resampler.packetIn.data.clear();
        impl->file.ReadPacket(impl->resampler.packetIn, nSamplesRead);
        impl->resampler.packetIn.Reshape(impl->resampler.packetIn.sampleRate, 1, impl->resampler.packetIn.nSamples, false); // keep only first channel
        impl->resampler.Push();
        impl->resampler.PullMax(packet, nSamplesMax);
        packet.meta.Clear();
    }

    void IQFileGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }


}

