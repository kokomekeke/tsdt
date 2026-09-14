#include <format>
#include <map>
#include <cmath>
#include <string>
#include <vector>
#include <variant>
#include <algorithm>
#include "Common/Assert.hpp"
#include "RidTransform.hpp"

namespace Sagax::SCG {

    ValidationResult ValidateConfig(const ServerConfig &cfg) {
        ValidationResult res;

        if(cfg.mqtt.brokerHost.empty()) {
            res.errors.push_back("mqtt.broker_host must not be empty");
        }
        if(cfg.mqtt.brokerPort <= 0) {
            res.errors.push_back("mqtt.broker_port must be positive");
        }
        if(cfg.mqtt.commandTopic.empty()) {
            res.errors.push_back("mqtt.command_topic must not be empty");
        }
        if(cfg.mqtt.logTopic.empty()) {
            res.errors.push_back("mqtt.log_topic must not be empty");
        }
        if(cfg.mqtt.errorTopic.empty()) {
            res.errors.push_back("mqtt.error_topic must not be empty");
        }

        if(cfg.scenarioFetchTimeout_s <= 0.0) {
            res.errors.push_back("scenario_fetch_timeout_s must be positive");
        }

        std::map<std::string, std::size_t> radioIds;
        for(std::size_t i = 0; i < cfg.radios.size(); ++i) {
            const auto &radio = cfg.radios[i];

            if(radio.id.empty()) {
                res.errors.push_back(std::format("radios[{}]: id must not be empty", i));
            }
            else if(radioIds.count(radio.id)) {
                res.errors.push_back(std::format("radios[{}]: duplicate id: {}", i, radio.id.c_str()));
            }
            else {
                radioIds[radio.id] = i;
            }

            // a mocked radio is built from the scenario alone, so the libraries the type
            // needs are only required when the radios are real
            if(std::holds_alternative<ServerConfig::Radio::UsrpB200>(radio.variant)) {
#ifndef HAVE_USRP
                if(!cfg.mockRadios) {
                    res.errors.push_back(std::format("radios[{}]: usrp radio, but this build has no usrp support", i));
                }
#endif
            }
            else if(std::holds_alternative<ServerConfig::Radio::Sidekiq>(radio.variant)) {
#ifndef HAVE_SIDEKIQ
                if(!cfg.mockRadios) {
                    res.errors.push_back(std::format("radios[{}]: sidekiq radio, but this build has no sidekiq support", i));
                }
#endif
            }
            else {
                res.errors.push_back(std::format("radios[{}]: unknown radio type", i));
            }

            if(auto *sk = std::get_if<ServerConfig::Radio::Sidekiq>(&radio.variant)) {
                // str_to_tx_hdl maps these four names, anything else lands on
                // skiq_tx_hdl_end and the card is handed a port that does not exist
                static const std::vector<std::string> knownHandlers = { "A1", "A2", "B1", "B2" };
                if(sk->handlers.empty()) {
                    res.errors.push_back(std::format("radios[{}]: handlers must not be empty", i));
                }
                std::map<std::string, std::size_t> usedHandlers;
                for(std::size_t h = 0; h < sk->handlers.size(); ++h) {
                    const std::string &handler = sk->handlers[h];
                    if(std::find(knownHandlers.begin(), knownHandlers.end(), handler) == knownHandlers.end()) {
                        res.errors.push_back(std::format("radios[{}]: unknown handler: {}", i, handler.c_str()));
                    }
                    else if(usedHandlers.count(handler)) {
                        res.errors.push_back(std::format("radios[{}]: duplicate handler: {}", i, handler.c_str()));
                    }
                    else {
                        usedHandlers[handler] = h;
                    }
                }
            }
        }

        return res;
    }

    ValidationResult ValidateRid(const ServerConfig &cfg, const Rid &rid) {
        ValidationResult res;

        if(rid.metadata.name.empty()) {
            res.errors.push_back("metadata.name must not be empty");
        }
        if(rid.globalSettings.duration_s <= 0.0) {
            res.errors.push_back("globalSettings.duration_s must be positive");
        }

        std::map<std::string, std::size_t> radioIds;
        for(std::size_t i = 0; i < cfg.radios.size(); ++i) {
            radioIds[cfg.radios[i].id] = i;
        }

        std::map<std::string, std::size_t> usedRadioSettings;
        for(std::size_t i = 0; i < rid.radioSettings.size(); ++i) {
            const auto &radioSetting = rid.radioSettings[i];

            if(radioSetting.radioId.empty()) {
                res.errors.push_back(std::format("radioSettings[{}]: radioId must not be empty", i));
            }
            else if(radioIds.count(radioSetting.radioId) == 0) {
                res.errors.push_back(std::format("radioSettings[{}]: unknown radioId: {}", i, radioSetting.radioId.c_str()));
            }

            if(usedRadioSettings.count(radioSetting.radioId)) {
                res.errors.push_back(std::format("radioSettings[{}]: duplicate radioId: {}", i, radioSetting.radioId.c_str()));
            }
            else {
                usedRadioSettings[radioSetting.radioId] = i;
            }

            if(radioSetting.nChannels < 0) {
                res.errors.push_back(std::format("radioSettings[{}]: negative number of channels", i));
            }

            if(radioSetting.bandwidth_hz <= 0.0) {
                res.errors.push_back(std::format("radioSettings[{}]: bandwidth_hz must be positive", i));
            }

            if(radioSetting.centers_hz.size() != std::size_t(radioSetting.nChannels)) {
                res.errors.push_back(std::format("radioSettings[{}]: centers_hz array has wrong size", i));
            }

            if(radioSetting.gains_db.size() != std::size_t(radioSetting.nChannels)) {
                res.errors.push_back(std::format("radioSettings[{}]: gains_db array has wrong size", i));
            }

            // a sidekiq transmits through the ports named in the config, so the scenario
            // has to ask for exactly as many channels as that radio has handlers
            if(radioIds.count(radioSetting.radioId) > 0) {
                const auto &radio = cfg.radios[radioIds[radioSetting.radioId]];
                if(auto *sk = std::get_if<ServerConfig::Radio::Sidekiq>(&radio.variant)) {
                    if(std::size_t(radioSetting.nChannels) != sk->handlers.size()) {
                        res.errors.push_back(std::format(
                            "radioSettings[{}]: radio {} has {} handlers configured, but {} channels are requested",
                            i, radioSetting.radioId.c_str(), sk->handlers.size(), radioSetting.nChannels));
                    }
                }
            }
            for(double center : radioSetting.centers_hz) {
                if(center <= 0.0) {
                    res.errors.push_back(std::format("radioSettings[{}]: center must be positive", i));
                }
            }
        }

        for(std::size_t i = 0; i < rid.interferenceSources.size(); ++i) {
            const auto &source = rid.interferenceSources[i];

            if(source.radioId.empty()) {
                res.errors.push_back(std::format("interferenceSources[{}]: radioId must not be empty", i));
            }
            else if(radioIds.count(source.radioId) == 0) {
                res.errors.push_back(std::format("interferenceSources[{}]: unknown radioId: {}", i, source.radioId.c_str()));
            }

            if(source.channelId < 0) {
                res.errors.push_back(std::format("interferenceSources[{}]: negative channel id", i));
            }
            else if(radioIds.count(source.radioId) > 0 && source.channelId >= rid.radioSettings[radioIds[source.radioId]].nChannels) {
                res.errors.push_back(std::format("interferenceSources[{}]: channel id out of range: {}", i, source.channelId));
            }

            if(source.startTime_s < 0.0) {
                res.errors.push_back(std::format("interferenceSources[{}]: startTime_s must be >= 0", i));
            }
            if(source.endTime_s < source.startTime_s) {
                res.errors.push_back(std::format("interferenceSources[{}]: endTime_s must be >= startTime_s", i));
            }
            if(source.center_hz <= 0.0) {
                res.errors.push_back(std::format("interferenceSources[{}]: center_hz must be positive", i));
            }
            if(source.power_dbm > 0.0) {
                res.warnings.push_back(std::format("interferenceSources[{}]: power_dbm is positive; check units", i));
            }

            if(std::holds_alternative<Rid::InterferenceSource::Noise>(source.variant)) {
                const auto &v = std::get<Rid::InterferenceSource::Noise>(source.variant);
                if(v.span_hz <= 0.0) {
                    res.errors.push_back(std::format("interferenceSources[{}]: noise span_hz must be positive", i));
                }
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Chirp>(source.variant)) {
                const auto &v = std::get<Rid::InterferenceSource::Chirp>(source.variant);
                if(v.sweepSpan_hz <= 0.0) {
                    res.errors.push_back(std::format("interferenceSources[{}]: chirp sweepSpan_hz must be positive", i));
                }
                if(v.sweepTime_s <= 0.0) {
                    res.errors.push_back(std::format("interferenceSources[{}]: chirp sweepTime_s must be positive", i));
                }
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Spoofing>(source.variant)) {
                res.errors.push_back(std::format("interferenceSources[{}]: spoofing is not yet supported", i));
            }
        }

        return res;
    }

    ScenarioRidMap MapRidToScenario(const ServerConfig &cfg, const Rid &rid) {
        Scenario scen;
        std::vector<std::int32_t> eventMap;

        scen.duration = rid.globalSettings.duration_s;
        scen.name = rid.metadata.name;

        std::map<std::string, std::int32_t> radioMap;
        for(std::int32_t i = 0; i < std::int32_t(cfg.radios.size()); ++i) {
            radioMap[cfg.radios[i].id] = i;
        }

        for(std::int32_t i = 0; i < std::int32_t(rid.radioSettings.size()); ++i) {
            auto &radioSettings = rid.radioSettings[i];
            auto &radio = cfg.radios[radioMap[radioSettings.radioId]];
            Scenario::Output output;
            output.name = radioSettings.radioId;
            if(std::holds_alternative<ServerConfig::Radio::UsrpB200>(radio.variant)) {
                auto &v = std::get<ServerConfig::Radio::UsrpB200>(radio.variant);
                OutputVariant::RadioUsrpB200 outputV;
                outputV.serial = v.serial;
                outputV.subdevSpec = "A:A";
                outputV.sampleRate = radioSettings.bandwidth_hz;
                outputV.gain = radioSettings.gains_db[0];
                outputV.center = radioSettings.centers_hz[0];
                output.variant.variant = outputV;
            }
            else if(std::holds_alternative<ServerConfig::Radio::Sidekiq>(radio.variant)) {
                auto &v = std::get<ServerConfig::Radio::Sidekiq>(radio.variant);
                OutputVariant::RadioSidekiq outputV;
                outputV.serial = v.serial;
                outputV.nChannels = std::size_t(radioSettings.nChannels);
                outputV.handlers = v.handlers;
                outputV.sampleRate = radioSettings.bandwidth_hz;
                outputV.centers = radioSettings.centers_hz;
                outputV.gains = radioSettings.gains_db;
                output.variant.variant = outputV;
            }
            else {
                Common::Assert(false, "unknown radio type");
            }

            scen.outputs.push_back(output);
        }


        for(std::int32_t i = 0; i < std::int32_t(rid.interferenceSources.size()); ++i) {
            auto &source = rid.interferenceSources[i];
            Scenario::Signal signal;
            signal.name = std::format("s{:03}", i);
            if(std::holds_alternative<Rid::InterferenceSource::Cw>(source.variant)) {
                // auto &v = std::get<Rid::InterferenceSource::Cw>(source.variant);
                SignalVariant::Tone signalV;
                signal.variant.variant = signalV;
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Noise>(source.variant)) {
                auto &v = std::get<Rid::InterferenceSource::Noise>(source.variant);
                SignalVariant::Noise signalV;
                signalV.span = v.span_hz;
                signal.variant.variant = signalV;
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Chirp>(source.variant)) {
                auto &v = std::get<Rid::InterferenceSource::Chirp>(source.variant);
                SignalVariant::Chirp signalV;
                signalV.rate = 1.0 / v.sweepTime_s;
                signalV.span = v.sweepSpan_hz;
                signal.variant.variant = signalV;
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Spoofing>(source.variant)) {
                Common::Assert(false, "unimplemented");
            }
            else {
                Common::Assert(false, "unknown interference source type");
            }

            Scenario::Event event;
            event.signalName = signal.name;
            event.outputName = source.radioId;
            event.frequency = source.center_hz;
            event.iChannel = source.channelId;
            event.start = source.startTime_s;
            event.end = source.endTime_s;
            event.amplitude = std::pow(10.0, source.power_dbm / 10.0); // this is flawed

            scen.signals.push_back(signal);
            scen.events.push_back(event);
            eventMap.push_back(i); // pretty much useless
        }


        return { std::move(scen), std::move(eventMap) };
    }

}

