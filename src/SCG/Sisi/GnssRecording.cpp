#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Modules/Packets.hpp"
#include "SCG/Signal/Gnss.hpp"
#include "GnssRecording.hpp"

namespace Sagax::SCG {

    namespace {

        using Constellation = Rid::InterferenceSource::Spoofing::Constellation;
        using Signal = SignalVariant::GnssSpoofing::Signal;

        // where two signals of one system share a center the older one is taken: L1CA
        // rather than L1C, L2C rather than the encrypted L2P, B2I rather than B2b. two of
        // the engine's signals are left out, GLO_G3 because its validator refuses it in
        // this build, and the beidou B2ab band because it has a frequency but no signal to
        // select with it
        struct Band {
            Constellation constellation;
            Signal signal;
        };

        constexpr Band bands[] = {
            { Constellation::gps, Signal::gpsL1ca },
            { Constellation::gps, Signal::gpsL2c },
            { Constellation::gps, Signal::gpsL5 },

            { Constellation::galileo, Signal::galE1 },
            { Constellation::galileo, Signal::galE6 },
            { Constellation::galileo, Signal::galE5b },
            { Constellation::galileo, Signal::galE5 },
            { Constellation::galileo, Signal::galE5a },

            { Constellation::beidou, Signal::bdsB1c },
            { Constellation::beidou, Signal::bdsB1i },
            { Constellation::beidou, Signal::bdsB3i },
            { Constellation::beidou, Signal::bdsB2i },
            { Constellation::beidou, Signal::bdsB2a },

            // glonass is fdma, the satellites sit at offsets from these centers, so a
            // narrow bandwidth holds only a few of the channels
            { Constellation::glonass, Signal::gloG1 },
            { Constellation::glonass, Signal::gloG2 },
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

    }

    // the centers in a rid are the nominal ones, so a whole hz is close enough
    std::optional<Signal> GnssSignalAt(Constellation constellation, double center_hz) {
        for(const auto &band : bands) {
            if(band.constellation == constellation && std::abs(GnssSignalCenter(band.signal) - center_hz) < 1.0) {
                return band.signal;
            }
        }
        return std::nullopt;
    }

    // written out of the table, so a center added there needs no second edit
    std::string GnssSupportedCenters(Constellation constellation) {
        std::string out = std::format("{} is only known at", NameOf(constellation));
        const char *separator = " ";
        for(const auto &band : bands) {
            if(band.constellation != constellation) {
                continue;
            }
            out += std::format("{}{}", separator, GnssSignalCenter(band.signal));
            separator = ", ";
        }
        return out;
    }

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
        // a mocked recording reads nothing and stays silent through the pad in the loop
        // below. it is still the length and the rate the scenario expects, which is what
        // lets a test be prepared and played with no engine and no ephemeris
        GnssEngine engine;
        if(mock) {
            Common::Logger::Info("gnss recording {} is mocked, the file will be silence", fileName);
        }
        else {
            Common::Logger::Info("opening the gnss engine for {}", fileName);
            engine.Open(spoofing, sampleRate_hz, duration_s, 0);
        }

        SSG::IO::IQFileOut<SCGSampleBase> file;

        SCGIQPacket packet;
        packet.Reshape(sampleRate_hz, 1, 0, true);
        auto &m = packet.meta.AddEntry<SSG::Packets::MFrequency>();
        m.frequency = GnssSignalCenter(spoofing.signal);
        file.Open(fileName, packet);
        Common::Assert(file.IsOpen(), std::format("could not open {}", fileName));

        const std::size_t nTotal = std::size_t(std::llround(duration_s * sampleRate_hz));
        std::size_t nWritten = 0;
        while(nWritten < nTotal && !stop.stop_requested()) {
            std::size_t n = std::min(scgPacketSize, nTotal - nWritten);
            packet.meta.Clear();
            packet.Reshape(sampleRate_hz, 1, n, false);

            // a read shorter than it was asked for is the end of the signal. the file
            // keeps the length the scenario expects, so the rest of it is silence
            std::size_t nRead = 0;
            if(!mock) {
                nRead = engine.Read(packet.data.data(), n);
            }
            std::fill(
                packet.data.begin() + std::ptrdiff_t(nRead),
                packet.data.end(),
                SCGSample{ 0.0f, 0.0f }
            );

            file.WritePacket(packet);
            nWritten += n;
        }
        engine.Close();

        bool failed = file.HasError();
        file.Close();
        Common::Assert(!failed, std::format("could not write {}", fileName));
    }

}
