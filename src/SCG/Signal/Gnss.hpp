#pragma once
#include <cstddef>
#include <memory>
#include "SCG/Signal/Sample.hpp"
#include "SCG/Scenario/SignalVariant.hpp"

namespace Sagax::SCG {

    // the nominal center of a gnss signal. glonass is fdma, for it this is the center of
    // the band and the satellites sit at offsets from it
    double GnssSignalCenter(SignalVariant::GnssSpoofing::Signal signal);

    // the ssp engine generating one gnss spoofing signal as complex baseband around
    // GnssSignalCenter. a build without the engine asserts on Open
    struct GnssEngine {
        struct Impl;
        std::unique_ptr<Impl> impl;

        GnssEngine();
        ~GnssEngine();

        // the signal runs for duration_s, the last waypoint is held until then.
        // sampleRate_hz has to be whole khz. nThreads 0 lets openmp choose
        void Open(const SignalVariant::GnssSpoofing &spoofing, double sampleRate_hz, double duration_s, int nThreads);
        // blocks until n samples are generated, fewer once the signal is over
        std::size_t Read(SCGSample *out, std::size_t n);
        void Close();
    };

}
