#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <vector>
#ifdef HAVE_SSP
#include "gnss/IfGenEngine.h"
#endif
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Modules/Packets.hpp"
#include "GnssRecording.hpp"

namespace Sagax::SCG {

#ifdef HAVE_SSP

    namespace {

        using Constellation = Rid::InterferenceSource::Spoofing::Constellation;

        // the engine is told which signal to generate, not which band, so a center has to
        // name one. the numbers are the FREQ_ defines of the engine's ConstVal.h, in the
        // pairing its own SignalCenterFreq table uses
        //
        // where two signals of one system share a center the older one is taken: L1CA
        // rather than L1C, L2C rather than the encrypted L2P, B2I rather than B2b. two of
        // the engine's signals are left out, GLO_G3 because its validator refuses it in
        // this build, and the beidou B2ab band because it has a frequency but no signal to
        // select with it
        struct Band {
            Constellation constellation;
            double center_hz;
            IfGenSignal signal;
            IfGenSystem system;
        };

        constexpr Band bands[] = {
            { Constellation::gps, 1575420000.0, IfGenSignal::GPS_L1CA, IfGenSystem::GPS },
            { Constellation::gps, 1227600000.0, IfGenSignal::GPS_L2C, IfGenSystem::GPS },
            { Constellation::gps, 1176450000.0, IfGenSignal::GPS_L5, IfGenSystem::GPS },

            { Constellation::galileo, 1575420000.0, IfGenSignal::GAL_E1, IfGenSystem::Galileo },
            { Constellation::galileo, 1278750000.0, IfGenSignal::GAL_E6, IfGenSystem::Galileo },
            { Constellation::galileo, 1207140000.0, IfGenSignal::GAL_E5b, IfGenSystem::Galileo },
            { Constellation::galileo, 1191795000.0, IfGenSignal::GAL_E5, IfGenSystem::Galileo },
            { Constellation::galileo, 1176450000.0, IfGenSignal::GAL_E5a, IfGenSystem::Galileo },

            { Constellation::beidou, 1575420000.0, IfGenSignal::BDS_B1C, IfGenSystem::BDS },
            { Constellation::beidou, 1561098000.0, IfGenSignal::BDS_B1I, IfGenSystem::BDS },
            { Constellation::beidou, 1268520000.0, IfGenSignal::BDS_B3I, IfGenSystem::BDS },
            { Constellation::beidou, 1207140000.0, IfGenSignal::BDS_B2I, IfGenSystem::BDS },
            { Constellation::beidou, 1176450000.0, IfGenSignal::BDS_B2a, IfGenSystem::BDS },

            // glonass is fdma, these are the band centers and the satellites sit at
            // offsets from them, so a narrow bandwidth holds only a few of the channels
            { Constellation::glonass, 1602000000.0, IfGenSignal::GLO_G1, IfGenSystem::GLONASS },
            { Constellation::glonass, 1246000000.0, IfGenSignal::GLO_G2, IfGenSystem::GLONASS },
        };

        const char *NameOf(Constellation constellation) {
            switch(constellation) {
                case Constellation::gps: return "GPS";
                case Constellation::glonass: return "GLONASS";
                case Constellation::galileo: return "GALILEO";
                case Constellation::beidou: return "BEIDOU";
            }
            return "unknown";
        }

        // the centers in a rid are the nominal ones, so a whole hz is close enough
        const Band *FindBand(Constellation constellation, double center_hz) {
            for(const auto &band : bands) {
                if(band.constellation == constellation && std::abs(band.center_hz - center_hz) < 1.0) {
                    return &band;
                }
            }
            return nullptr;
        }

        IfGenTime ToIfGenTime(std::chrono::system_clock::time_point t) {
            auto seconds = std::chrono::floor<std::chrono::seconds>(t);
            auto days = std::chrono::floor<std::chrono::days>(seconds);
            std::chrono::year_month_day date{days};
            std::chrono::hh_mm_ss<std::chrono::seconds> timeOfDay{seconds - days};

            IfGenTime out;
            out.year = int(date.year());
            out.month = int(unsigned(date.month()));
            out.day = int(unsigned(date.day()));
            out.hour = int(timeOfDay.hours().count());
            out.minute = int(timeOfDay.minutes().count());
            // the floor above dropped the fraction of a second, it is put back here
            out.second = double(timeOfDay.seconds().count())
                + std::chrono::duration<double>(t - seconds).count();
            return out;
        }

        IfGenParams BuildParams(const GnssRecordingJob &job, const Band &band) {
            IfGenParams params;
            params.time = ToIfGenTime(job.startTime);

            params.waypoints.reserve(job.waypoints.size());
            for(const auto &w : job.waypoints) {
                IfGenWaypoint point;
                point.latitudeDeg = w.latitude;
                point.longitudeDeg = w.longitude;
                point.altitudeM = w.altitude;
                point.timeS = w.t;
                params.waypoints.push_back(point);
            }
            // a trajectory is a list of places and times, not a curve drawn through them
            params.waypointInterp = IfGenWaypointInterp::Linear;

            params.ephemerisFile = job.ephemerisFile;
            params.signals = { { band.signal, band.system, true } };
            params.output.sampleRateHz = job.sampleRate_hz;
            params.output.centerFreqHz = job.center_hz;
            // the samples are pulled with Read, the engine writes nothing itself
            params.writeBin = false;
            return params;
        }

    }

    bool GnssCenterIsSupported(Constellation constellation, double center_hz) {
        return FindBand(constellation, center_hz) != nullptr;
    }

    // written out of the table, so a center added there needs no second edit
    std::string GnssSupportedCenters(Constellation constellation) {
        std::string out = std::format("{} is only known at", NameOf(constellation));
        const char *separator = " ";
        for(const auto &band : bands) {
            if(band.constellation != constellation) {
                continue;
            }
            out += std::format("{}{}", separator, band.center_hz);
            separator = ", ";
        }
        return out;
    }

#endif

    std::string GnssRecordingJob::Describe() const {
        return std::format("gnss recording {}", fileName);
    }

    // measured at 1.8 times real time at 1.92 msps and 1.1 times at 480 ksps, and the cost
    // is per satellite and millisecond rather than per sample, so a narrower band is not
    // cheaper. one second of work per second of signal is the safe side of both
    double GnssRecordingJob::Estimate_s() const {
        return duration_s;
    }

    void GnssRecordingJob::Run(std::stop_token stop) {
        // a recording that reads nothing stays silent through the pad in the loop below.
        // it is still the length and the rate the scenario expects, which is what lets a
        // test be prepared and played with no engine and no ephemeris
        bool silent = mock;

#ifdef HAVE_SSP
        IfGenEngine engine;
        if(mock) {
            Common::Logger::Info("gnss recording {} is mocked, the file will be silence", fileName);
        }
        else {
            const Band *band = FindBand(constellation, center_hz);
            Common::Assert(band != nullptr, std::format(
                "no gnss signal is known at {} hz, {}",
                center_hz, GnssSupportedCenters(constellation)));

            IfGenParams params = BuildParams(*this, *band);
            std::vector<std::string> paramErrors;
            if(!ValidateIfGenParams(params, paramErrors)) {
                Common::Assert(false, std::format(
                    "the gnss engine refused the parameters of {}: {}",
                    fileName, paramErrors.empty() ? std::string("no reason given") : paramErrors.front()));
            }

            // building the sky reads the ephemeris and places every satellite, which is
            // the one slow step that is not per sample
            engine.Init(params);
            Common::Assert(engine.IsOpen(), std::format("the gnss engine did not open for {}", fileName));
            Common::Logger::Info(
                "gnss engine ready for {}, {} samples per block",
                fileName, engine.Info().samplesPerBlock
            );
        }
#else
        // ValidateRid refuses a spoofer that is not mocked in a build without the engine,
        // so reaching this with mock unset means a test got past it somehow
        silent = true;
        Common::Logger::Warn("gnss recording {} is silence, this build has no gnss engine", fileName);
#endif

        SSG::IO::IQFileOut<SCGSampleBase> file;

        SCGIQPacket packet;
        packet.Reshape(sampleRate_hz, 1, 0, true);
        auto &m = packet.meta.AddEntry<SSG::Packets::MFrequency>();
        m.frequency = center_hz;
        file.Open(fileName, packet);
        Common::Assert(file.IsOpen(), std::format("could not open {}", fileName));

        const std::size_t nTotal = std::size_t(std::llround(duration_s * sampleRate_hz));
        std::size_t nWritten = 0;
        while(nWritten < nTotal && !stop.stop_requested()) {
            std::size_t n = std::min(scgPacketSize, nTotal - nWritten);
            packet.meta.Clear();
            packet.Reshape(sampleRate_hz, 1, n, false);

            // a read shorter than it was asked for is the trajectory running out. the file
            // keeps the length the scenario expects, so the rest of it is silence
            std::size_t nRead = 0;
#ifdef HAVE_SSP
            if(!silent) {
                nRead = engine.Read(packet.data.data(), n);
            }
#endif
            if(nRead < n) {
                std::fill(
                    packet.data.begin() + std::ptrdiff_t(nRead),
                    packet.data.end(),
                    SCGSample{ 0.0f, 0.0f }
                );
            }

            file.WritePacket(packet);
            nWritten += n;
        }
#ifdef HAVE_SSP
        if(!silent) {
            engine.Close();
        }
#endif

        bool failed = file.HasError();
        file.Close();
        Common::Assert(!failed, std::format("could not write {}", fileName));
    }

}
