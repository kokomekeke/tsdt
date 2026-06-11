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
#include "Sidekiq.hpp"

namespace Sagax::SCG {


    void SidekiqDevice::Init_() {
        if(dev == nullptr) {
            devOwned.reset();
            devOwned = SSG::UsrpB200::GetDevice(serial);
            dev = devOwned.get();
        }
        Common::Assert(dev, " device was not initialized");

        // TODO
    }

    void SidekiqDevice::Dispose_() {
        if(dev == devOwned.get()) {
            devOwned.reset();
        }
        dev = nullptr;
    }

    void SidekiqDevice::Start() {
        Common::Assert(dev != nullptr, "device is not set up");
        stream = dev->GetTxStream({ 0 });
    }

    void SidekiqDevice::Stop() {
        stream.reset();
    }

    void SidekiqDevice::Take(SCGIQPacket &packet) {
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
            // stream->Burst(packet.nSamples, std::as_const(packet).GetChannels());
        }

    }

}

#endif

