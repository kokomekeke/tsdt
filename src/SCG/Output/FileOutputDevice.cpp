#include <thread>
#include <vector>
#include <span>
#include <complex>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SSG/Math/Math.hpp"
#include "FileOutputDevice.hpp"


namespace Sagax::SCG {

    void FileOutputDevice::Start() {
        SCGIQPacket dummy;
        dummy.Reshape(sampleRate, nChannels, 0, true);
        auto &m = dummy.meta.AddEntry<SSG::Packets::MFrequency>();
        m.frequency = center;
        file.Open(fileName, dummy);
        Common::Assert(file.IsOpen(), "failed to open file");
    }

    void FileOutputDevice::Stop() {
        if(file.IsOpen()) {
            file.Close();
        }
    }

    void FileOutputDevice::Take(SCGIQPacket &packet) {
        Common::Assert(file.IsOpen(), "file not open");
        Common::Assert(packet.nChannels == nChannels, "wrong number of channels");
        // assert sample rate

        file.WritePacket(packet);
    }

}

