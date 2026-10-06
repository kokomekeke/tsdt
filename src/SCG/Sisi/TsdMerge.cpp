#include <format>
#include <map>
#include <utility>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/Tsd.hpp"
#include "SCG/Utility/Fetch.hpp"
#include "RidTransform.hpp"
#include "TsdMerge.hpp"

namespace Sagax::SCG {

    namespace {

        // the whole array has to match, element by element, so a difference in the order
        // is one as well. the numbers are copied out of the documents and never computed,
        // which is why they are compared as they are
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
            try {
                out = ParseTsd(json);
            }
            catch(const std::exception &e) {
                return std::format("failed to parse TSD: {}", e.what());
            }
            return {};
        }

        CommandError ReadRid(const std::string &json, Rid &out) {
            try {
                out = ParseRid(json);
            }
            catch(const std::exception &e) {
                return std::format("failed to parse RID: {}", e.what());
            }
            return {};
        }

    }

    CommandError FetchTsd(const ServerConfig &cfg, const std::string &url, TsdSources &out) {
        TsdSources sources;

        SCG_CHECK(Fetch(url, cfg.scenarioFetchTimeout_s, sources.tsdJson));
        SCG_CHECK(ReadTsd(sources.tsdJson, sources.tsd));

        sources.rids.assign(sources.tsd.steps.size(), std::nullopt);
        sources.ridJsons.assign(sources.tsd.steps.size(), std::string());

        // a url several steps name is read once, the later steps take the copy
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

            Common::Logger::Info("reading the RID of step {}: {}", i, ridUrl);
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

    CommandError ValidateTsdSources(const ServerConfig &cfg, const TsdSources &sources) {
        Common::Assert(
            sources.tsd.steps.size() == sources.rids.size(),
            "a rid is missing for a step of the tsd"
        );

        for(std::size_t i = 0; i < sources.rids.size(); ++i) {
            if(!sources.rids[i].has_value()) {
                continue;
            }
            Common::Logger::Info("validating the RID of step {}..", i);
            CommandError error = CheckValidity(ValidateRid(cfg, sources.rids[i].value()), "RID");
            if(!error.empty()) {
                return std::format(
                    "step[{}] \"{}\": {}",
                    i, sources.tsd.steps[i].interference.value_or(""), error
                );
            }
        }
        return {};
    }

    CommandError MergeTsd(const Tsd &tsd, const StepRids &rids, Rid &out) {
        Common::Assert(tsd.steps.size() == rids.size(), "a rid is missing for a step of the tsd");

        if(tsd.name.empty()) {
            return "the TSD has no name";
        }
        if(tsd.steps.empty()) {
            return "the TSD has no steps";
        }

        Rid merged;
        const Rid *first = nullptr;
        std::size_t firstStep = 0;
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
                merged.radioSettings = rid.radioSettings;
                merged.metadata = rid.metadata;
            }
            else {
                CommandError difference = CompareRadioSettings(first->radioSettings, rid.radioSettings);
                if(!difference.empty()) {
                    return std::format(
                        "step[{}] does not configure the radios the way step[{}] does: {}",
                        i, firstStep, difference
                    );
                }
                if(rid.metadata.version != first->metadata.version) {
                    return std::format(
                        "step[{}] is version \"{}\" and step[{}] is version \"{}\"",
                        i, rid.metadata.version, firstStep, first->metadata.version
                    );
                }
            }

            for(const auto &source : rid.interferenceSources) {
                Rid::InterferenceSource shifted = source;
                shifted.startTime_s += offset_s;
                shifted.endTime_s += offset_s;
                merged.interferenceSources.push_back(std::move(shifted));
            }

            offset_s += step.duration_s;
        }

        if(first == nullptr) {
            return "no step of the TSD names an interference";
        }

        merged.globalSettings.duration_s = offset_s;
        merged.metadata.name = tsd.name;
        merged.metadata.description = tsd.description;

        out = std::move(merged);
        return {};
    }

}
