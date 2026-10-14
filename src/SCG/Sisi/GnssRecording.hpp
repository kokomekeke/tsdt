#pragma once
#include <optional>
#include <string>
#include "SCG/Scenario/SignalVariant.hpp"
#include "SCG/Utility/Job.hpp"
#include "Rid.hpp"

namespace Sagax::SCG {

    // writes one gnss spoofing signal to an iq file, complex baseband at sampleRate_hz
    struct GnssRecordingJob : Job {
        SignalVariant::GnssSpoofing spoofing;
        double sampleRate_hz;
        double duration_s;
        std::string fileName;
        // writes silence instead of asking the engine, from mock_gnss in the config
        bool mock = false;

        std::string Describe() const override;
        double Estimate_s() const override;
        void Run(std::stop_token stop) override;
    };

    // the engine names a signal rather than a band, so a rid spoofer can only be played
    // on a center one of the signals of its constellation sits at
    std::optional<SignalVariant::GnssSpoofing::Signal> GnssSignalAt(
        Rid::InterferenceSource::Spoofing::Constellation constellation,
        double center_hz
    );

    // the centers GnssSignalAt knows for one constellation, written for an error message
    std::string GnssSupportedCenters(
        Rid::InterferenceSource::Spoofing::Constellation constellation
    );

}
