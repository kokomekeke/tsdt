#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Common/Logger.hpp"
#include "CommandError.hpp"
#include "Rid.hpp"
#include "ServerConfig.hpp"
#include "Tsd.hpp"

namespace Sagax::SCG {

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
            double offset_s;
            std::vector<std::size_t> steps;
            // the sources start from the offset, the duration reaches the end of the last rid
            Rid rid;
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
    // the name and the description of a part come from the tsd, the rest of the metadata from
    // its first rid
    CommandError MergeTsd(const Tsd &tsd, const StepRids &rids, double startAhead_s, MergedTsd &out);

}
