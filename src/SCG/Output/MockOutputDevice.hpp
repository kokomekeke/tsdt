#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <cstddef>
#include "SCG/Modules/Packets.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    // 16 send frames of 8176 bytes, each with a 16 byte header, 4 bytes to a sample. the
    // transmit fifo of the fpga is not documented and is not counted
    inline constexpr std::size_t mockBufferSizeUsrpB200 = 16 * (8176 - 16) / 4;

    // the 32768 sample host ring buffer of the driver, plus its share of the 16384 word
    // fpga fifo, which keeps a 2048 word margin and is split between the two channels
    inline constexpr std::size_t mockBufferSizeSidekiq = 32768 + (16384 - 2048) / 2;

    // stands in for a radio when the server is configured without hardware. it tracks the
    // level of the transmit buffer as a sample count, never the samples, and takes a
    // packet once there is room for it, so the pipeline runs up to one buffer ahead
    struct MockOutputDevice : public OutputDevice {
        std::string serial;
        double sampleRate;
        std::size_t nChannels;
        std::vector<double> centers;
        std::size_t bufferSize = 0; // 0 paces each packet on its own

        std::chrono::steady_clock::time_point streamStart;
        std::size_t nSamplesTotal = 0;
        std::size_t nSamplesGap = 0; // clocked out with the buffer empty
        std::size_t nSamplesLogged = 0;
        std::size_t nPacketsLogged = 0;
        std::size_t nPacketsNotNull = 0;
        std::size_t nSamplesGapLogged = 0;
        bool inUnderflow = false;

        void Start() override;
        void Stop() override;
        void Take(SCGIQPacket &packet) override;
    };

}
