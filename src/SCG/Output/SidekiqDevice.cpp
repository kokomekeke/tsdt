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
        if(sidekiqInitialized == false) {
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
        params.nChannels = nChannels;
        params.sampleRate = sampleRate;

        // TODO handler setup should go in the config
        // also there could be more antennas and this number should be checked in the validation, not here        
        Common::Assert(nChannels >= 2, std::format("too many channels, nChannels: {}", nChannels));

        params.txParams.resize(nChannels);
        params.rxParams.resize(nChannels);
        for(std::int32_t i = 0; i < nChannels; ++i) {
            auto &txp = params.txParams[i];
            txp.configure = true;
            txp.gain = gains[i];
            txp.handler = handlers[i];
            txp.center = centers[i];
        }
        dev->Configure(params);
        Common::Assert(std::abs(dev->params.sampleRate - sampleRate) < 1.0, "could not set samplerate");

        for(std::int32_t i = 0; i < nChannels; ++i) {
            if(std::abs(dev->params.txParams[i].gain - gains[i]) >= 1.0) {
                Common::Logger::Warn("could not set gain. Desired: {}, actual: {}", gains[i], dev->params.txParams[i].gain);
            }
            if(std::abs(dev->params.txParams[i].center - centers[i]) >= 1.0) {
                Common::Logger::Warn("could not set frequency. Desired: {}, actual: {}", centers[i], dev->params.txParams[i].center);
            }
        }
    }

    void SidekiqDevice::Dispose_() {
        if(dev == devOwned.get()) {
            devOwned.reset();
        }
        dev = nullptr;
    }

    void SidekiqDevice::Start() {
        Common::Assert(dev != nullptr, "device is not set up");
        // TODO: Ezt át kell alakítani igazából a sideiq le tudja kezelni a több különálló channelt párhuzamosan.
        stream = dev->GetTxStream({ 0 });
    }

    void SidekiqDevice::Stop() {
        stream.reset();
    }

    void SidekiqDevice::Take(SCGIQPacket &packet) {
        Common::Assert(packet.nChannels == nChannels, "wrong number of channels");
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

