#pragma once
#include <variant>
#include <string>
#include <cstdint>
#include <vector>

namespace Sagax::SCG {

    struct Rid {

        struct Metadata {
            std::string version;
            std::string name;
            std::string author;
            std::string created_utc;
            std::string description;
        };

        struct GlobalSettings {
            double duration_s;
        };

        struct RadioSettings {
            std::string radioId;
            std::int32_t nChannels;
            double bandwidth_hz;
            std::vector<double> centers_hz;
            std::vector<double> gains_db;
        };

        struct InterferenceSource {
            struct Cw {
            };
            struct Noise {
                double span_hz;
            };
            struct Chirp {
                double sweepSpan_hz;
                double sweepTime_s;
            };
            struct Spoofing {
                enum struct Constellation { gps, glonass, galileo, beidou };
                struct TrajectoryItem {
                    double t;
                    double longitude;
                    double latitude;
                    double altitude;
                };
                Constellation constellation;
                std::vector<std::int32_t> prnList;
                std::vector<TrajectoryItem> trajectory;
            };

            using Variant = std::variant<
                Cw,
                Noise,
                Chirp,
                Spoofing
            >;

            std::string radioId;
            std::int32_t channelId;
            double center_hz;
            double power_dbm;
            double startTime_s;
            double endTime_s;
            Variant variant;
        };

        GlobalSettings globalSettings;
        Metadata metadata;
        std::vector<RadioSettings> radioSettings;
        std::vector<InterferenceSource> interferenceSources;
    };



}


