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
            std::string scenarioUrl;
            std::string startTime;
            std::string testId;
        };

        struct TestCancel {
            std::string testId;
        };

        // it carries nothing, it ends whatever is being transmitted
        struct IntStop {
        };

        // part of the protocol and read like the rest, but nothing is done with them
        // yet. none of the three carries any field
        struct IntStart {
        };

        struct TestStart {
        };

        struct TestUpdate {
        };

        using Variant = std::variant<
            TestSetup,
            TestCancel,
            IntStop,
            IntStart,
            TestStart,
            TestUpdate
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
        SignalStatus type;
    };

    struct ErrorMessage {
        std::string message;
    };

}
