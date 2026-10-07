#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include "SCG/Utility/Job.hpp"
#include "Rid.hpp"

namespace Sagax::SCG {

    // writes one gnss spoofer to an iq file, complex baseband at sampleRate_hz
    struct GnssRecordingJob : Job {
        using Constellation = Rid::InterferenceSource::Spoofing::Constellation;
        using Waypoint = Rid::InterferenceSource::Spoofing::TrajectoryItem;

        Constellation constellation;
        std::vector<std::int32_t> prnList; // empty selects every satellite in view
        std::vector<Waypoint> waypoints; // t is relative to the first sample of the file
        std::chrono::system_clock::time_point startTime; // time of the first sample
        double center_hz;
        double sampleRate_hz;
        double duration_s;
        std::string ephemerisFile;
        std::string fileName;
        // writes silence instead of asking the engine, from mock_gnss in the config
        bool mock = false;

        std::string Describe() const override;
        double Estimate_s() const override;
        void Run(std::stop_token stop) override;
    };

#ifdef HAVE_SSP

    // the engine names a signal rather than a band, so a spoofer can only be recorded on
    // a center the mapping knows. ValidateRid asks this before a test is scheduled, so a
    // center with no signal is refused instead of failing in the preparation
    bool GnssCenterIsSupported(
        Rid::InterferenceSource::Spoofing::Constellation constellation,
        double center_hz
    );

    // the centers the mapping knows for one constellation, written for an error message
    std::string GnssSupportedCenters(
        Rid::InterferenceSource::Spoofing::Constellation constellation
    );

#endif

}
