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
                // one transmit port per channel, the sidekiq api names them A1, A2, B1, B2
                std::vector<std::string> handlers;
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
            std::string signalLogTopic;
            std::string testLogTopic;
            std::string errorLogTopic;
        };

        std::string recordingDir;
        // rinex navigation file
        std::string gnssEphemerisFile;
        bool mockRadios; // every radio is replaced with a device that only paces the pipeline
        // the spoofer recordings are silence of the right length and rate, made without the gnss
        // engine and without an ephemeris
        bool mockGnss;
        // a spoofer is recorded into a file while the test is prepared and played from it.
        // when false the gnss engine generates it while the test plays
        bool prerecordGnss;
        bool logMqtt;
        double statusUpdatePeriod_s;
        double startAhead_s; // pipeline starts before the specified time, but the output is stalled until the precise moment
        double scenarioFetchTimeout_s; // the whole exchange with the machine the scenario is read from, not each step of it
        std::vector<Radio> radios;
        Mqtt mqtt;
    };

}


