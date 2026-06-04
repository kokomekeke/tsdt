#ifdef HAVE_USRP

#include <thread>
#include <vector>
#include <span>
#include <complex>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/Math/Math.hpp"
#include "OutputDevice.hpp"
#include "UsrpB200Device.hpp"

namespace Sagax::SCG {


    void UsrpB200Device::Init_() {
        if(dev == nullptr) {
            devOwned.reset();
            devOwned = SSG::UsrpB200::GetDevice(serial);
            dev = devOwned.get();
        }
        Common::Assert(dev, "usrp b200 device was not initialized");

        SSG::UsrpB200::DeviceParams params;
        params.subdevSpec = "A:A";
        params.nChannels = 1;
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
        if(dev == devOwned.get()) {
            devOwned.reset();
        }
        dev = nullptr;
    }

    void UsrpB200Device::Start() {
        Common::Assert(dev != nullptr, "device is not set up");
        stream = dev->GetTxStream();
    }

    void UsrpB200Device::Stop() {
        stream.reset();
    }

    void UsrpB200Device::Take(SCGIQPacket &packet) {
        Common::Assert(packet.nChannels == 1, "only 1 channel is allowed");
        // assert samplerate

        if(packet.null) {
            auto duration =
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(
                        double(packet.nSamples) / packet.sampleRate
                    )
                );
            std::this_thread::sleep_for(duration); // likely not the way we should be doing this
        }
        else {
            stream->Burst(packet.nSamples, packet.GetChannels());
        }

    }

}

#endif

