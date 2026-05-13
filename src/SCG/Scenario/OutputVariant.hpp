#pragma once
#include <variant>
#include <complex>
#include <string>
#include <vector>

namespace Sagax::SCG {
    struct OutputVariant {

        struct Null {
        };

        struct FileIQ {
            std::string fileName;
            std::size_t nChannels;
            double sampleRate;
            double center;
        };

        struct RadioUsrpB200 {
            std::string serial;
            std::string subdevSpec;
            double sampleRate;
            double center;
            double gain;
        };

        using T = std::variant<
            Null,
            RadioUsrpB200,
            FileIQ
        >;

        struct CommonParams {
            double sampleRate;
            std::size_t nChannels;
            std::vector<double> centers;
        };

        T variant;


        CommonParams GetCommonParams() const;

    };
}


