#include <thread>
#include <vector>
#include <span>
#include <complex>
#include "Common/Assert.hpp"
#include "SSG/Math/Math.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

#ifdef HAVE_USRP
    void UsrpB200Device::Start() {
        Common::Assert(bool(dev), "device is not set up");
        dev->StartStream();
    }

    void UsrpB200Device::Stop() {
        dev->StopStream();
    }

    void UsrpB200Device::Take(SSG::Packets::IQPacket &packet) {
        Common::Assert(packet.nChannels == 1, "only 1 channel is allowed");
        // assert samplerate

        if(packet.null) { // definitely not the way we should be doing this
            auto duration =
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(
                        double(packet.nSamples) / packet.sampleRate
                    )
                );
            std::this_thread::sleep_for(duration);
            return;
        }

        auto ci = packet.GetChannels();
        conversionBuf.resize(packet.nSamples);
        std::vector<std::span<std::complex<float>>> bufs = { std::span<std::complex<float>>(conversionBuf) };
        SSG::Math::Convert<std::complex<double>, std::complex<float>>(ci[0], bufs[0]);

        dev->Burst(packet.nSamples, bufs);
    }
#endif

    void FileOutputDevice::Start() {
        SSG::Packets::IQPacket dummy;
        dummy.Reshape(sampleRate, nChannels, 0, true);
        auto &m = dummy.meta.AddEntry<SSG::Packets::MFrequency>();
        m.frequency = center;
        file.Open(fileName, dummy);
        Common::Assert(file.IsOpen(), "failed to open file");
    }

    void FileOutputDevice::Stop() {
        if(file.IsOpen()) {
            file.Close();
        }
    }

    void FileOutputDevice::Take(SSG::Packets::IQPacket &packet) {
        Common::Assert(file.IsOpen(), "file not open");
        Common::Assert(packet.nChannels == nChannels, "wrong number of channels");
        // assert sample rate

        file.WritePacket(packet);
    }

}

