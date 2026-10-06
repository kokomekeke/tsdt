#pragma once
#include <optional>
#include <string>
#include <vector>

namespace Sagax::SCG {

    // the steps play one after the other, each one for its own length. a step names the
    // rid it plays by url, a step with nothing there stays quiet for its length. the json
    // may carry keys that are not here, they are not read
    struct Tsd {

        struct Step {
            double duration_s;
            std::optional<std::string> interference;
        };

        std::string name;
        std::string description;
        std::vector<Step> steps;
    };

}
