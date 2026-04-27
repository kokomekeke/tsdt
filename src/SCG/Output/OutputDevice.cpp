#include <thread>
#include <vector>
#include <span>
#include <complex>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/Math/Math.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    void FileOutputDevice::Start() {
        SCGIQPacket dummy;
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

    void FileOutputDevice::Take(SCGIQPacket &packet) {
        Common::Assert(file.IsOpen(), "file not open");
        Common::Assert(packet.nChannels == nChannels, "wrong number of channels");
        // assert sample rate

        file.WritePacket(packet);
    }


#ifdef HAVE_USRP
    void UsrpB200Device::Init_() {
        dev.reset();
        dev = std::make_unique<SSG::UsrpB200::Device>(std::format("serial={}", serial), subdevSpec, 1);
        SSG::UsrpB200::DeviceParams params;
        params.sampleRate = sampleRate;
        params.bandwidth = sampleRate + 1.0;
        params.centers = { center };
        params.gains = { gain };
        dev->Configure(params);
        Common::Assert(std::abs(dev->params.sampleRate - sampleRate) < 1.0, "could not set samplerate");
        if(abs(dev->params.bandwidth - (sampleRate + 1.0)) >= 1.0) {
            Common::Logger::Warn("could not set bandwidth");
        }
        Common::Assert(std::abs(dev->params.centers[0] - center) < 1.0, "could not set frequency");
        if(abs(dev->params.gains[0] - gain) >= 1.0) {
            Common::Logger::Warn("could not set gain");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1)); // required magic wait
    }

    void UsrpB200Device::Dispose_() {
        dev.reset();
    }

    void UsrpB200Device::Start() {
        Common::Assert(bool(dev), "device is not set up");
        dev->StartStream();
    }

    void UsrpB200Device::Stop() {
        dev->StopStream();
    }

    void UsrpB200Device::Take(SCGIQPacket &packet) {
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
        dev->Burst(packet.nSamples, ci);
    }
#endif

}

