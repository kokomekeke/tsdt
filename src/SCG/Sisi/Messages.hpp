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

    // one test changed its status. only the change is sent, on test_log_topic
    struct TestStatusMessage {
        enum class TestStatus {
            TEST_INIT,
            TEST_PREPARE,
            TEST_READY,
            TEST_RUNNING,
            TEST_COMPLETED,
            TEST_FAILED
        };
        std::string timestamp; // when the status was published, see FormatIsoTime
        std::string testId;
        TestStatus status;
    };

    // what is on the air right now, on signal_log_topic. sent on a change and again every
    // status_update_period_s
    struct SignalStatusMessage {
        enum class SignalStatus {
            none,
            jamming,
            spoofing // wins over jamming when both are active
        };
        std::string timestamp; // when the status was published, see FormatIsoTime
        SignalStatus status;
    };

    struct ErrorMessage {
        std::string message;
    };

}
