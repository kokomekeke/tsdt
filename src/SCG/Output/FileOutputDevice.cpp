#include <algorithm>
#include <vector>
#include <span>
#include <complex>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/Math/Math.hpp"
#include "FileOutputDevice.hpp"


namespace Sagax::SCG {

    void FileOutputDevice::Start() {
        packet.Reshape(sampleRate, nChannels, 0, true);
        auto &m = packet.meta.AddEntry<SSG::Packets::MFrequency>();
        m.frequency = center;
        file.Open(fileName, packet);
        packet.meta.Clear();
        Common::Assert(file.IsOpen(), "failed to open file");
    }

    void FileOutputDevice::Stop() {
        if(file.IsOpen()) {
            file.Close();
        }
    }

    void FileOutputDevice::Take(const std::vector<std::span<const SCGSample>> &channels) {
        Common::Assert(file.IsOpen(), "file not open");
        Common::Assert(channels.size() == nChannels, "wrong number of channels");
        packet.Reshape(sampleRate, nChannels, channels[0].size(), false);
        auto out = packet.GetChannels();
        for(std::size_t ic = 0; ic < nChannels; ++ic) {
            std::copy(channels[ic].begin(), channels[ic].end(), out[ic].begin());
        }
        file.WritePacket(packet);
    }

}

