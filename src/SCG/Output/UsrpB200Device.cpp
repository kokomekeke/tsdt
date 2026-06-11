#ifdef HAVE_USRP

#include <thread>
#include <vector>
#include <span>
#include <complex>
#include <utility>
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
        params.txFreq = center;
        params.rxParams.resize(1);
        params.txParams.resize(1);
        params.txParams[0].configure = true;
        params.txParams[0].gain = gain;
        params.txParams[0].antenna = "TX/RX";
        dev->Configure(params);
        Common::Assert(std::abs(dev->params.sampleRate - sampleRate) < 1.0, "could not set samplerate");
        Common::Assert(std::abs(dev->params.txFreq - center) < 1.0, "could not set frequency");
        if(abs(dev->params.txParams[0].gain - gain) >= 1.0) {
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
        stream = dev->GetTxStream({ 0 });
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
            stream->Burst(packet.nSamples, std::as_const(packet).GetChannels());
        }

    }

}

#endif

