#include <variant>

namespace Sagax::SCG {
    struct MqttMessage {
        struct TestStart {
        };

        struct TestStop {
        };

        struct IntStart {
        };

        struct IntStop {
        };

        using Variant = std::variant<
            TestStart,
            TestStop,
            IntStart,
            IntStop
        >;

        Variant variant;
    };

}

