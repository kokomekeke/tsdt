#pragma once
#include <variant>
#include <string>
#include <cstdint>
#include <vector>
#include "Rid.hpp"

namespace Sagax::SCG {
    struct CommandMessage {
        struct TestSetup {
            std::string scenario;
            std::string time;
            std::string testId;
        };

        struct TestCancel {
            std::string testId;
        };

        using Variant = std::variant<
            TestSetup,
            TestCancel
        >;

        Variant variant;
    };

    struct StatusMessage {
        std::vector<Rid::InterferenceSource> active;
    };

    struct ErrorMessage {
        std::string message;
    };

}
