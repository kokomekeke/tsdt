#include <algorithm>
#include <random>
#include <numbers>
#include <cmath>
#include <format>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/IO/SampleFile.hpp"
#include "SSG/IO/IQFileIn.hpp"
#include "SSG/IO/RealFileIn.hpp"
#include "SSG/IO/PlaybackAdapter.hpp"
#include "SignalGenerator.hpp"

namespace Sagax::SCG {

    namespace {
        // reads the sample rate of the file and checks that it can be played
        template<typename T>
        double ReadFileSampleRate(SSG::IO::SampleFileIn<T> &file, const std::string &fileName) {
            file.SetFile(fileName);
            file.Open();
            Common::Assert(file.IsOpen(), std::format("failed to open file: {}", fileName));
            SSG::IO::SampleFileMeta meta = file.GetMeta();
            file.Close();
            Common::Assert(meta.sampleRate.has_value(), std::format("file has no samplerate info: {}", fileName));
            Common::Assert(meta.nSamples.has_value() && meta.nSamples.value() > 0, std::format("file has no samples or missing samplecount info: {}", fileName));
            return meta.sampleRate.value();
        }

        // fills out from the first channel of the file. returns how many samples were read, which is
        // fewer than asked for only when the mode ends the playback
        template<typename T>
        std::size_t ReadFirstChannel(SSG::IO::PlaybackAdapter<T> &adapter, SSG::Packets::SamplePacket<T> &packet, std::span<T> out) {
            std::size_t n = 0;
            while(n < out.size()) {
                adapter.nSamples = out.size() - n;
                auto plan = adapter.GetReadPlan();
                if(!plan.doRead) {
                    break;
                }
                adapter.CommitReadPlan(plan, packet);
                // the channels of a packet follow each other, the first one is at the front
                std::copy_n(packet.data.begin(), packet.nSamples, out.begin() + std::ptrdiff_t(n));
                n += packet.nSamples;
            }
            return n;
        }

        void FillZero(std::span<SCGSample> buf) {
            std::fill(buf.begin(), buf.end(), SCGSample{ SCGSampleBase(0.0), SCGSampleBase(0.0) });
        }
    }



    struct NoiseGenerator::Impl {
        unsigned int seed;
        std::mt19937 gen;
        std::vector<SCGSampleBase> buf; // pre generated random number buffer
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

        std::normal_distribution<SCGSampleBase> dist(SCGSampleBase(0.0), SCGSampleBase(1.0));
        for(std::size_t i = 0; i < bufSize; ++i) {
            impl->buf[i] = dist(impl->gen);
        }
    }

    NoiseGenerator::WorkType NoiseGenerator::GetWorkType() {
        return WorkType::cpu;
    }

    void NoiseGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double NoiseGenerator::GetSampleRate() {
        return span;
    }

    void NoiseGenerator::Init() {
    }

    void NoiseGenerator::Dispose() {
    }

    void NoiseGenerator::Start() {
        impl->gen.seed(impl->seed); // do this for every run so that we always generate the same output
    }

    void NoiseGenerator::Stop() {
    }

    void NoiseGenerator::Read([[maybe_unused]] std::span<SCGSample> buf) {
        Common::Assert(false, "noise generator has no read");
    }

    void NoiseGenerator::Generate(std::span<SCGSample> buf) {
        // the samples are at the rate span, so the noise fills a band of width span with
        // the density psd
        double sigma = std::sqrt(psd * span / 2.0);
        for(auto &s : buf) {
            auto z0 = SCGSampleBase(impl->buf[impl->gen() % impl->buf.size()] * sigma);
            auto z1 = SCGSampleBase(impl->buf[impl->gen() % impl->buf.size()] * sigma);
            s = SCGSample{ z0, z1 };
        }
    }





    ToneGenerator::WorkType ToneGenerator::GetWorkType() {
        return WorkType::cpu;
    }

    void ToneGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double ToneGenerator::GetSampleRate() {
        return sampleRate;
    }

    void ToneGenerator::Init() {
    }

    void ToneGenerator::Dispose() {
    }

    void ToneGenerator::Start() {
    }

    void ToneGenerator::Stop() {
    }

    void ToneGenerator::Read([[maybe_unused]] std::span<SCGSample> buf) {
        Common::Assert(false, "tone generator has no read");
    }

    void ToneGenerator::Generate(std::span<SCGSample> buf) {
        std::fill(buf.begin(), buf.end(), SCGSample{ SCGSampleBase(1.0), SCGSampleBase(0.0) });
    }





    ChirpGenerator::WorkType ChirpGenerator::GetWorkType() {
        return WorkType::cpu;
    }

    void ChirpGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double ChirpGenerator::GetSampleRate() {
        return sampleRate;
    }

    void ChirpGenerator::Init() {
    }

    void ChirpGenerator::Dispose() {
        waveBuf.clear();
        waveBuf.shrink_to_fit();
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

    void ChirpGenerator::Read([[maybe_unused]] std::span<SCGSample> buf) {
        Common::Assert(false, "chirp generator has no read");
    }

    void ChirpGenerator::Generate(std::span<SCGSample> buf) {
        FillZero(buf);
        waveBuf.assign(buf.size(), SCGSampleBase(0.0));
        gen.Process(sampleRate, waveBuf);
        mod.Process(sampleRate, waveBuf, buf);
    }





    PulseGenerator::WorkType PulseGenerator::GetWorkType() {
        return WorkType::cpu;
    }

    void PulseGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double PulseGenerator::GetSampleRate() {
        return sampleRate;
    }

    void PulseGenerator::Init() {
    }

    void PulseGenerator::Dispose() {
        waveBuf.clear();
        waveBuf.shrink_to_fit();
    }

    void PulseGenerator::Start() {
        gen.ampl = 0.5;
        gen.freq = rate;
        gen.offset = 0.5;
        gen.duty = duty;
        gen.Reset();
    }

    void PulseGenerator::Stop() {
    }

    void PulseGenerator::Read([[maybe_unused]] std::span<SCGSample> buf) {
        Common::Assert(false, "pulse generator has no read");
    }

    void PulseGenerator::Generate(std::span<SCGSample> buf) {
        waveBuf.assign(buf.size(), SCGSampleBase(0.0));
        gen.Process(sampleRate, waveBuf);
        for(std::size_t i = 0; i < buf.size(); ++i) {
            buf[i] = SCGSample{ waveBuf[i], SCGSampleBase(0.0) };
        }
    }





    struct AudioFileGenerator::Impl {
        // used by Read
        SSG::IO::RealFileIn<SCGSampleBase> file;
        SSG::IO::PlaybackAdapter<SCGSampleBase> adapter;
        SSG::Packets::SamplePacket<SCGSampleBase> packet;
        std::vector<SCGSampleBase> readBuf;

        // used by Generate
        std::vector<SCGSampleBase> signal;

        double fileSampleRate;
    };

    AudioFileGenerator::AudioFileGenerator() : impl(std::make_unique<Impl>()) {
    }

    AudioFileGenerator::~AudioFileGenerator() = default;

    AudioFileGenerator::WorkType AudioFileGenerator::GetWorkType() {
        return WorkType::io;
    }

    void AudioFileGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double AudioFileGenerator::GetSampleRate() {
        return impl->fileSampleRate;
    }

    void AudioFileGenerator::Init() {
        impl->fileSampleRate = ReadFileSampleRate(impl->file, fileName);
    }

    void AudioFileGenerator::Dispose() {
        impl->adapter.Close();
        impl->packet = SSG::Packets::SamplePacket<SCGSampleBase>();
        impl->readBuf = std::vector<SCGSampleBase>();
        impl->signal = std::vector<SCGSampleBase>();
    }

    void AudioFileGenerator::Start() {
        impl->adapter.file = &impl->file;
        impl->adapter.doLog = false;
        impl->adapter.mode = SSG::IO::FilePlaybackMode::Infinite{};
        impl->adapter.Close();
        impl->file.SetFile(fileName);
        impl->adapter.Reset();
        impl->adapter.ResetSeek();
        impl->adapter.Open();
        mod->Reset();
    }

    void AudioFileGenerator::Stop() {
        impl->adapter.Close();
    }

    void AudioFileGenerator::Read(std::span<SCGSample> buf) {
        impl->readBuf.resize(buf.size());
        std::size_t n = ReadFirstChannel(impl->adapter, impl->packet, std::span(impl->readBuf));
        Common::Assert(n == buf.size(), "a looping file ended");
        for(std::size_t i = 0; i < buf.size(); ++i) {
            buf[i] = SCGSample{ impl->readBuf[i], SCGSampleBase(0.0) };
        }
    }

    void AudioFileGenerator::Generate(std::span<SCGSample> buf) {
        impl->signal.resize(buf.size());
        for(std::size_t i = 0; i < buf.size(); ++i) {
            impl->signal[i] = buf[i].real();
        }
        FillZero(buf);
        mod->Process(impl->fileSampleRate, impl->signal, buf);
    }





    struct IQFileGenerator::Impl {
        SSG::IO::IQFileIn<SCGSampleBase> file;
        SSG::IO::PlaybackAdapter<SCGSample> adapter;
        SSG::Packets::SamplePacket<SCGSample> packet;
        double fileSampleRate;
    };

    IQFileGenerator::IQFileGenerator() : impl(std::make_unique<Impl>()) {
    }

    IQFileGenerator::~IQFileGenerator() = default;

    IQFileGenerator::WorkType IQFileGenerator::GetWorkType() {
        return WorkType::io;
    }

    void IQFileGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double IQFileGenerator::GetSampleRate() {
        return impl->fileSampleRate;
    }

    void IQFileGenerator::Init() {
        impl->fileSampleRate = ReadFileSampleRate(impl->file, fileName);
    }

    void IQFileGenerator::Dispose() {
        impl->adapter.Close();
        impl->packet = SSG::Packets::SamplePacket<SCGSample>();
    }

    void IQFileGenerator::Start() {
        impl->adapter.file = &impl->file;
        impl->adapter.doLog = false;
        if(loop) {
            impl->adapter.mode = SSG::IO::FilePlaybackMode::Infinite{};
        }
        else {
            impl->adapter.mode = SSG::IO::FilePlaybackMode::File{};
        }
        impl->adapter.Close();
        impl->file.SetFile(fileName);
        impl->adapter.Reset();
        impl->adapter.ResetSeek();
        impl->adapter.Open();
    }

    void IQFileGenerator::Stop() {
        impl->adapter.Close();
    }

    void IQFileGenerator::Read(std::span<SCGSample> buf) {
        std::size_t n = ReadFirstChannel(impl->adapter, impl->packet, buf);
        Common::Assert(!loop || n == buf.size(), "a looping file ended");
        // after the end of the file the rest of the event is silence
        FillZero(buf.subspan(n));
    }

    void IQFileGenerator::Generate([[maybe_unused]] std::span<SCGSample> buf) {
    }





    ZeroGenerator::WorkType ZeroGenerator::GetWorkType() {
        return WorkType::cpu;
    }

    void ZeroGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double ZeroGenerator::GetSampleRate() {
        return sampleRate;
    }

    void ZeroGenerator::Init() {
    }

    void ZeroGenerator::Dispose() {
    }

    void ZeroGenerator::Start() {
    }

    void ZeroGenerator::Stop() {
    }

    void ZeroGenerator::Read([[maybe_unused]] std::span<SCGSample> buf) {
        Common::Assert(false, "zero generator has no read");
    }

    void ZeroGenerator::Generate(std::span<SCGSample> buf) {
        FillZero(buf);
    }





    void GnssSpoofingGenerator::Setup(const SignalVariant::GnssSpoofing &spoofing_, double duration_s_) {
        spoofing = spoofing_;
        duration_s = duration_s_;
    }

    GnssSpoofingGenerator::WorkType GnssSpoofingGenerator::GetWorkType() {
        return WorkType::cpu;
    }

    void GnssSpoofingGenerator::SetSampleRate(double sampleRate_) {
        sampleRate = sampleRate_;
    }

    double GnssSpoofingGenerator::GetSampleRate() {
        return sampleRate;
    }

    void GnssSpoofingGenerator::Init() {
        engine.Open(spoofing, sampleRate, duration_s, 1);
        isOpen = true;
    }

    void GnssSpoofingGenerator::Dispose() {
        if(isOpen) {
            engine.Close();
            isOpen = false;
        }
    }

    void GnssSpoofingGenerator::Start() {
    }

    void GnssSpoofingGenerator::Stop() {
    }

    void GnssSpoofingGenerator::Read([[maybe_unused]] std::span<SCGSample> buf) {
        Common::Assert(false, "gnss generator has no read");
    }

    void GnssSpoofingGenerator::Generate(std::span<SCGSample> buf) {
        std::size_t nRead = engine.Read(buf.data(), buf.size());
        // a read shorter than asked for is the end of the signal
        FillZero(buf.subspan(nRead));
    }

}
