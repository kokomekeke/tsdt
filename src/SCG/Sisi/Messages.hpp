#pragma once
#include <optional>
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

        // ends whatever is being transmitted
        struct IntStop {
        };

        // part of the protocol and read like the rest, but nothing is done with them
        // yet. none of them carries any field
        struct IntStart {
        };

        struct TestStart {
        };

        struct TestUpdate {
        };

        struct TestEnd {
        };

        using Variant = std::variant<
            TestSetup,
            TestCancel,
            IntStop,
            IntStart,
            TestStart,
            TestUpdate,
            TestEnd
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
        TestStatus testStatus;
    };

    struct SignalStatusMessage {
        enum class SignalStatus {
            none,
            jamming,
            spoofing
        };
        struct Description {}; // IRD allows for any content, an array of active signals for example
        std::string timestamp; // iso 8601, utc
        SignalStatus type;
        Description description;
    };

    // TN5-IRD-LOG, an event that is stored with the session
    struct SessionLogMessage {
        enum class Subsystem {
            vdl,
            gt,
            rsg,
            tms,
            dc
        };
        enum class Severity {
            error,
            warning,
            info,
            debug,
            trace
        };
        std::string eventId; // uuid v4, reused when the same event is sent again
        std::string timestamp; // iso 8601, utc
        std::optional<std::string> sessionId; // equal to the test id, omitted when unknown
        Subsystem subsystem;
        Severity severity;
        std::string message;
    };

}
