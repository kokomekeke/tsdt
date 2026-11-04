#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "Common/Assert.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/Tsd.hpp"
#include "SCG/Output/OutputTransform.hpp"
#include "SCG/Signal/SignalTransform.hpp"
#include "SCG/Utility/Fetch.hpp"
#include "GnssRecording.hpp"
#include "TsdTransform.hpp"

namespace Sagax::SCG {

    namespace {

        std::chrono::system_clock::duration Seconds(double seconds) {
            return std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(seconds)
            );
        }

        std::string GnssFileName(const std::string &directory, std::int32_t iStep, std::int32_t iSource) {
            return (std::filesystem::path(directory) / std::format("gnss_{:03}_{:04}.wav", iStep, iSource)).string();
        }

        // startTime is the time the source starts at
        SignalVariant::GnssSpoofing GetSpoofing(
            const ServerConfig &cfg,
            const Rid::InterferenceSource &source,
            std::chrono::system_clock::time_point startTime
        ) {
            auto &v = std::get<Rid::InterferenceSource::Spoofing>(source.variant);
            auto gnssSignal = GnssSignalAt(v.constellation, source.center_hz);
            Common::Assert(gnssSignal.has_value(), "ValidateRid lets no unknown gnss center through");

            SignalVariant::GnssSpoofing spoofing;
            spoofing.signal = gnssSignal.value();
            spoofing.startTime = startTime;
            spoofing.ephemerisFile = cfg.gnssEphemerisFile;
            for(const auto &w : v.trajectory) {
                spoofing.trajectory.push_back({ w.t, w.latitude, w.longitude, w.altitude });
            }
            return spoofing;
        }

        // the arrays match element by element, the order included. the numbers are compared
        // exactly, they are copied from the documents and never computed
        CommandError CompareRadioSettings(
            const std::vector<Rid::RadioSettings> &a,
            const std::vector<Rid::RadioSettings> &b
        ) {
            if(a.size() != b.size()) {
                return std::format("it configures {} radios instead of {}", b.size(), a.size());
            }
            for(std::size_t i = 0; i < a.size(); ++i) {
                if(a[i].radioId != b[i].radioId) {
                    return std::format(
                        "radioSettings[{}] is radio \"{}\" instead of \"{}\"",
                        i, b[i].radioId, a[i].radioId
                    );
                }
                if(a[i].nChannels != b[i].nChannels) {
                    return std::format(
                        "radioSettings[{}]: {} channels instead of {}",
                        i, b[i].nChannels, a[i].nChannels
                    );
                }
                if(a[i].bandwidth_hz != b[i].bandwidth_hz) {
                    return std::format(
                        "radioSettings[{}]: bandwidth_hz is {} instead of {}",
                        i, b[i].bandwidth_hz, a[i].bandwidth_hz
                    );
                }
                if(a[i].centers_hz != b[i].centers_hz) {
                    return std::format("radioSettings[{}]: the center frequencies differ", i);
                }
                if(a[i].gains_db != b[i].gains_db) {
                    return std::format("radioSettings[{}]: the gains differ", i);
                }
            }
            return {};
        }

        CommandError ReadTsd(const std::string &json, Tsd &out) {
            Json::JsonIssues issues;
            if(!ParseTsd(json, out, issues)) {
                return std::format("failed to parse TSD:\n{}", issues.Format());
            }
            return {};
        }

        CommandError ReadRid(const std::string &json, Rid &out) {
            Json::JsonIssues issues;
            if(!ParseRid(json, out, issues)) {
                return std::format("failed to parse RID:\n{}", issues.Format());
            }
            return {};
        }

    }

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
                // the sidekiq device of ssg only configures two channels
                if(sk->handlers.size() != 2) {
                    res.errors.push_back(std::format("radios[{}]: a sidekiq needs exactly 2 handlers, it has {}", i, sk->handlers.size()));
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
            // has to ask for exactly as many channels as that radio has handlers. a usrp b200
            // is driven on one channel
            if(radioIds.count(radioSetting.radioId) > 0) {
                const auto &radio = cfg.radios[radioIds[radioSetting.radioId]];
                if(auto *sk = std::get_if<ServerConfig::Radio::Sidekiq>(&radio.variant)) {
                    if(std::size_t(radioSetting.nChannels) != sk->handlers.size()) {
                        res.errors.push_back(std::format(
                            "radioSettings[{}]: radio {} has {} handlers configured, but {} channels are requested",
                            i, radioSetting.radioId.c_str(), sk->handlers.size(), radioSetting.nChannels));
                    }
                }
                else if(std::holds_alternative<ServerConfig::Radio::UsrpB200>(radio.variant)) {
                    if(radioSetting.nChannels != 1) {
                        res.errors.push_back(std::format(
                            "radioSettings[{}]: radio {} is a usrp b200 with 1 channel, but {} channels are requested",
                            i, radioSetting.radioId.c_str(), radioSetting.nChannels));
                    }
                }
            }
            for(double center : radioSetting.centers_hz) {
                if(center <= 0.0) {
                    res.errors.push_back(std::format("radioSettings[{}]: center must be positive", i));
                }
            }
        }

        std::set<std::pair<std::string, std::int32_t>> gainUpdateChannels;
        for(std::size_t i = 0; i < rid.gainUpdates.size(); ++i) {
            const auto &update = rid.gainUpdates[i];

            if(usedRadioSettings.count(update.radioId) == 0) {
                res.errors.push_back(std::format("gainUpdates[{}]: radio {} has no radio_settings entry", i, update.radioId.c_str()));
            }
            else if(update.channelId < 0 || update.channelId >= rid.radioSettings[usedRadioSettings[update.radioId]].nChannels) {
                res.errors.push_back(std::format("gainUpdates[{}]: channel id out of range: {}", i, update.channelId));
            }

            if(gainUpdateChannels.count({ update.radioId, update.channelId })) {
                res.errors.push_back(std::format(
                    "gainUpdates[{}]: radio {} channel {} already has gain updates", i, update.radioId.c_str(), update.channelId));
            }
            else {
                gainUpdateChannels.insert({ update.radioId, update.channelId });
            }

            if(update.stepInterval_s <= 0.0) {
                res.errors.push_back(std::format("gainUpdates[{}]: step_interval_s must be positive", i));
            }

            for(std::size_t iv = 0; iv < update.values.size(); ++iv) {
                const auto &v = update.values[iv];
                if(v.t < 0.0 || v.t > rid.globalSettings.duration_s) {
                    res.errors.push_back(std::format(
                        "gainUpdates[{}]: values[{}]: t {} is outside of [0, globalSettings.duration_s {}]",
                        i, iv, v.t, rid.globalSettings.duration_s));
                }
                if(iv > 0 && v.t <= update.values[iv - 1].t) {
                    res.errors.push_back(std::format("gainUpdates[{}]: values[{}]: time does not advance", i, iv));
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

            // a source may only name a radio this rid gives settings to, the other radios of the
            // config have no channel count, bandwidth or center
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
            // radioIds indexes the radios of the config, not rid.radioSettings
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
                        "interferenceSources[{}]: mock_gnss is set, the spoofer is replaced by silence", i));
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

                // the engine selects satellites by signal, system and elevation mask, not by a list
                if(!v.prnList.empty()) {
                    res.errors.push_back(std::format(
                        "interferenceSources[{}]: prn_list has to be empty, the gnss engine has no "
                        "satellite selection and generates every one in view",
                        i));
                }

                if(!GnssSignalAt(v.constellation, source.center_hz).has_value()) {
                    res.errors.push_back(std::format(
                        "interferenceSources[{}]: no gnss signal is known at center_frequency_hz {}, {}",
                        i, source.center_hz, GnssSupportedCenters(v.constellation)));
                }

#ifndef HAVE_SSP
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
                else if(v.trajectory.front().t != 0.0) {
                    res.errors.push_back(std::format("interferenceSources[{}]: the first waypoint has to be at t = 0", i));
                }
                for(std::size_t iw = 0; iw < v.trajectory.size(); ++iw) {
                    const auto &w = v.trajectory[iw];
                    if(w.latitude < -90.0 || w.latitude > 90.0) {
                        res.errors.push_back(std::format("interferenceSources[{}]: trajectory[{}]: latitude out of range", i, iw));
                    }
                    if(w.longitude < -180.0 || w.longitude > 180.0) {
                        res.errors.push_back(std::format("interferenceSources[{}]: trajectory[{}]: longitude out of range", i, iw));
                    }
                    if(iw > 0 && w.t <= v.trajectory[iw - 1].t) {
                        res.errors.push_back(std::format("interferenceSources[{}]: trajectory[{}]: time does not advance", i, iw));
                    }
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

    CommandError FetchTsd(const Common::Logger::Handle &lh, const ServerConfig &cfg, const std::string &url, TsdSources &out) {
        TsdSources sources;

        SCG_CHECK(Fetch(url, cfg.scenarioFetchTimeout_s, sources.tsdJson));
        SCG_CHECK(ReadTsd(sources.tsdJson, sources.tsd));

        sources.rids.assign(sources.tsd.steps.size(), std::nullopt);
        sources.ridJsons.assign(sources.tsd.steps.size(), std::string());

        std::map<std::string, std::size_t> read;
        for(std::size_t i = 0; i < sources.tsd.steps.size(); ++i) {
            const auto &step = sources.tsd.steps[i];
            if(!step.interference.has_value()) {
                continue;
            }
            const std::string &ridUrl = step.interference.value();

            auto it = read.find(ridUrl);
            if(it != read.end()) {
                sources.rids[i] = sources.rids[it->second];
                sources.ridJsons[i] = sources.ridJsons[it->second];
                continue;
            }

            Common::Logger::Info(lh, "reading the RID of step {}: {}", i, ridUrl);
            std::string json;
            CommandError error = Fetch(ridUrl, cfg.scenarioFetchTimeout_s, json);
            if(error.empty()) {
                error = ReadRid(json, sources.rids[i].emplace());
            }
            if(!error.empty()) {
                return std::format("step[{}] \"{}\": {}", i, ridUrl, error);
            }

            sources.ridJsons[i] = std::move(json);
            read[ridUrl] = i;
        }

        out = std::move(sources);
        return {};
    }

    CommandError ValidateTsdSources(const Common::Logger::Handle &lh, const ServerConfig &cfg, const TsdSources &sources) {
        Common::Assert(
            sources.tsd.steps.size() == sources.rids.size(),
            "a rid is missing for a step of the tsd"
        );

        for(std::size_t i = 0; i < sources.rids.size(); ++i) {
            if(!sources.rids[i].has_value()) {
                continue;
            }
            Common::Logger::Info(lh, "validating the RID of step {}..", i);
            CommandError error = CheckValidity(lh, ValidateRid(cfg, sources.rids[i].value()), "RID");
            if(!error.empty()) {
                return std::format(
                    "step[{}] \"{}\": {}",
                    i, sources.tsd.steps[i].interference.value_or(""), error
                );
            }
        }
        return {};
    }

    CommandError MergeTsd(const Tsd &tsd, const StepRids &rids, double startAhead_s, MergedTsd &out) {
        Common::Assert(tsd.steps.size() == rids.size(), "a rid is missing for a step of the tsd");

        if(tsd.name.empty()) {
            return "the TSD has no name";
        }
        if(tsd.steps.empty()) {
            return "the TSD has no steps";
        }

        MergedTsd merged;
        const Rid *first = nullptr;
        std::size_t firstStep = 0;
        std::size_t previousStep = 0;
        double previousEnd_s = 0.0;
        double offset_s = 0.0;

        for(std::size_t i = 0; i < tsd.steps.size(); ++i) {
            const auto &step = tsd.steps[i];
            if(step.duration_s <= 0.0) {
                return std::format("step[{}]: duration_seconds must be positive", i);
            }

            if(!rids[i].has_value()) {
                offset_s += step.duration_s;
                continue;
            }
            const Rid &rid = rids[i].value();

            if(step.duration_s < rid.globalSettings.duration_s) {
                return std::format(
                    "step[{}]: the step is {} s long and the RID it plays is {} s long",
                    i, step.duration_s, rid.globalSettings.duration_s
                );
            }

            if(first == nullptr) {
                first = &rid;
                firstStep = i;
            }
            else if(rid.metadata.version != first->metadata.version) {
                return std::format(
                    "step[{}] is version \"{}\" and step[{}] is version \"{}\"",
                    i, rid.metadata.version, firstStep, first->metadata.version
                );
            }

            double gap_s = offset_s - previousEnd_s;
            if(merged.parts.empty() || gap_s >= startAhead_s) {
                merged.parts.push_back(MergedTsd::Part{ offset_s, 0.0, {} });
            }
            else {
                const Rid &partRid = rids[std::size_t(merged.parts.back().steps.front().iStep)].value();
                CommandError difference = CompareRadioSettings(partRid.radioSettings, rid.radioSettings);
                if(!difference.empty()) {
                    return std::format(
                        "step[{}] does not configure the radios the way step[{}] does, and the gap "
                        "between them is {} s, shorter than start_ahead_s {} s: {}",
                        i, previousStep, gap_s, startAhead_s, difference
                    );
                }
            }

            MergedTsd::Part &part = merged.parts.back();
            double shift_s = offset_s - part.offset_s;
            part.steps.push_back(MergedTsd::Part::Step{ std::int32_t(i), shift_s });
            part.duration_s = shift_s + rid.globalSettings.duration_s;

            previousStep = i;
            previousEnd_s = offset_s + rid.globalSettings.duration_s;
            offset_s += step.duration_s;
        }

        if(first == nullptr) {
            return "no step of the TSD names an interference";
        }

        merged.duration_s = offset_s;

        out = std::move(merged);
        return {};
    }

    CommandError TransformTsd(
        const PipelineConfig &pipelineConfig,
        const ServerConfig &cfg,
        const Tsd &tsd,
        const StepRids &rids,
        std::chrono::system_clock::time_point startTime,
        const std::string &directory,
        TsdScenario &out
    ) {
        MergedTsd merged;
        SCG_CHECK(MergeTsd(tsd, rids, cfg.startAhead_s, merged));

        std::map<std::string, std::int32_t> radioMap;
        for(std::int32_t i = 0; i < std::int32_t(cfg.radios.size()); ++i) {
            radioMap[cfg.radios[i].id] = i;
        }

        TsdScenario scenario;
        scenario.rids = rids;
        scenario.duration_s = merged.duration_s;

        for(auto &mergedPart : merged.parts) {
            TsdScenario::Part part;
            part.time = startTime + Seconds(mergedPart.offset_s);
            part.duration_s = mergedPart.duration_s;

            ScenarioLayout layout;
            layout.mode = ScenarioLayout::Mode::stream;
            std::vector<Resource*> generatorResources;
            std::vector<Resource*> deviceResources;

            // radio id -> index of the output
            std::map<std::string, std::size_t> outputMap;
            const Rid &first = rids[std::size_t(mergedPart.steps.front().iStep)].value();
            for(auto &radioSettings : first.radioSettings) {
                auto &radio = cfg.radios[radioMap[radioSettings.radioId]];
                OutputVariant outputV;
                if(auto *usrpCfg = std::get_if<ServerConfig::Radio::UsrpB200>(&radio.variant)) {
                    OutputVariant::RadioUsrpB200 usrp;
                    usrp.serial = usrpCfg->serial;
                    usrp.subdevSpec = "A:A";
                    usrp.sampleRate = radioSettings.bandwidth_hz;
                    usrp.gain = radioSettings.gains_db[0];
                    usrp.center = radioSettings.centers_hz[0];
                    outputV.variant = usrp;
                }
                else if(auto *sidekiqCfg = std::get_if<ServerConfig::Radio::Sidekiq>(&radio.variant)) {
                    OutputVariant::RadioSidekiq sidekiq;
                    sidekiq.serial = sidekiqCfg->serial;
                    sidekiq.nChannels = std::size_t(radioSettings.nChannels);
                    sidekiq.handlers = sidekiqCfg->handlers;
                    sidekiq.sampleRate = radioSettings.bandwidth_hz;
                    sidekiq.centers = radioSettings.centers_hz;
                    sidekiq.gains = radioSettings.gains_db;
                    outputV.variant = sidekiq;
                }
                else {
                    Common::Assert(false, "unknown radio type");
                }

                auto params = outputV.GetCommonParams();
                ScenarioLayout::Output output;
                output.sampleRate = params.sampleRate;
                output.nChannels = params.nChannels;
                output.centers = params.centers;
                output.nSamples = std::max(std::int64_t(0), std::int64_t(part.duration_s * params.sampleRate));
                auto [dev, rsc] = GetOutputDevice(pipelineConfig, outputV);
                output.device = std::move(dev);
                deviceResources.insert(deviceResources.end(), rsc.begin(), rsc.end());

                outputMap[radioSettings.radioId] = layout.outputs.size();
                layout.outputs.push_back(std::move(output));
            }

            std::vector<std::pair<TsdScenario::EventSource, ScenarioLayout::Event>> events;
            // the channels the gain updates of the previous step left away from radio_settings
            std::set<std::pair<std::string, std::int32_t>> changedChannels;
            for(auto &step : mergedPart.steps) {
                part.steps.push_back(step.iStep);
                const Rid &rid = rids[std::size_t(step.iStep)].value();

                for(auto &[radioId, channelId] : changedChannels) {
                    auto it = std::find_if(rid.radioSettings.begin(), rid.radioSettings.end(), [&](const Rid::RadioSettings &r) {
                        return r.radioId == radioId;
                    });
                    part.gainUpdates.push_back(TsdScenario::GainUpdate{
                        part.time + Seconds(step.shift_s),
                        radioId,
                        channelId,
                        it->gains_db[std::size_t(channelId)]
                    });
                }
                changedChannels.clear();

                // a ramp from value v to value w is set every stepInterval_s from v on. the
                // count of steps is rounded down by a margin, so a step that lands on w only by
                // rounding is left to w
                for(auto &update : rid.gainUpdates) {
                    auto add = [&](double t, double gain_db) {
                        part.gainUpdates.push_back(TsdScenario::GainUpdate{
                            part.time + Seconds(step.shift_s + t),
                            update.radioId,
                            update.channelId,
                            gain_db
                        });
                    };
                    for(std::size_t iv = 0; iv < update.values.size(); ++iv) {
                        const auto &v = update.values[iv];
                        add(v.t, v.gain_db);
                        if(iv + 1 == update.values.size()) {
                            break;
                        }
                        const auto &w = update.values[iv + 1];
                        double span_s = w.t - v.t;
                        std::int64_t nSteps = std::int64_t(std::ceil(span_s / update.stepInterval_s - 1e-9));
                        for(std::int64_t j = 1; j < nSteps; ++j) {
                            double dt_s = double(j) * update.stepInterval_s;
                            add(v.t + dt_s, v.gain_db + (w.gain_db - v.gain_db) * dt_s / span_s);
                        }
                    }
                    changedChannels.insert({ update.radioId, update.channelId });
                }

                for(std::int32_t is = 0; is < std::int32_t(rid.interferenceSources.size()); ++is) {
                    auto &source = rid.interferenceSources[is];
                    std::size_t iOutput = outputMap.at(source.radioId);
                    auto &output = layout.outputs[iOutput];
                    Common::Assert(std::size_t(source.channelId) < output.nChannels, "channel index out of range");

                    double start_s = step.shift_s + source.startTime_s;
                    double end_s = step.shift_s + source.endTime_s;

                    SignalVariant signalV;
                    if(std::holds_alternative<Rid::InterferenceSource::Cw>(source.variant)) {
                        signalV.variant = SignalVariant::Tone{};
                    }
                    else if(auto *noiseSource = std::get_if<Rid::InterferenceSource::Noise>(&source.variant)) {
                        SignalVariant::Noise noise;
                        noise.span = noiseSource->span_hz;
                        signalV.variant = noise;
                    }
                    else if(auto *chirpSource = std::get_if<Rid::InterferenceSource::Chirp>(&source.variant)) {
                        SignalVariant::Chirp chirp;
                        chirp.rate = 1.0 / chirpSource->sweepTime_s;
                        chirp.span = chirpSource->sweepSpan_hz;
                        signalV.variant = chirp;
                    }
                    else if(std::holds_alternative<Rid::InterferenceSource::Spoofing>(source.variant)) {
                        SignalVariant::GnssSpoofing spoofing = GetSpoofing(cfg, source, part.time + Seconds(start_s));
                        if(cfg.prerecordGnss) {
                            auto job = std::make_unique<GnssRecordingJob>();
                            job->spoofing = spoofing;
                            job->sampleRate_hz = output.sampleRate;
                            job->duration_s = source.endTime_s - source.startTime_s;
                            job->prepareRate = cfg.gnssPrepareRate;
                            job->mock = cfg.mockGnss;
                            job->fileName = GnssFileName(directory, step.iStep, is);

                            SignalVariant::IQFile file;
                            file.fileName = job->fileName;
                            file.loop = false;
                            signalV.variant = file;

                            scenario.jobs.push_back(std::move(job));
                        }
                        else if(cfg.mockGnss) {
                            signalV.variant = SignalVariant::Zero{};
                        }
                        else {
                            signalV.variant = spoofing;
                        }
                    }
                    else {
                        Common::Assert(false, "unknown interference source type");
                    }

                    std::int64_t iStart = std::clamp(std::int64_t(start_s * output.sampleRate), std::int64_t(0), output.nSamples);
                    std::int64_t iEnd = std::clamp(std::int64_t(end_s * output.sampleRate), std::int64_t(0), output.nSamples);
                    if(iStart >= iEnd) {
                        continue;
                    }

                    auto [gen, rsc] = GetSignalGenerator(pipelineConfig, signalV, source.endTime_s - source.startTime_s);
                    gen->SetSampleRate(output.sampleRate);
                    generatorResources.insert(generatorResources.end(), rsc.begin(), rsc.end());

                    events.push_back({ TsdScenario::EventSource{ step.iStep, is }, ScenarioLayout::Event{
                        iOutput,
                        std::size_t(source.channelId),
                        iStart,
                        iEnd,
                        source.center_hz,
                        std::pow(10.0, source.power_dbm / 10.0), // this is flawed
                        std::move(gen)
                    } });
                }
            }
            std::stable_sort(events.begin(), events.end(), [](const auto &e1, const auto &e2) {
                return e1.second.iStart < e2.second.iStart;
            });
            for(auto &[eventSource, e] : events) {
                part.eventSources.push_back(eventSource);
                layout.events.push_back(std::move(e));
            }
            std::stable_sort(part.gainUpdates.begin(), part.gainUpdates.end(), [](const auto &u1, const auto &u2) {
                return u1.time < u2.time;
            });
            for(std::size_t ie = 0; ie < layout.events.size(); ++ie) {
                layout.outputs[layout.events[ie].iOutput].events.push_back(ie);
            }
            AddLines(layout);
            layout.callbacks = pipelineConfig.events;
            layout.callbacks.startTime = part.time;

            part.pipeline.resources = std::move(generatorResources);
            part.pipeline.resources.insert(part.pipeline.resources.end(), deviceResources.begin(), deviceResources.end());
            part.pipeline.SetLayout(std::move(layout));
            scenario.parts.push_back(std::move(part));
        }

        out = std::move(scenario);
        return {};
    }

}
