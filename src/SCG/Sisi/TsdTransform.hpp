#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Common/Logger.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"
#include "SCG/Scenario/ScenarioPipeline.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Utility/Job.hpp"
#include "CommandError.hpp"
#include "Rid.hpp"
#include "ServerConfig.hpp"
#include "Tsd.hpp"

namespace Sagax::SCG {

    ValidationResult ValidateConfig(const ServerConfig &cfg);
    ValidationResult ValidateRid(const ServerConfig &cfg, const Rid &rid);

    // parallel to tsd.steps
    using StepRids = std::vector<std::optional<Rid>>;

    // the documents a tsd is made of, kept as they were read. ridJsons runs parallel to
    // the steps and holds an empty string where a step named no rid
    struct TsdSources {
        Tsd tsd;
        std::string tsdJson;
        StepRids rids;
        std::vector<std::string> ridJsons;
    };

    // reads the tsd at the url and then the rid of every step, and parses each one. a url
    // named by several steps is read once and the later steps take a copy. the timeout of
    // the config applies to each read on its own
    CommandError FetchTsd(const Common::Logger::Handle &lh, const ServerConfig &cfg, const std::string &url, TsdSources &out);

    // validates the rid of every step on its own, an error names its step
    CommandError ValidateTsdSources(const Common::Logger::Handle &lh, const ServerConfig &cfg, const TsdSources &sources);

    // the tsd cut into parts, each one played by a pipeline of its own. offsets and
    // durations are in seconds from the start of the tsd
    struct MergedTsd {
        struct Part {
            struct Step {
                std::int32_t iStep;
                // the start of the step from the offset of the part
                double shift_s;
            };

            double offset_s;
            // reaches the end of the last rid
            double duration_s;
            // the steps that name a rid
            std::vector<Step> steps;
        };

        std::vector<Part> parts;
        // the sum of the durations of the steps
        double duration_s;
    };

    // lays the steps end to end
    // a step is at least as long as the rid it plays
    // the gap between two rids runs from the end of the first rid to the start of the step of
    // the second one
    // a rid joins the part before it when the gap is shorter than startAhead_s, and starts a new
    // part otherwise
    // the rids of a part configure the radios the same way, down to the order of the array
    // the versions match across the whole tsd
    CommandError MergeTsd(const Tsd &tsd, const StepRids &rids, double startAhead_s, MergedTsd &out);

    // a tsd ready to play, the parts in the order they play
    struct TsdScenario {
        // the source of a step an event of a layout plays
        struct EventSource {
            std::int32_t iStep;
            std::int32_t iSource;
        };

        struct GainUpdate {
            std::chrono::system_clock::time_point time;
            std::string radioId;
            std::int32_t channelId;
            double gain_db;
        };

        struct Part {
            // the system time of the first sample
            std::chrono::system_clock::time_point time;
            double duration_s;
            std::vector<std::int32_t> steps;
            // still needs Setup
            ScenarioPipeline pipeline;
            // parallel to the events of the layout of the pipeline
            std::vector<EventSource> eventSources;
            // the ramps of the rids as the single gains to set, ordered by time. a step that
            // follows one with gain updates in the part first sets the channels those updates
            // changed back to their radio_settings gain
            std::vector<GainUpdate> gainUpdates;
        };

        StepRids rids;
        std::vector<Part> parts;
        // they write the gnss files the pipelines read, they have to finish before the first part plays
        std::vector<std::unique_ptr<Job>> jobs;
        double duration_s;
    };

    // merges the steps with MergeTsd and builds the pipeline of every part. the tsd starts at
    // startTime, the jobs write their files into directory, named by the step and the source.
    // the radio settings of the first rid of a part configure the radios of its pipeline
    CommandError TransformTsd(
        const PipelineConfig &pipelineConfig,
        const ServerConfig &cfg,
        const Tsd &tsd,
        const StepRids &rids,
        std::chrono::system_clock::time_point startTime,
        const std::string &directory,
        TsdScenario &out
    );

}
