#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <format>
#include <optional>
#include <utility>
#include <variant>
#include "Common/Assert.hpp"
#include "SCG/Utility/WaitUntil.hpp"
#include "RadioMonitor.hpp"
#include "ServerParameters.hpp"

namespace Sagax::SCG {

    void RadioMonitor::Start(Common::ThreadPool &threadPool, const ServerConfig &config, const PipelineConfig &pipelineConfig) {
        Common::Assert(!task.valid(), "the radio monitor is already running");
        Common::Assert(bool(onNotReady), "the radio monitor has no onNotReady");
        radios.clear();
        for(auto &radioConfig : config.radios) {
            Radio radio;
            radio.temperature_celsius = 0.0;
            radio.warm = false;
            radio.hot = false;
            radio.hotTemperature_celsius = 0.0;
            radio.loggedTemperature_celsius.reset();
            if([[maybe_unused]] auto *usrpConfig = std::get_if<ServerConfig::Radio::UsrpB200>(&radioConfig.variant)) {
                radio.upperTemperature_celsius = usrpUpperTemperature_celsius;
                radio.lowerTemperature_celsius = usrpLowerTemperature_celsius;
#ifdef HAVE_USRP
                auto it = pipelineConfig.preinit.usrp.devices.find(usrpConfig->serial);
                if(it != pipelineConfig.preinit.usrp.devices.end()) {
                    radio.usrp = it->second.get();
                }
#endif
            }
            else if([[maybe_unused]] auto *sidekiqConfig = std::get_if<ServerConfig::Radio::Sidekiq>(&radioConfig.variant)) {
                radio.upperTemperature_celsius = sidekiqUpperTemperature_celsius;
                radio.lowerTemperature_celsius = sidekiqLowerTemperature_celsius;
#ifdef HAVE_SIDEKIQ
                auto it = pipelineConfig.preinit.sidekiq.devices.find(sidekiqConfig->serial);
                if(it != pipelineConfig.preinit.sidekiq.devices.end()) {
                    radio.sidekiq = it->second.get();
                }
#endif
            }
            else {
                Common::Assert(false, "unknown radio type");
            }
            radios[radioConfig.id] = radio;
        }
        gainUpdates.clear();
        iNextGainUpdate = 0;
        warnings.clear();
        running = true;
        task = threadPool.Dispatch(true, [this]() {
            try {
                Run();
            }
            catch(const std::exception &e) {
                Common::Logger::Error(lh, "the radio monitor stopped: {}", e.what());
            }
        });
    }

    void RadioMonitor::Stop() {
        if(!task.valid()) {
            return;
        }
        {
            std::unique_lock lck(mtx);
            running = false;
        }
        cv.notify_all();
        task.wait();
        task = {};
    }

    void RadioMonitor::SetGainUpdates(std::vector<TsdScenario::GainUpdate> updates) {
        {
            std::unique_lock lck(mtx);
            gainUpdates = std::move(updates);
            iNextGainUpdate = 0;
        }
        cv.notify_all();
    }

    bool RadioMonitor::RadiosReady(std::string &hotRadios) {
        std::unique_lock lck(mtx);
        hotRadios.clear();
        for(auto &[id, radio] : radios) {
            if(radio.hot) {
                hotRadios += std::format("{}{} at {:.1f}C", hotRadios.empty() ? "" : ", ", id, radio.hotTemperature_celsius);
            }
        }
        return hotRadios.empty();
    }

    std::vector<std::string> RadioMonitor::TakeWarnings() {
        std::unique_lock lck(mtx);
        return std::exchange(warnings, {});
    }

    // a temperature check that is due goes before a gain update that is due
    void RadioMonitor::Run() {
        std::unique_lock lck(mtx);
        auto nextCheck = std::chrono::steady_clock::now();
        auto nextLog = nextCheck;
        while(running) {
            auto now = std::chrono::system_clock::now();
            auto nowSteady = std::chrono::steady_clock::now();
            if(nowSteady >= nextCheck) {
                nextCheck = nowSteady + radioMonitorTick;
                bool log = nowSteady >= nextLog;
                if(log) {
                    nextLog += radioTemperatureLogInterval;
                }
                if(CheckTemperatures(log)) {
                    lck.unlock();
                    onNotReady();
                    lck.lock();
                }
                continue;
            }
            if(iNextGainUpdate < gainUpdates.size()) {
                auto time = gainUpdates[iNextGainUpdate].time;
                if(now >= time) {
                    SetGain(gainUpdates[iNextGainUpdate]);
                    ++iNextGainUpdate;
                    continue;
                }
                if(time - now < nextCheck - nowSteady) {
                    WaitUntil(cv, lck, time, radioMonitorPrecision);
                    continue;
                }
            }
            cv.wait_until(lck, nextCheck);
        }
    }

    // the channel of a radio is the channel of its device, the outputs open the device
    // channels in order
    void RadioMonitor::SetGain(const TsdScenario::GainUpdate &update) {
        const Radio &radio = radios.at(update.radioId);
        double late_ms = std::chrono::duration<double, std::milli>(std::chrono::system_clock::now() - update.time).count();
        std::size_t channel = std::size_t(update.channelId);
        std::optional<double> realized;
#ifdef HAVE_USRP
        if(radio.usrp != nullptr) {
            SSG::UsrpB200::Error error = radio.usrp->SetTxGain(channel, update.gain_db);
            if(error != SSG::UsrpB200::Error::none) {
                Common::Logger::Warn(lh, "radio {} ch{}: failed to set the gain: {}", update.radioId, channel, SSG::UsrpB200::ToString(error));
                return;
            }
            realized = radio.usrp->params.txParams[channel].gain;
        }
#endif
#ifdef HAVE_SIDEKIQ
        if(radio.sidekiq != nullptr) {
            SSG::Sidekiq::Error error = radio.sidekiq->SetTxGain(channel, update.gain_db);
            if(error != SSG::Sidekiq::Error::none) {
                Common::Logger::Warn(lh, "radio {} ch{}: failed to set the gain: {}", update.radioId, channel, SSG::Sidekiq::ToString(error));
                return;
            }
            realized = radio.sidekiq->params.txParams[channel].gain;
        }
#endif
        if(!realized.has_value()) {
            Common::Logger::Debug(lh, "radio {} ch{}: no device, gain {}dB, {:.3f}ms late", update.radioId, channel, update.gain_db, late_ms);
            return;
        }
        if(std::abs(realized.value() - update.gain_db) >= 1.0) {
            Common::Logger::Warn(lh, "radio {} ch{}: could not set gain. Desired: {}, actual: {}", update.radioId, channel, update.gain_db, realized.value());
        }
        Common::Logger::Debug(lh, "radio {} ch{}: gain {}dB, {:.3f}ms late", update.radioId, channel, realized.value(), late_ms);
    }

    // returns true when the radios turned not ready
    bool RadioMonitor::CheckTemperatures(bool log) {
        auto isHot = [](const auto &entry) {
            return entry.second.hot;
        };
        bool wasReady = std::none_of(radios.begin(), radios.end(), isHot);
        std::string logged;
        for(auto &[id, radio] : radios) {
            std::optional<double> temperature_celsius;
#ifdef HAVE_USRP
            if(radio.usrp != nullptr) {
                double read_celsius;
                SSG::UsrpB200::Error error = radio.usrp->ReadTemperature(read_celsius);
                if(error != SSG::UsrpB200::Error::none) {
                    Common::Logger::Warn(lh, "radio {}: failed to read the temperature: {}", id, SSG::UsrpB200::ToString(error));
                }
                else {
                    temperature_celsius = read_celsius;
                }
            }
#endif
#ifdef HAVE_SIDEKIQ
            if(radio.sidekiq != nullptr) {
                double read_celsius;
                SSG::Sidekiq::Error error = radio.sidekiq->ReadTemperature(read_celsius);
                if(error != SSG::Sidekiq::Error::none) {
                    Common::Logger::Warn(lh, "radio {}: failed to read the temperature: {}", id, SSG::Sidekiq::ToString(error));
                }
                else {
                    temperature_celsius = read_celsius;
                }
            }
#endif
            if(!temperature_celsius.has_value()) {
                continue;
            }
            radio.temperature_celsius = temperature_celsius.value();
            if(log && (!radio.loggedTemperature_celsius.has_value() || std::abs(radio.temperature_celsius - radio.loggedTemperature_celsius.value()) >= radioTemperatureLogChange_celsius)) {
                radio.loggedTemperature_celsius = radio.temperature_celsius;
                logged += std::format("{}{} at {:.1f}C", logged.empty() ? "" : ", ", id, radio.temperature_celsius);
            }
            if(!radio.warm && radio.temperature_celsius >= radio.lowerTemperature_celsius) {
                radio.warm = true;
                warnings.push_back(std::format(
                    "radio {} is at {:.1f}C, at or above {:.1f}C, the radios are stopped at {:.1f}C",
                    id, radio.temperature_celsius, radio.lowerTemperature_celsius, radio.upperTemperature_celsius
                ));
            }
            else if(radio.warm && radio.temperature_celsius < radio.lowerTemperature_celsius) {
                radio.warm = false;
            }
            if(!radio.hot && radio.temperature_celsius >= radio.upperTemperature_celsius) {
                radio.hot = true;
                radio.hotTemperature_celsius = radio.temperature_celsius;
                Common::Logger::Warn(lh, "radio {} is at {:.1f}C, at or above {:.1f}C", id, radio.temperature_celsius, radio.upperTemperature_celsius);
            }
            else if(radio.hot && radio.temperature_celsius < radio.lowerTemperature_celsius) {
                radio.hot = false;
                Common::Logger::Info(lh, "radio {} is at {:.1f}C, below {:.1f}C", id, radio.temperature_celsius, radio.lowerTemperature_celsius);
            }
        }
        if(!logged.empty()) {
            Common::Logger::Info(lh, "temperatures: {}", logged);
        }
        return wasReady && std::any_of(radios.begin(), radios.end(), isHot);
    }

}
