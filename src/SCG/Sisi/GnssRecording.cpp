#include <algorithm>
#include <cmath>
#include <format>
#include "Common/Assert.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Modules/Packets.hpp"
#include "GnssRecording.hpp"

namespace Sagax::SCG {

    std::string GnssRecordingJob::Describe() const {
        return std::format("gnss recording {}", fileName);
    }

    // assumes the generation runs at real time speed
    double GnssRecordingJob::Estimate_s() const {
        return duration_s;
    }

    // TODO: generate with the gnss engine, until then the file is silence of the right length
    void GnssRecordingJob::Run(std::stop_token stop) {
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
            std::fill(packet.data.begin(), packet.data.end(), SCGSample{ 0.0f, 0.0f });
            file.WritePacket(packet);
            nWritten += n;
        }

        bool failed = file.HasError();
        file.Close();
        Common::Assert(!failed, std::format("could not write {}", fileName));
    }

}
