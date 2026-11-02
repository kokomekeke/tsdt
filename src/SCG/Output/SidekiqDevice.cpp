#ifdef HAVE_SIDEKIQ

#include <thread>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>
#include <span>
#include <complex>
#include <format>
#include <cmath>
#include <numeric>
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
        SSG::Sidekiq::Error error = SSG::Sidekiq::LibraryInit();
        Common::Assert(error == SSG::Sidekiq::Error::none, std::format("failed to initialize the sidekiq library: {}", SSG::Sidekiq::ToString(error)));
        if(dev == nullptr) {
            devOwned.reset();
            error = SSG::Sidekiq::GetDevice(serial, devOwned);
            Common::Assert(error == SSG::Sidekiq::Error::none, std::format("failed to open the sidekiq device: {}", SSG::Sidekiq::ToString(error)));
            dev = devOwned.get();
        }
        Common::Assert(dev, "sidekiq device was not initialized");

        SSG::Sidekiq::DeviceParams params;
        params.nChannels = nChannels;
        params.sampleRate = sampleRate;

        // TODO handler setup should go in the config
        // also there could be more antennas and this number should be checked in the validation, not here
        Common::Assert(nChannels == 2, std::format("the sidekiq device needs exactly 2 channels, nChannels: {}", nChannels));

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
        error = dev->Configure(params);
        Common::Assert(error == SSG::Sidekiq::Error::none, std::format("failed to configure the sidekiq device: {}", SSG::Sidekiq::ToString(error)));
        Common::Assert(std::abs(dev->params.sampleRate - sampleRate) < 1.0, "could not set samplerate");

        for(std::size_t i = 0; i < nChannels; ++i) {
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

    // reads the rf timestamp of the card together with the system time, the system time is
    // taken halfway through the read
    void SidekiqDevice::Start() {
        Common::Assert(dev != nullptr, "device is not set up");
        std::vector<std::size_t> channels(nChannels);
        std::iota(channels.begin(), channels.end(), std::size_t(0));
        SSG::Sidekiq::Error error = dev->GetTxStream(channels, SSG::Sidekiq::StreamMode::timed, stream);
        Common::Assert(error == SSG::Sidekiq::Error::none, std::format("failed to start the sidekiq stream: {}", SSG::Sidekiq::ToString(error)));
        auto before = std::chrono::system_clock::now();
        error = dev->ReadTxTimestamp(syncTimestamp);
        auto after = std::chrono::system_clock::now();
        Common::Assert(error == SSG::Sidekiq::Error::none, std::format("failed to read the sidekiq timestamp: {}", SSG::Sidekiq::ToString(error)));
        syncTime = before + (after - before) / 2;
    }

    void SidekiqDevice::Stop() {
        if(stream) {
            SSG::Sidekiq::TxStats stats;
            if(stream->ReadStats(stats) == SSG::Sidekiq::Error::none) {
                Common::Logger::Info("sidekiq dropped samples: {}, late events: {}, underruns: {}", stats.droppedSamples, stats.lateEvents, stats.underruns);
            }
        }
        stream.reset();
    }

    // the rf timestamp counts samples, a time before the sync point is in the past and
    // goes to timestamp 0, the stream drops those samples as late.
    // late samples are counted by the stream and reported at Stop
    void SidekiqDevice::Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) {
        Common::Assert(channels.size() == nChannels, "wrong number of channels");
        std::optional<std::uint64_t> timestamp;
        if(time.has_value()) {
            double offset = std::chrono::duration<double>(time.value() - syncTime).count() * dev->params.sampleRate;
            std::int64_t t = std::int64_t(syncTimestamp) + std::llround(offset);
            timestamp = std::uint64_t(std::max(t, std::int64_t(0)));
        }
        SSG::Sidekiq::Error error = stream->Burst(channels[0].size(), channels, timestamp);
        if(error != SSG::Sidekiq::Error::none && error != SSG::Sidekiq::Error::late) {
            Common::Logger::Warn("sidekiq burst failed: {}", SSG::Sidekiq::ToString(error));
        }
    }

    void SidekiqDevice::Flush() {
        SSG::Sidekiq::Error error = stream->Flush();
        if(error != SSG::Sidekiq::Error::none) {
            Common::Logger::Warn("sidekiq flush failed: {}", SSG::Sidekiq::ToString(error));
        }
    }

}

#endif
