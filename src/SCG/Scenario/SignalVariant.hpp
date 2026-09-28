#pragma once
#include <variant>
#include <complex>
#include <vector>


namespace Sagax::SCG {

    struct SignalVariant {
        struct Null {
        };

        struct Noise {
            double span;
        };

        struct Tone {
        };

        struct Chirp {
            double span;
            double rate;
        };

        struct Pulse {
            double duty;
            double rate;
        };

        struct AudioFile {
            enum struct Modulation { IQ, FM, AM, PM };
            Modulation mod;
            std::string fileName;
        };

        struct IQFile {
            std::string fileName;
            // when false the file plays once and silence follows
            bool loop = true;
        };

        using T = std::variant<
            Null,
            Noise,
            Tone,
            Chirp,
            Pulse,
            AudioFile,
            IQFile
        >;

        T variant;
    };
}



