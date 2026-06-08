#include <format>
#include <map>
#include <cmath>
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

            if(!std::holds_alternative<ServerConfig::Radio::UsrpB200>(radio.variant)) {
                res.errors.push_back(std::format("radios[{}]: unknown radio type", i));
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

            if(radioSetting.bandwidth_hz <= 0.0) {
                res.errors.push_back(std::format("radioSettings[{}]: bandwidth_hz must be positive", i));
            }
            if(radioSetting.center_hz <= 0.0) {
                res.errors.push_back(std::format("radioSettings[{}]: center_hz must be positive", i));
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
                res.errors.push_back(std::format("interferenceSources[{}]: spoofing is not supported", i));
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
                outputV.gain = radioSettings.gain_db;
                outputV.center = radioSettings.center_hz;
                outputV.sampleRate = radioSettings.bandwidth_hz;
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
            event.iChannel = 0;
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

