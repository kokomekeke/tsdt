#ifdef HAVE_USRP

#include <thread>
#include <chrono>
#include <optional>
#include <vector>
#include <span>
#include <complex>
#include <format>
#include <cmath>
#include <utility>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/Math/Math.hpp"
#include "OutputDevice.hpp"
#include "UsrpB200Device.hpp"

namespace Sagax::SCG {


    void UsrpB200Device::Init_() {
        if(devPreinit != nullptr) {
            dev = devPreinit;
        }
        else {
            devOwned.reset();
            SSG::UsrpB200::Error error = SSG::UsrpB200::GetDevice(serial, devOwned);
            Common::Assert(error == SSG::UsrpB200::Error::none, std::format("failed to open the usrp b200 device: {}", SSG::UsrpB200::ToString(error)));
            dev = devOwned.get();
        }
        Common::Assert(dev, "usrp b200 device was not initialized");

        SSG::UsrpB200::DeviceParams params;
        params.subdevSpec = subdevSpec;
        params.nChannels = 1;
        params.sampleRate = sampleRate;
        params.txFreq = center;
        params.rxParams.resize(1);
        params.txParams.resize(1);
        params.txParams[0].configure = true;
        params.txParams[0].gain = gain;
        params.txParams[0].antenna = "TX/RX";
        SSG::UsrpB200::Error error = dev->Configure(params);
        Common::Assert(error == SSG::UsrpB200::Error::none, std::format("failed to configure the usrp b200 device: {}", SSG::UsrpB200::ToString(error)));
        Common::Assert(std::abs(dev->params.sampleRate - sampleRate) < 1.0, "could not set samplerate");
        Common::Assert(std::abs(dev->params.txFreq - center) < 1.0, "could not set frequency");
        if(doLog && std::abs(dev->params.txParams[0].gain - gain) >= 1.0) {
            Common::Logger::Warn("could not set gain");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1)); // required magic wait
    }

    void UsrpB200Device::Dispose_() {
        devOwned.reset();
        dev = nullptr;
    }

    // sets the device time to 0 and keeps the system time of that. the device time is
    // behind it by the time the setting takes to reach the device over usb
    void UsrpB200Device::Start() {
        Common::Assert(dev != nullptr, "device is not set up");
        SSG::UsrpB200::Error error = dev->GetTxStream({ 0 }, SSG::UsrpB200::StreamMode::timed, stream);
        Common::Assert(error == SSG::UsrpB200::Error::none, std::format("failed to start the usrp b200 stream: {}", SSG::UsrpB200::ToString(error)));
        deviceTimeZero = std::chrono::system_clock::now();
        error = dev->SetTime(0.0);
        Common::Assert(error == SSG::UsrpB200::Error::none, std::format("failed to set the usrp b200 time: {}", SSG::UsrpB200::ToString(error)));
        loggedStats = { 0, 0, 0, 0 };
        nextStatsCheck = std::chrono::steady_clock::now() + statsLogInterval;
    }

    void UsrpB200Device::Stop() {
        if(stream && doLog) {
            SSG::UsrpB200::TxStats stats;
            if(stream->ReadStats(stats) == SSG::UsrpB200::Error::none) {
                Common::Logger::Info("usrp b200 {} at stop, dropped samples: {}, underflows: {}, late packets: {}, sequence errors: {}", serial, stats.droppedSamples, stats.underflowEvents, stats.lateEvents, stats.sequenceErrorEvents);
            }
        }
        stream.reset();
    }

    // the stats are read every statsLogInterval and logged if they differ from the last
    // logged ones, and once more at Stop
    void UsrpB200Device::Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) {
        Common::Assert(channels.size() == 1, "only 1 channel is allowed");
        std::optional<double> deviceTime;
        if(time.has_value()) {
            deviceTime = std::chrono::duration<double>(time.value() - deviceTimeZero).count();
        }
        SSG::UsrpB200::Error error = stream->Burst(channels[0].size(), channels, deviceTime);
        if(!doLog) {
            return;
        }
        if(error != SSG::UsrpB200::Error::none && error != SSG::UsrpB200::Error::late) {
            Common::Logger::Warn("usrp b200 burst failed: {}", SSG::UsrpB200::ToString(error));
        }
        auto now = std::chrono::steady_clock::now();
        if(now < nextStatsCheck) {
            return;
        }
        nextStatsCheck = now + statsLogInterval;
        SSG::UsrpB200::TxStats stats;
        if(stream->ReadStats(stats) != SSG::UsrpB200::Error::none) {
            return;
        }
        if(stats.droppedSamples != loggedStats.droppedSamples || stats.underflowEvents != loggedStats.underflowEvents || stats.lateEvents != loggedStats.lateEvents || stats.sequenceErrorEvents != loggedStats.sequenceErrorEvents) {
            Common::Logger::Info("usrp b200 {} dropped samples: {}, underflows: {}, late packets: {}, sequence errors: {}", serial, stats.droppedSamples, stats.underflowEvents, stats.lateEvents, stats.sequenceErrorEvents);
            loggedStats = stats;
        }
    }

    void UsrpB200Device::Flush() {
        SSG::UsrpB200::Error error = stream->Flush();
        if(error != SSG::UsrpB200::Error::none && doLog) {
            Common::Logger::Warn("usrp b200 flush failed: {}", SSG::UsrpB200::ToString(error));
        }
    }

}

#endif

