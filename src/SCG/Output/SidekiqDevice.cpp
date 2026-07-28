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
        // TODO: ne legyen már hardcodeolva a channel number
        params.nChannels = 2;
        params.sampleRate = sampleRate;
        params.txFreq = center;
        // TODO: Itt se legyen hardcodeolva a ch number
        params.rxParams.resize(2);
        params.txParams.resize(2);
        params.txParams[0].configure = true;
        params.txParams[0].gain = gain;
        params.txParams[0].antenna = "A1";
        params.txParams[1].configure = true;
        params.txParams[1].gain = gain;
        params.txParams[1].antenna = "B1";
        dev->Configure(params);
        Common::Assert(std::abs(dev->params.sampleRate - sampleRate) < 1.0, "could not set samplerate");
        Common::Assert(std::abs(dev->params.txFreq - center) < 1.0, "could not set frequency");
        if(abs(dev->params.txParams[0].gain - gain) >= 1.0) {
            Common::Logger::Warn("could not set gain");
        }
    }

    void SidekiqDevice::Dispose_() {
        if(dev == devOwned.get()) {
            devOwned.reset();
        }
        dev = nullptr;
    }

    void SidekiqDevice::Start() {
        Common::Logger::Debug("Start");
        Common::Assert(dev != nullptr, "device is not set up");
        // TODO: Ezt át kell alakítani igazából a sideiq le tudja kezelni a több különálló channelt párhuzamosan.
        stream = dev->GetTxStream({ 0 });
    }

    void SidekiqDevice::Stop() {
        Common::Logger::Debug("Start");
        stream.reset();
    }

    void SidekiqDevice::Take(SCGIQPacket &packet) {
        static uint32_t counter = 0;
        counter++;
        if (counter % 10000 == 0)
        {
            Common::Logger::Debug("Start");
        }
        
        Common::Assert(packet.nChannels <= 2, "maximum 2 channel is allowed");
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

