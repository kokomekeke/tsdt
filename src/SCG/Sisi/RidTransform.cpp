#include <format>
#include <filesystem>
#include <map>
#include <cmath>
#include <string>
#include <vector>
#include <variant>
#include <algorithm>
#include "Common/Assert.hpp"
#include "GnssRecording.hpp"
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
        if(cfg.mqtt.signalLogTopic.empty()) {
            res.errors.push_back("mqtt.signal_log_topic must not be empty");
        }
        if(cfg.mqtt.testLogTopic.empty()) {
            res.errors.push_back("mqtt.test_log_topic must not be empty");
        }
        if(cfg.mqtt.errorLogTopic.empty()) {
            res.errors.push_back("mqtt.error_log_topic must not be empty");
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

            if(cfg.mockRadios) {
                res.warnings.push_back(std::format(
                    "radioSettings[{}]: mock_radios is set, radio {} transmits nothing", i, radioSetting.radioId.c_str()));
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

            // a source may only name a radio this rid also gave settings to. the config can
            // know about radios this rid leaves alone, and those carry no channel count,
            // no bandwidth and no center for the source to sit in
            if(!source.radioId.empty()
                && radioIds.count(source.radioId) > 0
                && usedRadioSettings.count(source.radioId) == 0
            ) {
                res.errors.push_back(std::format(
                    "interferenceSources[{}]: radio {} has no radio_settings entry", i, source.radioId.c_str()));
            }

            if(source.channelId < 0) {
                res.errors.push_back(std::format("interferenceSources[{}]: negative channel id", i));
            }
            // indexed through usedRadioSettings and not radioIds: the latter counts the
            // radios of the config, which is a different array from rid.radioSettings and
            // only lines up with it when the rid happens to list them all, in order
            else if(usedRadioSettings.count(source.radioId) > 0
                && source.channelId >= rid.radioSettings[usedRadioSettings[source.radioId]].nChannels
            ) {
                res.errors.push_back(std::format("interferenceSources[{}]: channel id out of range: {}", i, source.channelId));
            }

            if(source.startTime_s < 0.0) {
                res.errors.push_back(std::format("interferenceSources[{}]: startTime_s must be >= 0", i));
            }
            if(source.endTime_s < source.startTime_s) {
                res.errors.push_back(std::format("interferenceSources[{}]: endTime_s must be >= startTime_s", i));
            }
            if(source.endTime_s > rid.globalSettings.duration_s) {
                res.errors.push_back(std::format(
                    "interferenceSources[{}]: endTime_s {} is past globalSettings.duration_s {}",
                    i, source.endTime_s, rid.globalSettings.duration_s));
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
                const auto &v = std::get<Rid::InterferenceSource::Spoofing>(source.variant);

                if(cfg.mockGnss) {
                    res.warnings.push_back(std::format(
                        "interferenceSources[{}]: mock_gnss is set, the spoofer is recorded as silence", i));
                }

                if(cfg.gnssEphemerisFile.empty()) {
                    res.errors.push_back(std::format(
                        "interferenceSources[{}]: the source is a gnss spoofer, but no gnss_ephemeris_file is configured", i));
                }

                // the gnss engine needs a sample rate of whole khz
                if(usedRadioSettings.count(source.radioId) > 0) {
                    double bandwidth_hz = rid.radioSettings[usedRadioSettings[source.radioId]].bandwidth_hz;
                    if(bandwidth_hz <= 0.0 || std::fmod(bandwidth_hz, 1000.0) != 0.0) {
                        res.errors.push_back(std::format(
                            "interferenceSources[{}]: a gnss spoofer needs a bandwidth_hz of whole khz, radio {} has {}",
                            i, source.radioId.c_str(), bandwidth_hz));
                    }
                }

                // the engine selects by signal, system and elevation mask, so there is no
                // way to honour a list of satellites once the samples are summed
                if(!v.prnList.empty()) {
                    res.errors.push_back(std::format(
                        "interferenceSources[{}]: prn_list has to be empty, the gnss engine has no "
                        "satellite selection and generates every one in view",
                        i));
                }

#ifdef HAVE_SSP
                if(!GnssCenterIsSupported(v.constellation, source.center_hz)) {
                    res.errors.push_back(std::format(
                        "interferenceSources[{}]: no gnss signal is known at center_frequency_hz {}, {}",
                        i, source.center_hz, GnssSupportedCenters(v.constellation)));
                }
#else
                // the same shape as the rule for a radio this build cannot drive: the test
                // can still be set up, as long as nothing has to be generated for it
                if(!cfg.mockGnss) {
                    res.errors.push_back(std::format(
                        "interferenceSources[{}]: gnss spoofer, but this build has no gnss engine "
                        "and mock_gnss is not set",
                        i));
                }
#endif

                if(v.trajectory.empty()) {
                    res.errors.push_back(std::format("interferenceSources[{}]: gnss spoofer without a trajectory", i));
                }
                for(std::size_t iw = 0; iw < v.trajectory.size(); ++iw) {
                    const auto &w = v.trajectory[iw];
                    if(w.latitude < -90.0 || w.latitude > 90.0) {
                        res.errors.push_back(std::format("interferenceSources[{}]: trajectory[{}]: latitude out of range", i, iw));
                    }
                    if(w.longitude < -180.0 || w.longitude > 180.0) {
                        res.errors.push_back(std::format("interferenceSources[{}]: trajectory[{}]: longitude out of range", i, iw));
                    }
                    if(w.t < 0.0) {
                        res.errors.push_back(std::format("interferenceSources[{}]: trajectory[{}]: time must be >= 0", i, iw));
                    }
                    if(iw > 0 && w.t <= v.trajectory[iw - 1].t) {
                        res.errors.push_back(std::format("interferenceSources[{}]: trajectory[{}]: time does not advance", i, iw));
                    }
                }

                if(!v.trajectory.empty() && v.trajectory.front().t > 0.0) {
                    res.warnings.push_back(std::format(
                        "interferenceSources[{}]: the trajectory starts at {} s, "
                        "the position holds at the first waypoint until then",
                        i, v.trajectory.front().t));
                }
                double duration_s = source.endTime_s - source.startTime_s;
                if(!v.trajectory.empty() && v.trajectory.back().t < duration_s) {
                    res.warnings.push_back(std::format(
                        "interferenceSources[{}]: the trajectory ends at {} s but the source runs for {} s, "
                        "the position holds at the last waypoint",
                        i, v.trajectory.back().t, duration_s));
                }
            }
        }

        return res;
    }

    ScenarioRidMap MapRidToScenario(
        const ServerConfig &cfg,
        const Rid &rid,
        std::chrono::system_clock::time_point startTime,
        const std::string &directory
    ) {
        Scenario scen;
        std::vector<std::int32_t> eventMap;
        std::vector<std::unique_ptr<Job>> jobs;

        scen.duration = rid.globalSettings.duration_s;
        scen.name = rid.metadata.name;

        std::map<std::string, std::int32_t> radioMap;
        for(std::int32_t i = 0; i < std::int32_t(cfg.radios.size()); ++i) {
            radioMap[cfg.radios[i].id] = i;
        }

        std::map<std::string, double> radioRates;
        for(auto &radioSettings : rid.radioSettings) {
            radioRates[radioSettings.radioId] = radioSettings.bandwidth_hz;
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
            signal.name = std::format("s{:04}", i);
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
                auto &v = std::get<Rid::InterferenceSource::Spoofing>(source.variant);
                auto job = std::make_unique<GnssRecordingJob>();
                job->constellation = v.constellation;
                job->prnList = v.prnList;
                job->waypoints = v.trajectory;
                job->startTime = startTime + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::duration<double>(source.startTime_s)
                );

                job->center_hz = source.center_hz;
                job->sampleRate_hz = radioRates[source.radioId];
                job->duration_s = source.endTime_s - source.startTime_s;
                job->ephemerisFile = cfg.gnssEphemerisFile;
                job->mock = cfg.mockGnss;
                job->fileName = (std::filesystem::path(directory) / std::format("gnss_{:04}.wav", i)).string();

                SignalVariant::IQFile signalV;
                signalV.fileName = job->fileName;
                signalV.loop = false;
                signal.variant.variant = signalV;

                jobs.push_back(std::move(job));
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


        return { std::move(scen), std::move(eventMap), std::move(jobs) };
    }

}

