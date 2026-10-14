#include <chrono>
#include <format>
#include <string>
#include <vector>
#ifdef HAVE_SSP
#include "gnss/IfGenEngine.h"
#endif
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "Gnss.hpp"

namespace Sagax::SCG {

    using Signal = SignalVariant::GnssSpoofing::Signal;

    // the FREQ_ defines of the engine's ConstVal.h, in the pairing its own
    // SignalCenterFreq table uses
    double GnssSignalCenter(Signal signal) {
        switch(signal) {
            case Signal::gpsL1ca: return 1575420000.0;
            case Signal::gpsL2c: return 1227600000.0;
            case Signal::gpsL5: return 1176450000.0;
            case Signal::galE1: return 1575420000.0;
            case Signal::galE6: return 1278750000.0;
            case Signal::galE5b: return 1207140000.0;
            case Signal::galE5: return 1191795000.0;
            case Signal::galE5a: return 1176450000.0;
            case Signal::bdsB1c: return 1575420000.0;
            case Signal::bdsB1i: return 1561098000.0;
            case Signal::bdsB3i: return 1268520000.0;
            case Signal::bdsB2i: return 1207140000.0;
            case Signal::bdsB2a: return 1176450000.0;
            case Signal::gloG1: return 1602000000.0;
            case Signal::gloG2: return 1246000000.0;
        }
        Common::Assert(false, "unknown gnss signal");
        return 0.0;
    }

#ifdef HAVE_SSP

    namespace {

        IfGenSignalSelect ToIfGenSignal(Signal signal) {
            switch(signal) {
                case Signal::gpsL1ca: return { IfGenSignal::GPS_L1CA, IfGenSystem::GPS, true };
                case Signal::gpsL2c: return { IfGenSignal::GPS_L2C, IfGenSystem::GPS, true };
                case Signal::gpsL5: return { IfGenSignal::GPS_L5, IfGenSystem::GPS, true };
                case Signal::galE1: return { IfGenSignal::GAL_E1, IfGenSystem::Galileo, true };
                case Signal::galE6: return { IfGenSignal::GAL_E6, IfGenSystem::Galileo, true };
                case Signal::galE5b: return { IfGenSignal::GAL_E5b, IfGenSystem::Galileo, true };
                case Signal::galE5: return { IfGenSignal::GAL_E5, IfGenSystem::Galileo, true };
                case Signal::galE5a: return { IfGenSignal::GAL_E5a, IfGenSystem::Galileo, true };
                case Signal::bdsB1c: return { IfGenSignal::BDS_B1C, IfGenSystem::BDS, true };
                case Signal::bdsB1i: return { IfGenSignal::BDS_B1I, IfGenSystem::BDS, true };
                case Signal::bdsB3i: return { IfGenSignal::BDS_B3I, IfGenSystem::BDS, true };
                case Signal::bdsB2i: return { IfGenSignal::BDS_B2I, IfGenSystem::BDS, true };
                case Signal::bdsB2a: return { IfGenSignal::BDS_B2a, IfGenSystem::BDS, true };
                case Signal::gloG1: return { IfGenSignal::GLO_G1, IfGenSystem::GLONASS, true };
                case Signal::gloG2: return { IfGenSignal::GLO_G2, IfGenSystem::GLONASS, true };
            }
            Common::Assert(false, "unknown gnss signal");
            return {};
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

        // the engine starts at the first waypoint and stops after the last one, so the
        // last one is repeated at duration_s to hold the position until the end
        IfGenParams BuildParams(
            const SignalVariant::GnssSpoofing &spoofing,
            double sampleRate_hz,
            double duration_s,
            int nThreads
        ) {
            Common::Assert(!spoofing.trajectory.empty(), "gnss spoofing without a trajectory");
            Common::Assert(spoofing.trajectory.front().t == 0.0, "the first waypoint of a gnss trajectory has to be at 0");

            std::vector<SignalVariant::GnssSpoofing::Waypoint> waypoints = spoofing.trajectory;
            if(waypoints.back().t < duration_s) {
                SignalVariant::GnssSpoofing::Waypoint last = waypoints.back();
                last.t = duration_s;
                waypoints.push_back(last);
            }

            IfGenParams params;
            params.time = ToIfGenTime(spoofing.startTime);

            params.waypoints.reserve(waypoints.size());
            for(const auto &w : waypoints) {
                IfGenWaypoint point;
                point.latitudeDeg = w.latitude;
                point.longitudeDeg = w.longitude;
                point.altitudeM = w.altitude;
                point.timeS = w.t;
                params.waypoints.push_back(point);
            }
            // a trajectory is a list of places and times, not a curve drawn through them
            params.waypointInterp = IfGenWaypointInterp::Linear;

            params.ephemerisFile = spoofing.ephemerisFile;
            params.signals = { ToIfGenSignal(spoofing.signal) };
            params.output.sampleRateHz = sampleRate_hz;
            params.output.centerFreqHz = GnssSignalCenter(spoofing.signal);
            params.nThreads = nThreads;
            // the samples are pulled with Read, the engine writes nothing itself
            params.writeBin = false;
            return params;
        }

    }

    struct GnssEngine::Impl {
        IfGenEngine engine;
    };

    void GnssEngine::Open(const SignalVariant::GnssSpoofing &spoofing, double sampleRate_hz, double duration_s, int nThreads) {
        IfGenParams params = BuildParams(spoofing, sampleRate_hz, duration_s, nThreads);
        std::vector<std::string> paramErrors;
        if(!ValidateIfGenParams(params, paramErrors)) {
            Common::Assert(false, std::format(
                "the gnss engine refused its parameters: {}",
                paramErrors.empty() ? std::string("no reason given") : paramErrors.front()));
        }

        // building the sky reads the ephemeris and places every satellite, which is the
        // one slow step that is not per sample
        impl->engine.Init(params);
        Common::Assert(impl->engine.IsOpen(), "the gnss engine did not open");
        Common::Logger::Info("gnss engine ready, {} samples per block", impl->engine.Info().samplesPerBlock);
    }

    std::size_t GnssEngine::Read(SCGSample *out, std::size_t n) {
        return impl->engine.Read(out, n);
    }

    void GnssEngine::Close() {
        impl->engine.Close();
    }

#else

    struct GnssEngine::Impl {
    };

    void GnssEngine::Open(
        [[maybe_unused]] const SignalVariant::GnssSpoofing &spoofing,
        [[maybe_unused]] double sampleRate_hz,
        [[maybe_unused]] double duration_s,
        [[maybe_unused]] int nThreads
    ) {
        Common::Assert(false, "this build has no gnss engine");
    }

    std::size_t GnssEngine::Read([[maybe_unused]] SCGSample *out, [[maybe_unused]] std::size_t n) {
        Common::Assert(false, "this build has no gnss engine");
        return 0;
    }

    void GnssEngine::Close() {
    }

#endif

    GnssEngine::GnssEngine() : impl(std::make_unique<Impl>()) {
    }

    GnssEngine::~GnssEngine() = default;

}
