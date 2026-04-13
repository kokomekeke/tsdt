#include <algorithm>
#include <numbers>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SignalGenerator.hpp"

namespace Sagax::SCG {

    void NoiseGenerator::Setup(double sampleRate_, double psd_, std::size_t bufSize, unsigned int seed) {
        sampleRate = sampleRate_;
        psd = psd_;
        gen.seed(seed);
        buf.resize(bufSize);

        // fill the buffer with random numbers from a normal distribution
        // faster than generating them on the fly
        std::normal_distribution<double> dist(0.0, 1.0);
        for(std::size_t i = 0; i < bufSize; ++i) {
            buf[i] = dist(gen);
        }
    }

    void NoiseGenerator::Start() {
    }

    void NoiseGenerator::Stop() {
    }

    void NoiseGenerator::Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);

        double sigma = std::sqrt(psd * packet.sampleRate / 2.0);
        for(std::size_t i = 0; i < packet.data.size(); ++i) {
            double z0 = buf[gen() % buf.size()] * sigma;
            double z1 = buf[gen() % buf.size()] * sigma;
            packet.data[i] = std::complex<double>{z0, z1};
        }
    }

    double NoiseGenerator::GetSampleRate() {
        return sampleRate;
    }


    void ToneGenerator::Start() {
    }

    void ToneGenerator::Stop() {
    }

    void ToneGenerator::Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);
        for(auto &s : packet.data) {
            s = { 1.0, 0.0 };
        }
    }

    double ToneGenerator::GetSampleRate() {
        return sampleRate;
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

    void ChirpGenerator::Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);
        packet.Fill({ 0.0, 0.0 });
        waveBuf.assign(nSamplesMax, 0.0);

        gen.Process(sampleRate, waveBuf);
        mod.Process(sampleRate, waveBuf, packet.data);
    }

    double ChirpGenerator::GetSampleRate() {
        return sampleRate;
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

    void PulseGenerator::Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) {
        packet.Reshape(sampleRate, 1, nSamplesMax, false);

        waveBuf.resize(nSamplesMax);
        gen.Process(sampleRate, waveBuf);

        for(std::size_t i = 0; i < nSamplesMax; ++i) {
            packet.data[i] = std::complex<double>{ waveBuf[i], 0.0 };
        }
    }

    double PulseGenerator::GetSampleRate() {
        return sampleRate;
    }



    void AudioFileGenerator::Start() {
        file.Open(fileName);
        Common::Assert(file.IsOpen(), "failed to open file");
        meta = file.GetMeta();
        Common::Assert(meta.sampleRate.has_value(), "file has no samplerate info");
        Common::Assert(meta.nSamples.has_value() && meta.nSamples.value() > 0, "file has no samples or missing samplecount info");

        mod->Reset();
    }

    void AudioFileGenerator::Stop() {
        file.Close();
    }

    void AudioFileGenerator::Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) {
        if(file.Eof()) {
            Common::Logger::Debug("file wraparound");
            file.WrapAround();
        }

        packetBuf.meta.Clear();
        packetBuf.data.clear();
        file.ReadPacket(packetBuf, nSamplesMax);
        packetBuf.SetNull(false);
        Common::Assert(packetBuf.nChannels == 1, "file must have 1 channel");
        Common::Assert(packetBuf.nSamples <= nSamplesMax, "read too many samples");

        packet.meta.Clear();
        packet.data.clear();
        packet.Reshape(packetBuf.sampleRate, packetBuf.nChannels, packetBuf.nSamples, false);
        packet.Fill({ 0.0, 0.0 });
        mod->Process(packetBuf.sampleRate, packetBuf.data, packet.data);
    }

    double AudioFileGenerator::GetSampleRate() {
        return meta.sampleRate.value();
    }



    void IQFileGenerator::Start() {
        file.Open(fileName);
        Common::Assert(file.IsOpen(), "failed to open file");
        meta = file.GetMeta();
        Common::Assert(meta.sampleRate.has_value(), "file has no samplerate info");
        Common::Assert(meta.nSamples.has_value() && meta.nSamples.value() > 0, "file has no samples or missing samplecount info");
    }

    void IQFileGenerator::Stop() {
        if(file.IsOpen()) {
            file.Close();
        }
    }

    void IQFileGenerator::Generate(SSG::Packets::IQPacket &packet, std::size_t nSamplesMax) {
        if(file.Eof()) {
            Common::Logger::Debug("file wraparound");
            file.WrapAround();
        }

        packet.meta.Clear();
        packet.data.clear();

        file.ReadPacket(packet, nSamplesMax);
        Common::Assert(packet.nSamples <= nSamplesMax, "read too many samples");
        if(packet.nSamples == 0) {
            packet.SetNull(true);
        }
    }

    double IQFileGenerator::GetSampleRate() {
        return meta.sampleRate.value();
    }


}

