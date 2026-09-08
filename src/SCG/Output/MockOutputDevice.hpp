#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <cstddef>
#include "SCG/Modules/Packets.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    // stands in for a radio when the server is configured without hardware. it holds
    // every packet back until the point the real device would have finished sending it,
    // so the pipeline is paced the same way, and it logs a line once per second of
    // stream time
    struct MockOutputDevice : public OutputDevice {
        std::string serial;
        double sampleRate;
        std::size_t nChannels;
        std::vector<double> centers;

        // the point the first packet was taken, every deadline is measured from it
        std::chrono::steady_clock::time_point streamStart;
        std::size_t nSamplesTotal = 0;
        std::size_t nSamplesLogged = 0; // samples counted since the last log line
        std::size_t nPacketsLogged = 0; // packets counted since the last log line
        std::size_t nPacketsNotNull = 0; // packets that were not null since the last log line

        void Start() override;
        void Stop() override;
        void Take(SCGIQPacket &packet) override;
    };

}
