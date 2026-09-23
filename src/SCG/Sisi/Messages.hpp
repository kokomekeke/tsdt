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

    struct StatusMessage {
        enum class TestStatus {
            TEST_INIT,
            TEST_PREPARE,
            TEST_READY,
            TEST_RUNNING,
            TEST_COMPLETED,
            TEST_FAILED
        };
        struct TestInfo {
            std::string testId;
            TestStatus status;
        };
        struct Details {
            // anything we might need later
        };
        std::string timestamp; // when the status was published, see FormatIsoTime
        std::vector<TestInfo> tests;
        Details details;
    };

    struct ErrorMessage {
        std::string message;
    };

}
