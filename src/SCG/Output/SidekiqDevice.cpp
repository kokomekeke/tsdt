#ifdef HAVE_SIDEKIQ

#include <thread>
#include <vector>
#include <span>
#include <complex>
#include <utility>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/Math/Math.hpp"
#include "OutputDevice.hpp"
#include "SidekiqDevice.hpp"

namespace Sagax::SCG {


    void SidekiqDevice::Init_() {
        static bool sidekiqInitialized = false;
        if(sidekiqManager == nullptr || sidekiqInitialized == false) {
            // the sidekiq api is unique you can initialize only once
            sidekiqManager = std::make_unique<SSG::Sidekiq::SidekiqManager>();
            sidekiqManager->Init();
            sidekiqInitialized = true;
        }
        if(dev == nullptr) {
            devOwned.reset();
            devOwned = SSG::Sidekiq::GetDevice(serial);
            dev = devOwned.get();
        }
        Common::Assert(dev, "sidekiq device was not initialized");

        SSG::Sidekiq::DeviceParams params;
        params.nChannels = 1;
        params.sampleRate = sampleRate;
        params.txFreq = center;
        params.rxParams.resize(1);
        params.txParams.resize(1);
        params.txParams[0].configure = true;
        params.txParams[0].gain = gain;
        params.txParams[0].antenna = "A1";
        dev->Configure(params);
        // Common::Assert(std::abs(dev->parasidekiqInitializedms.sampleRate - sampleRate) < 1.0, "could not set samplerate");
        // Common::Assert(std::abs(dev->params.txFreq - center) < 1.0, "could not set frequency");
        // if(abs(dev->params.txParams[0].gain - gain) >= 1.0) {
        //     Common::Logger::Warn("could not set gain");
        // }
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

