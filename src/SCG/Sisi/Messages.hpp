#pragma once
#include <variant>
#include <string>
#include <cstdint>
#include <utility>
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

    struct TestStatusMessage {
        enum class TestStatus {
            TEST_INIT,
            TEST_PREPARE,
            TEST_READY,
            TEST_RUNNING,
            TEST_COMPLETED,
            TEST_FAILED
        };
        std::string timestamp; // iso 8601, utc
        std::string testId;
        TestStatus status;
    };

    struct SignalStatusMessage {
        enum class SignalStatus {
            none,
            jamming,
            spoofing
        };
        std::string timestamp; // iso 8601, utc
        SignalStatus status;
    };

    struct ErrorMessage {
        std::string message;
    };

}
