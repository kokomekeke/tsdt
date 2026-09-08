#pragma once
#include <variant>
#include <string>
#include <cstdint>
#include <vector>

namespace Sagax::SCG {

    struct ServerConfig {
        struct Radio {
            struct UsrpB200 {
                std::string serial;
            };

            struct Sidekiq {
                std::string serial;
            };

            using Variant = std::variant<
                UsrpB200,
                Sidekiq
            >;

            std::string id;
            Variant variant;
        };

        struct Mqtt {
            std::string brokerHost;
            std::int32_t brokerPort;
            std::string commandTopic;
            std::string logTopic;
            std::string errorTopic;
        };

        std::string recordingDir;
        bool mockRadios; // every radio is replaced with a device that only paces the pipeline
        double statusUpdatePeriod_s;
        double startAhead_s; // pipeline starts before the specified time, but the output is stalled until the precise moment
        std::vector<Radio> radios;
        Mqtt mqtt;
    };

}


