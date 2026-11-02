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
        // the server brings the library up before it preinitializes the devices, this is
        // for the runs that open the device here instead. the call does nothing after the
        // first one
        Common::Assert(SSG::Sidekiq::LibraryInit(), "failed to initialize the sidekiq library");
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

        Common::Assert(
            handlers.size() == nChannels && centers.size() == nChannels && gains.size() == nChannels,
            "handler, center or gain count does not match the number of channels"
        );

        params.txParams.resize(nChannels);
        params.rxParams.resize(nChannels);
        for(std::size_t i = 0; i < nChannels; ++i) {
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

    void SidekiqDevice::Take(const std::vector<std::span<const SCGSample>> &channels) {
        Common::Assert(channels.size() == nChannels, "wrong number of channels");
        stream->Burst(channels[0].size(), channels);
    }

}

#endif

