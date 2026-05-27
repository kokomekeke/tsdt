#include <format>
#include <map>
#include <cmath>
#include "Common/Assert.hpp"
#include "RidTransform.hpp"

namespace Sagax::SCG {

    ValidationResult ValidateConfig(const ServerConfig &cfg) {
        return {}; // TODO
    }

    ValidationResult ValidateRid(const ServerConfig &cfg, const Rid &rid) {
        return {}; // TODO
    }

    ScenarioRidMap MapRidToScenario(const ServerConfig &cfg, const Rid &rid) {
        Scenario scen;
        std::vector<std::int32_t> eventMap;

        scen.duration = rid.globalSettings.duration_s;
        scen.name = rid.metadata.name;

        std::map<std::string, std::int32_t> radioMap;
        for(std::int32_t i = 0; i < cfg.radios.size(); ++i) {
            radioMap[cfg.radios[i].id] = i;
        }

        for(std::int32_t i = 0; i < rid.radioSettings.size(); ++i) {
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


        for(std::int32_t i = 0; i < rid.interferenceSources.size(); ++i) {
            auto &source = rid.interferenceSources[i];
            Scenario::Signal signal;
            signal.name = std::format("s{:03}", i);
            if(std::holds_alternative<Rid::InterferenceSource::Cw>(source.variant)) {
                auto &v = std::get<Rid::InterferenceSource::Cw>(source.variant);
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

