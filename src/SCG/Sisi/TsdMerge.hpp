#pragma once
#include <optional>
#include <string>
#include <vector>
#include "CommandError.hpp"
#include "Rid.hpp"
#include "ServerConfig.hpp"
#include "Tsd.hpp"

namespace Sagax::SCG {

    // the rid every step named, in the order of tsd.steps. a step with no interference
    // holds nothing
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
    CommandError FetchTsd(const ServerConfig &cfg, const std::string &url, TsdSources &out);

    // runs the rid rules on every step on its own, which is what names the step an error
    // belongs to. the merged rid is checked again by whoever builds it
    CommandError ValidateTsdSources(const ServerConfig &cfg, const TsdSources &sources);

    // builds one rid out of the steps. the sources of a step move by the sum of the
    // durations of the steps before it, and the duration of the result is the sum of all
    // of them. a step has to be at least as long as the rid it plays
    //
    // every rid has to configure the radios the same way, down to the order of the array,
    // because a radio is tuned once for the whole playback. the versions have to match
    // too. the name and the description come from the tsd, the rest of the metadata from
    // the first rid that has one
    CommandError MergeTsd(const Tsd &tsd, const StepRids &rids, Rid &out);

}
