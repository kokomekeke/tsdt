#pragma once
#include <variant>
#include <string>
#include <cstdint>
#include <vector>

namespace Sagax::SCG {
    struct CommandMessage {
        struct TestStart {
            std::string scenario;
            std::string vehicle;
            std::string customer;
        };

        struct TestEnd {
        };

        struct TestUpdate {
            // this has things but we dont care
        };

        struct IntStart {
            std::string profile;
        };

        struct IntStop {
        };

        using Variant = std::variant<
            TestStart,
            TestEnd,
            TestUpdate,
            IntStart,
            IntStop
        >;

        Variant variant;
    };

    struct LogMessage {
        enum struct Type { spoofing, jamming, none };
        enum struct Status { active, inactive };
        Type type;
        Status status;
    };

}

